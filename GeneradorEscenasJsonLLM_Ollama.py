import asyncio
import json
import os
import re
import traceback
import uuid
from typing import Any, Dict, List, Optional
import guidance
import httpx
import uvicorn
from fastapi import FastAPI, Request
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from openai import AsyncOpenAI
from pydantic import BaseModel, Field, ValidationError

# Cambia a True si deseas forzar la generación inicial con Guidance
USE_GUIDANCE: bool = False

# ==============================================================================
# CONFIGURACIÓN DE PUERTOS (Los modelos se autodetectan al arrancar)
# ==============================================================================
ORCHESTRATOR_BASE_URL = "http://localhost:11434/v1"   # Puerto de Ollama con OpenCode
ORCHESTRATOR_API_KEY = "ollama"

SPECIALIST_BASE_URL = "http://localhost:8080/v1"      # Puerto del LLM especialista 
SPECIALIST_API_KEY = "sk-no-key-required"

SPECIALIST_AGENT_PORT = 8001                          # Puerto interno del servidor ACP
SPECIALIST_AGENT_URL = f"http://127.0.0.1:{SPECIALIST_AGENT_PORT}"

#Estos puertos y URLS se usan en Httpx para que el ACP pueda mandar los mensajes y prompts de ambos agentes entre ellos

def extraer_json_limpio(texto_crudo: str) -> Dict[str, Any]: #Método encargado de devolver el json generado de un modelo a un lenguaje universal, de esta manera evitamos fallos entre modelos que no tengan sintaxis de JSON parecidas

    texto = re.sub(r"<think>.*?</think>", "", texto_crudo, flags=re.DOTALL).strip()
    texto = re.sub(r"^```(?:json)?\s*|\s*```$", "", texto, flags=re.MULTILINE).strip()
    inicio = texto.find("{")
    fin = texto.rfind("}")
    if inicio != -1 and fin != -1:
        texto = texto[inicio:fin + 1]
        
    # Elimina comas finales antes de '}' o ']' (error clásico de modelos 2B-7B)
    texto = re.sub(r",\s*([}\]])", r"\1", texto)
    return json.loads(texto)

async def detectar_modelo_abierto(base_url: str, cliente: AsyncOpenAI) -> str: #Método que comprueba que modelos son los que están cargados actualmente en cada puerto
    # Comprobación del puerto de Ollama
    if "11434" in base_url:
        try:
            ollama_root = base_url.replace("/v1", "") #Cambiamos a una ruta universal para buscar los activos
            async with httpx.AsyncClient(timeout=25.0) as http: 
                resp = await http.get(f"{ollama_root}/api/ps")
                if resp.status_code == 200: #En caso de que HTTPX encuentre alguna sesión de ollama pasamos los modelos activos y devolvemos el nombre del primer modelo activo
                    modelos_activos = resp.json().get("models", [])
                    if modelos_activos:
                        return modelos_activos[0]["name"]
        except Exception:
            pass
    try:
        modelos = await cliente.models.list() #Devuelve la lista de modelos que tiene le cliente
        if modelos.data:
            return modelos.data[0].id #Nos quedamos con el primer modelo cargado
    except Exception as e:
        raise RuntimeError(f"No se pudo detectar ningún modelo abierto en {base_url}: {e}")

    raise RuntimeError(f"El servidor en {base_url} está encendido pero no tiene ningún modelo cargado.")

# ============================================
# 1. Clases de contexto de Unreal para la IA
# ============================================

#Vector 3 de Unreal on las 3 posiciones x,y,z
class Vector3(BaseModel):
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
# Rotation de Unreal on las 3 posiciones x,y,z
class Rotation(BaseModel):
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
# Transform de Unreal 2 vectores 3 Posición y Escala y un Rotation que es la Rotacion
class Transform(BaseModel):
    position: Vector3 = Field(default_factory=Vector3)
    rotation: Rotation = Field(default_factory=Rotation)
    scale: Vector3 = Field(default_factory=lambda: Vector3(x=1.0, y=1.0, z=1.0))
