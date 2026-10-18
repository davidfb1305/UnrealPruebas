import asyncio
import json
from typing import Any, Dict, List, Optional
import guidance
import os
import traceback
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from pydantic import BaseModel, Field

#============================================
# 1. Clases de contexto de Unreal para la IA
#============================================

class Vector3(BaseModel):
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

class Rotation(BaseModel):
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

class Transform(BaseModel):
    position: Vector3 = Field(default_factory=Vector3)
    rotation: Rotation = Field(default_factory=Rotation)
    scale: Vector3 = Field(default_factory=lambda: Vector3(x=1.0, y=1.0, z=1.0))

class ActorData(BaseModel):
    actor_id: str = Field(description="Nombre o ID único del actor")
    actor_class: str = Field(description="Ruta de la clase en Unreal")
    transform: Transform
    properties: Optional[Dict[str, Any]] = Field(default=None)

class EscenaUnreal(BaseModel):
    scene_name: str = Field(description="Nombre del nivel o escena")
    unreal_version: str = Field(default="5.4")
    timestamp: Optional[str] = Field(default=None)
    actors: List[ActorData] = Field(description="Lista de actores de la escena")

# Modelo para decidir si se necesita llamar a una herramienta MCP en el paso 1
class DecisiónHerramienta(BaseModel):
    requiere_mcp: bool = Field(description="True si necesita consultar algo al MCP antes de generar la escena")
    tool_name: Optional[str] = Field(default=None, description="Nombre de la herramienta MCP a ejecutar")
    arguments: Optional[Dict[str, Any]] = Field(default=None, description="Parámetros para la herramienta")

#=========================================
# 2. Configuración del llamafile y el MCP
#=========================================

llm = guidance.models.OpenAI(
    model="qwen",
    base_url="http://localhost:8080/v1",
    api_key="sk-no-key-required",
)

MCP_SERVER_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "server.py")) #IMPORTANTE SOLO FUNCIONA SI ESTÁN EN LA MISMA CARPETA, EN CASO DE CAMBIARLO HAY QUE PASAR LA RUTA ABSOLUTA AQUÍ

server_params = StdioServerParameters(
    command="uv",
    args=["run", "--with", "mcp[cli]", "mcp", "run", MCP_SERVER_PATH],
    env=None,
)

#=======================================
# 3. Lectura de @mcp resources del MCP
#=======================================

async def leer_recursos_mcp(session_mcp: ClientSession, resources_info: List[Dict[str, Any]]) -> str:
    """Lee el contenido textual de los recursos expuestos por el MCP para usarlos como contexto."""
    contexto_recursos = ""
    for res in resources_info:
        try:
            resource_data = await session_mcp.read_resource(res["uri"])
            contenido = resource_data.contents[0].text
            contexto_recursos += f"\n[Recurso MCP ({res['uri']})]: {contenido}"
        except Exception as e:
            print(f"Advertencia: No se pudo leer el recurso {res['uri']}: {e}")
    return contexto_recursos

#=============================================================
# 4. Generación de escena de Unreal en JSON con consulta MCP
#=============================================================