# Actor de Unreal, que define a un Actor en Unreal: Su ID, Su clase, Su Transform en escena y si tiene sus propiedades
class ActorData(BaseModel):
    actor_id: str = Field(description="Nombre o ID único del actor")
    actor_class: str = Field(description="Ruta de la clase en Unreal")
    transform: Transform
    properties: Optional[Dict[str, Any]] = Field(default=None)

#JSON a generar en forma de escena de UNREAL
class EscenaUnreal(BaseModel):
    scene_name: str = Field(description="Nombre del nivel o escena")
    unreal_version: str = Field(default="5.4")
    timestamp: Optional[str] = Field(default=None)
    actors: List[ActorData] = Field(description="Lista de actores de la escena")

#JSON a generar si el modelo requiere alguna herramienta MCP 
class DecisiónHerramienta(BaseModel):
    requiere_mcp: bool = Field(description="True si necesita consultar algo al MCP antes de generar la escena")
    tool_name: Optional[str] = Field(default=None, description="Nombre de la herramienta MCP a ejecutar")
    arguments: Optional[Dict[str, Any]] = Field(default=None, description="Parámetros para la herramienta")

#Mensaje ACP que obliga a los modelos a mandar un formato de mensajes limpio y conocido entre ellos para el A2A
class ACPMessage(BaseModel):
    sender_agent: str
    target_agent: str
    task_type: str  # EVALUATE_TOOLS | CREATE_JSON | REPAIR_JSON | RESPONSE | ERROR
    payload: Dict[str, Any]


## =====================================================================
# 2. Servidor Agente Especialista (A2A / ACP en background)
# =====================================================================
#NUESTRA TRAMPA para el LLM, el LLM no sabe de ACP ni de su sistema de JSON pero podemos chivarselo usando FastAPI, esto permite generar o pasar una API de lo que nosotros queramos eligiendo el sistema de ACP que Ollama y OpenCode ya conocen 
specialist_app = FastAPI(title="Unreal Specialist Agent") 
#Donde esta el LLM y cuanto de margen tiene
specialist_llm_client = AsyncOpenAI(
    base_url=SPECIALIST_BASE_URL,
    api_key=SPECIALIST_API_KEY,
    timeout=3000.0
)

@specialist_app.get("/.well-known/agent.json") #SINTAXIS DE FASTAPI (/.well-known/agent.json) indica la api de A2A la sección del JSON
async def specialist_agent_card(): #En que formato debe devolver el LLM usando A2A / ACP
    modelo_activo = await detectar_modelo_abierto(SPECIALIST_BASE_URL, specialist_llm_client)
    return {
        "name": "unreal_specialist_agent",
        "description": "Agente especialista en generar y reparar JSON de escenas de Unreal Engine.",
        "model": modelo_activo,
        "url": f"{SPECIALIST_AGENT_URL}/rpc",
        "version": "1.0.0",
        "capabilities": {"tasks": ["CREATE_JSON", "REPAIR_JSON"]}
    }

@specialist_app.post("/rpc") #SINTAXIS DE FASTAPI esto hace que la petición que le llega al LLM se traduzca en texto que entienda
async def specialist_handle_rpc(request: Request):
    #Segmentación de la request parseada en JSON
    body = await request.json()
    rpc_id = body.get("id")
    method = body.get("method")
    params = body.get("params", {})

    if method not in ["CREATE_JSON", "REPAIR_JSON"]: #Si no es una petición conocida la ignoramos
        return {
            "jsonrpc": "2.0",
            "id": rpc_id,
            "error": {"code": -32601, "message": f"Método no soportado: {method}"}
        }

    try: #Procesamos la petición
        system_prompt = params.get("system_prompt", "")
        user_prompt = params.get("user_prompt", "")
        schema_str = json.dumps(EscenaUnreal.model_json_schema(), ensure_ascii=False)
        modelo_activo = await detectar_modelo_abierto(SPECIALIST_BASE_URL, specialist_llm_client)

        print(f"\n-> [Agente Especialista ({modelo_activo})] Generando JSON en streaming:")
        stream = await specialist_llm_client.chat.completions.create(
            model=modelo_activo,
            messages=[
                {
                    "role": "system",
                    "content": (
                        f"{system_prompt}\n\n"
                        f"Debes responder ÚNICAMENTE con un objeto JSON válido que respete este esquema:\n"
                        f"{schema_str}\n"
                        "No incluyas bloques Markdown, ni saludos, ni explicaciones previas o posteriores."
                    )
                },
                {"role": "user", "content": user_prompt}
            ],
            temperature=0.2,
            frequency_penalty=1.1,
            stream=True
        )
        #Una vez guarada la respuesta del model en la variable stream (para poder segmentarlo)
        fragmentos = []
        async for chunk in stream: #Procesamos el texto de manera que después podamos leerlo y traducirlo más fácil
            delta = chunk.choices[0].delta.content or ""
            if delta:fragmentos.append(delta)
        contenido = "".join(fragmentos)
        try:
            resultado_json = extraer_json_limpio(contenido)
        except json.JSONDecodeError:
            # Si tiene un error de sintaxis irreductible, lo devolvemos como texto
            resultado_json = contenido
        return {
            "jsonrpc": "2.0",
            "id": rpc_id,
            "result": {
                "sender_agent": "unreal_specialist_agent",
                "status": "completed",
                "data": resultado_json
            }
        }

    except Exception as e:
        return {
            "jsonrpc": "2.0",
            "id": rpc_id,
            "error": {
                "code": -32000,
                "message": repr(e),
                "data": traceback.format_exc()
            }
        }

async def iniciar_servidor_especialista(): #Inicializa el agente LLM encargado de la generación de escenas dentro

    config = uvicorn.Config(
        app=specialist_app,
        host="127.0.0.1",
        port=SPECIALIST_AGENT_PORT,
        log_level="error"
    )
    server = uvicorn.Server(config)
    asyncio.create_task(server.serve())
    await asyncio.sleep(0.5)


# ============================================
# 3. Router ACP / A2A y Clientes LLM
# ============================================

class ACPClient:
    def __init__(self, orchestrator_client: AsyncOpenAI, remote_agents: Dict[str, str]):
        self.orchestrator = orchestrator_client
        self.remote_agents = remote_agents

    async def discover_remote_agent(self, agent_name: str) -> Dict[str, Any]: #Para que los modelos se encuentren entre ellos
        base_url = self.remote_agents[agent_name]
        async with httpx.AsyncClient(timeout=30000.0) as client:
            resp = await client.get(f"{base_url}/.well-known/agent.json")
            resp.raise_for_status()
            return resp.json()

    async def send_message(self, message: ACPMessage) -> ACPMessage:
        # Evaluación local mediante el Orquestador / Harness
        if message.target_agent == "harness_orchestrator":
            try:
                modelo_orquestador = await detectar_modelo_abierto(ORCHESTRATOR_BASE_URL, self.orchestrator)
                print(f"-> [ACP Router] Enrutando '{message.task_type}' hacia Orquestador ({modelo_orquestador})...")

                schema_str = json.dumps(DecisiónHerramienta.model_json_schema(), ensure_ascii=False)
                system_prompt = (
                    f"{message.payload.get('system_prompt', '')}\n\n"
                    f"Ciñete estrictamente a este esquema JSON:\n{schema_str}\n"
                    "No agregues texto adicional ni bloques Markdown."
                )

                respuestaLLM = await self.orchestrator.chat.completions.create(
                    model=modelo_orquestador,
                    messages=[
                        {"role": "system", "content": system_prompt},
                        {"role": "user", "content": message.payload.get("user_prompt", "")}
                    ],
                    response_format={"type": "json_object"},
                    temperature=0.2,
                )

                datos_json = extraer_json_limpio(respuestaLLM.choices[0].message.content)

                return ACPMessage(
                    sender_agent="harness_orchestrator",
                    target_agent=message.sender_agent,
                    task_type="RESPONSE",
                    payload={"JSON generado": datos_json}
                )

            except Exception as e:
                print(f"Error en el Router al procesar EVALUATE_TOOLS: {repr(e)}")
                return ACPMessage(
                    sender_agent="harness_orchestrator",
                    target_agent=message.sender_agent,
                    task_type="ERROR",
                    payload={"error": repr(e), "trace": traceback.format_exc()}
                )

        # Delegación por protocolo JSON-RPC 2.0 hacia el agente remoto
        elif message.target_agent in self.remote_agents:
            print(f"-> [ACP Router] Tarea {message.task_type} de {message.sender_agent} hacia {message.target_agent}")
            try:
                agent_card = await self.discover_remote_agent(message.target_agent)
                rpc_url = agent_card["url"]

                rpc_payload = {
                    "jsonrpc": "2.0",
                    "id": str(uuid.uuid4()),
                    "method": message.task_type,
                    "params": message.payload
                }

                async with httpx.AsyncClient(timeout=30000.0) as client:
                    resp = await client.post(rpc_url, json=rpc_payload)
                    resp.raise_for_status()
                    rpc_response = resp.json()

                if "error" in rpc_response:
                    return ACPMessage(
                        sender_agent=message.target_agent,
                        target_agent=message.sender_agent,
                        task_type="ERROR",
                        payload={"error": rpc_response["error"]["message"]}
                    )

                resultado_json = rpc_response["result"]["data"]
                clave_payload = "JSON reparado" if message.task_type == "REPAIR_JSON" else "JSON generado"

                return ACPMessage(
                    sender_agent=message.target_agent,
                    target_agent=message.sender_agent,
                    task_type="RESPONSE",
                    payload={clave_payload: resultado_json}
                )

            except Exception as e:
                print(f"Error crítico {message.sender_agent} no ha podido mandar {message.task_type} a {message.target_agent}: {repr(e)}")
                return ACPMessage(
                    sender_agent=message.target_agent,
                    target_agent=message.sender_agent,
                    task_type="ERROR",
                    payload={"error": repr(e), "trace": traceback.format_exc()}
                )

        return ACPMessage(
            sender_agent="router",
            target_agent=message.sender_agent,
            task_type="ERROR",
            payload={"error": f"Agente destino no registrado: {message.target_agent}"}
        )