async def generar_escena_con_mcp(
    prompt_usuario: str, 
    session_mcp: ClientSession, 
    tools_info: List[Dict[str, Any]],
    resources_context: str
) -> EscenaUnreal:
    
    #================================================================================================================================
    # 4.1. Consultamos si se requiere la intervención del MCP, en caso de ser positivo se añade a la respuesta el resultado del MCP
    #================================================================================================================================
    
    #Seteo del contexto para el llamafile, consulta las herramientas disponibles en el MCP y si alguna se requiere con lo que se ha pedido entonces se la llama
    prompt_evaluacion = (
        "Eres un asistente de Unreal Engine. Analiza la petición del usuario junto con el contexto actual "
        "y determina si necesitas llamar a alguna herramienta MCP para consultar o modificar algo previo:\n"
        f"Herramientas MCP disponibles: {json.dumps(tools_info, ensure_ascii=False)}\n"
        f"Contexto de Resources activos: {resources_context}"
    )

    with guidance.system():
        lm = llm + prompt_evaluacion

    with guidance.user():
        lm += prompt_usuario

    with guidance.assistant():
        lm += guidance.json(name="decision", schema=DecisiónHerramienta)

    decision = DecisiónHerramienta.model_validate_json(lm["decision"])

    mcp_context = ""

    # Si el LLM decidió consultar el MCP 
    if decision.requiere_mcp and decision.tool_name:
        print(f"\n[MCP] Ejecutando herramienta previa: '{decision.tool_name}' con args: {decision.arguments}")
        
        # Ejecución real en el MCP
        mcp_response = await session_mcp.call_tool(
            decision.tool_name, 
            arguments=decision.arguments or {}
        )
        
        # Extracción limpia del texto devuelto por el MCP
        if mcp_response.content and hasattr(mcp_response.content[0], "text"):
            contenido_texto = mcp_response.content[0].text
        else:
            contenido_texto = str(mcp_response.content)
        
        mcp_context = f"\n[Datos obtenidos de Unreal vía MCP ({decision.tool_name})]: {contenido_texto}"
        print(f"[MCP] Datos recibidos: {mcp_context}")
        

    #=============================================================
    # 4.2. Generación del JSON en el formato pedido vía guidance
    #=============================================================
    
    #Aquí se setea aún más contexto para la generación
    prompt_final = (
        "Eres un diseñador de niveles experto de Unreal Engine. Genera la escena final en formato JSON. "
        "Utiliza la información proporcionada por el usuario y los datos reales obtenidos de Unreal si existen."
    )
    
    #Como se permite la obtención de recursos por el MCP es necesario agregar ese contexto al resultado final del prompt
    contexto_completo = f"{resources_context}\n{mcp_context}"
    
    with guidance.system():
        lm_final = llm + prompt_final + f"\nContexto del Editor: {contexto_completo}" #Metemos el contexto

    with guidance.user():
        lm_final += prompt_usuario  #Metemos el prompt usuario

    with guidance.assistant():
        lm_final += guidance.json(name="escena_json", schema=EscenaUnreal) #Metemos la escena en formato JSON válido

    return EscenaUnreal.model_validate_json(lm_final["escena_json"])


#========================================
# 5. Bucle principal de la conversación
#========================================
async def main():
    print("Iniciando orquestador MCP + Guidance + Llamafile...")

    #COMO AHORA TENGO QUE CONSULTAR COSAS CON EL MCP DEBO HACER UN HILO ASÍNCRONO EN EL MAIN PARA LA CONVERSACIÓN
    async with stdio_client(server_params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            
            #====================================================================================================
            # 5.1 Esta parte se encarga de la carga de recursos gracias a las macros de @mcp del script de MCP
            #====================================================================================================
            
            # Carga de tools @mcp tools
            tools_mcp = await session.list_tools()
            tools_info = [
                {
                    "name": tool.name,
                    "description": tool.description,
                    "inputSchema": tool.input_schema,
                }
                for tool in tools_mcp.tools
            ]
            
            #Carga de Resources (@mcp.resource)
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

            #=======================================================================================================
            # 5.2 Parte del bucle de conversación y llamada a los métodos del apartado 4 en función de lo necesario
            #=======================================================================================================
            
            while True:
                pregunta = input("\n¿Qué escena o acción quieres realizar? ")
                if pregunta.lower() in ["salir", "exit", "quit"]:
                    break

                try:
                    # Leemos el contenido actualizado de todos los recursos antes de procesar
                    resources_context = await leer_recursos_mcp(session, resources_info)

                    # Generamos la escena pasando tools y el contexto de recursos
                    escena = await generar_escena_con_mcp(
                        pregunta, 
                        session, 
                        tools_info, 
                        resources_context
                    )

                    # Guardar archivo json en formato correcto
                    nombre_archivo = "resultado.json"
                    with open(nombre_archivo, "w", encoding="utf-8") as f:
                        f.write(escena.model_dump_json(indent=2))

                    print(f"\nRespuesta final guardada con éxito en: {nombre_archivo}")

                except Exception as e:
                    print(f"Error procesando la solicitud: {e}")
                    
                    
#=======================================================================
# 6. Permite crear hilos diferentes y trabajar en simultáneo con el MCP          
#=======================================================================

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