# ==========================================
# 4. Configuración de modelos y del MCP
# ==========================================

if USE_GUIDANCE:
    llm = guidance.models.OpenAI(
        model="default",
        base_url=SPECIALIST_BASE_URL,
        api_key=SPECIALIST_API_KEY,
    )
else:
    orchestrator_client = AsyncOpenAI(
        base_url=ORCHESTRATOR_BASE_URL,
        api_key=ORCHESTRATOR_API_KEY,
        timeout=30000.0
    )
    acp_router = ACPClient(
        orchestrator_client=orchestrator_client,
        remote_agents={"unreal_specialist_agent": SPECIALIST_AGENT_URL}
    )

MCP_SERVER_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "server.py"))

server_params = StdioServerParameters(
    command="uv",
    args=["run", "--with", "mcp[cli]", "mcp", "run", MCP_SERVER_PATH],
    env=None,
)

# =======================================
# 5. Lectura de @mcp resources del MCP
# =======================================

async def leer_recursos_mcp(session_mcp: ClientSession, resources_info: List[Dict[str, Any]]) -> str:
    contexto_recursos = ""
    for res in resources_info:
        try:
            resource_data = await session_mcp.read_resource(res["uri"])
            contenido = resource_data.contents[0].text
            contexto_recursos += f"\n[Recurso MCP ({res['uri']})]: {contenido}"
        except Exception as e:
            print(f"Advertencia: No se pudo leer el recurso {res['uri']}: {e}")
    return contexto_recursos


# =============================================================
# 6. Reparación de JSON mediante el agente especialista
# =============================================================

async def reparar_Json(escenaJson: Any, err: Exception, prompt_user: str) -> Dict[str, Any]:
    json_str = (
        json.dumps(escenaJson, indent=2, ensure_ascii=False)
        if isinstance(escenaJson, dict)
        else str(escenaJson)
    )

    prompt_final = (
        "Has generado este JSON siguiendo una serie de pautas pedidas por el usuario, "
        "no obstante el validador ha detectado fallos en el esquema Pydantic."
    )

    acp_scene_request = ACPMessage(
        sender_agent="harness_orchestrator",
        target_agent="unreal_specialist_agent",
        task_type="REPAIR_JSON",
        payload={
            "system_prompt": (
                f"{prompt_final}\n\n"
                f"Error detectado:\n{err}\n\n"
                "Corrige los campos erróneos y devuelve el objeto JSON completo y válido."
            ),
            "user_prompt": (
                f"Petición original del usuario: {prompt_user}\n\n"
                f"JSON defectuoso a reparar:\n{json_str}"
            )
        }
    )

    acp_scene_answer = await acp_router.send_message(acp_scene_request)
    if acp_scene_answer.task_type == "ERROR":
        raise RuntimeError(f"Error en el agente especialista al reparar JSON: {acp_scene_answer.payload}")

    return acp_scene_answer.payload["JSON reparado"]


# =============================================================
# 7. Generación de escena de Unreal en JSON con consulta MCP
# =============================================================

async def generar_escena_con_mcp(
    prompt_usuario: str,
    session_mcp: ClientSession,
    tools_info: List[Dict[str, Any]],
    resources_context: str
) -> EscenaUnreal:

    prompt_evaluacion = (
        "Eres un asistente de Unreal Engine. Analiza la petición del usuario junto con el contexto actual "
        "y determina si necesitas llamar a alguna herramienta MCP para consultar o modificar algo previo:\n"
        f"Herramientas MCP disponibles: {json.dumps(tools_info, ensure_ascii=False)}\n"
        f"Contexto de Resources activos: {resources_context}"
    )

    if USE_GUIDANCE:
        with guidance.system():
            lm = llm + prompt_evaluacion
        with guidance.user():
            lm += prompt_usuario
        with guidance.assistant():
            lm += guidance.json(name="decision", schema=DecisiónHerramienta)
        decision = DecisiónHerramienta.model_validate_json(lm["decision"])
    else:
        print("-> [Harness] Consultando al orquestador si requiere herramientas o recursos...")

        acp_eval_request = ACPMessage(
            sender_agent="user_cli",
            target_agent="harness_orchestrator",
            task_type="EVALUATE_TOOLS",
            payload={
                "system_prompt": (
                    "Analiza la petición del usuario junto con las herramientas y recursos MCP disponibles. "
                    "Debes decidir si es necesario invocar alguna herramienta para cumplir con la tarea. "
                    "Tu respuesta debe ceñirse estrictamente al formato JSON requerido por DecisiónHerramienta."
                ),
                "user_prompt": (
                    f"Petición del usuario: '{prompt_usuario}'\n\n"
                    f"Contexto de evaluación:\n{prompt_evaluacion}"
                )
            }
        )

        try:
            acp_eval_answer = await acp_router.send_message(acp_eval_request)
            if acp_eval_answer.task_type == "ERROR":
                raise RuntimeError(f"Fallo en evaluación de herramientas: {acp_eval_answer.payload}")

            lm_decision_raw = acp_eval_answer.payload.get("JSON generado")

            if isinstance(lm_decision_raw, dict):
                decision = DecisiónHerramienta.model_validate(lm_decision_raw)
            else:
                decision = DecisiónHerramienta.model_validate_json(lm_decision_raw)

            print(f"-> [Harness] Decisión validada con éxito. ¿Requiere MCP?: {decision.requiere_mcp}")

        except (ValidationError, json.JSONDecodeError, KeyError, TypeError) as err:
            print(f"Error al evaluar o validar la decisión de herramientas: {err}")
            raise err

    mcp_context = ""

    if decision.requiere_mcp and decision.tool_name:
        print(f"\n[MCP] Ejecutando herramienta previa: '{decision.tool_name}' con args: {decision.arguments}")

        mcp_response = await session_mcp.call_tool(
            decision.tool_name,
            arguments=decision.arguments or {}
        )

        if mcp_response.content and hasattr(mcp_response.content[0], "text"):
            contenido_texto = mcp_response.content[0].text
        else:
            contenido_texto = str(mcp_response.content)

        mcp_context = f"\n[Datos obtenidos de Unreal vía MCP ({decision.tool_name})]: {contenido_texto}"
        print(f"[MCP] Datos recibidos: {mcp_context}")

    prompt_final = (
        "Eres un diseñador de niveles experto de Unreal Engine. Genera la escena final en formato JSON. "
        "Utiliza la información proporcionada por el usuario y los datos reales obtenidos de Unreal si existen."
    )

    contexto_completo = f"{resources_context}\n{mcp_context}"

    if USE_GUIDANCE:
        with guidance.system():
            lm_final = llm + prompt_final + f"\nContexto del Editor: {contexto_completo}"
        with guidance.user():
            lm_final += prompt_usuario
        with guidance.assistant():
            lm_final += guidance.json(name="escena_json", schema=EscenaUnreal)
        return EscenaUnreal.model_validate_json(lm_final["escena_json"])
    else:
        acp_scene_request = ACPMessage(
            sender_agent="harness_orchestrator",
            target_agent="unreal_specialist_agent",
            task_type="CREATE_JSON",
            payload={
                "system_prompt": (
                    f"{prompt_final}\n"
                    f"Contexto del Editor: {contexto_completo}\n"
                    "Responde estrictamente con un JSON válido compatible con EscenaUnreal."
                ),
                "user_prompt": prompt_usuario
            }
        )

        acp_scene_answer = await acp_router.send_message(acp_scene_request)
        if acp_scene_answer.task_type == "ERROR":
            raise RuntimeError(f"Error en el agente especialista al generar la escena: {acp_scene_answer.payload}")

        llmJsonScene = acp_scene_answer.payload["JSON generado"]

        for intento in range(100):
            try:
                escena_validada = EscenaUnreal.model_validate(llmJsonScene)
                return escena_validada
            except (ValidationError, json.JSONDecodeError, KeyError) as err:
                print(f"-> [Harness] JSON mal generado (intento {intento + 1}/3), mandando a reparar...")
                llmJsonScene = await reparar_Json(
                    escenaJson=llmJsonScene,
                    err=err,
                    prompt_user=prompt_usuario
                )

        raise ValueError("No se pudo obtener un JSON válido tras 3 intentos de reparación.")


# ========================================
# 8. Bucle principal de la conversación
# ========================================

async def main():
    print("Iniciando orquestador...")

    if not USE_GUIDANCE:
        await iniciar_servidor_especialista()
        modelo_orq = await detectar_modelo_abierto(ORCHESTRATOR_BASE_URL, orchestrator_client)
        modelo_esp = await detectar_modelo_abierto(SPECIALIST_BASE_URL, specialist_llm_client)
        print(f"-> Orquestador detectado ({modelo_orq}) en {ORCHESTRATOR_BASE_URL}")
        print(f"-> Especialista detectado ({modelo_esp}) en {SPECIALIST_BASE_URL} (vía {SPECIALIST_AGENT_URL})")

    async with stdio_client(server_params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()

            tools_mcp = await session.list_tools()
            tools_info = [
                {
                    "name": tool.name,
                    "description": tool.description,
                    "inputSchema": tool.input_schema,
                }
                for tool in tools_mcp.tools
            ]

            resources_mcp = await session.list_resources()
            resources_info = [
                {
                    "uri": str(r.uri),
                    "name": r.name,
                    "description": r.description
                }
                for r in resources_mcp.resources
            ]

            print(f"-> Tools descubiertas: {[t['name'] for t in tools_info]}")
            print(f"-> Resources descubiertos: {[r['uri'] for r in resources_info]}")

            while True:
                pregunta = input("\n¿Qué escena o acción quieres realizar? ")
                if pregunta.lower() in ["salir", "exit", "quit"]:
                    break

                try:
                    resources_context = await leer_recursos_mcp(session, resources_info)

                    escena = await generar_escena_con_mcp(
                        pregunta,
                        session,
                        tools_info,
                        resources_context
                    )

                    nombre_archivo = "resultado.json"
                    with open(nombre_archivo, "w", encoding="utf-8") as f:
                        f.write(escena.model_dump_json(indent=2))

                    print(f"\nRespuesta final guardada con éxito en: {nombre_archivo}")

                except Exception as e:
                    print(f"Error procesando la solicitud: {e}")
                    traceback.print_exc()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except Exception as e:
        print("\n" + "=" * 50)
        print("DETALLE DEL ERROR:")
        print("=" * 50)
        traceback.print_exc()
        if hasattr(e, "exceptions"):
            for idx, inner_exc in enumerate(e.exceptions):
                print(f"\n--- Excepción interna {idx + 1} ---")
                traceback.print_exception(type(inner_exc), inner_exc, inner_exc.__traceback__)