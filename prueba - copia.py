import guidance
import torch
from pydantic import BaseModel, Field
from typing import List, Optional, Dict, Any

# Seteamos los componentes básicos de los actores de Unreal para facilitar el trabajo de comprensión al llamafile
class Vector3(BaseModel):
    x: float = 0.0;
    y: float = 0.0;
    z: float = 0.0;
class Rotation(BaseModel):
    x: float = 0.0;
    y: float = 0.0;
    z: float = 0.0;
class Transform(BaseModel):
    position: Vector3 = Field(Vector3(x=0.0,y=0.0,z=0.0)); #Field se usa para setear una descripción o formato default del atributo
    rotation: Rotation =  Field(Rotation(x=0.0,y=0.0,z=0.0));
    scale: Vector3= Field(default_factory=lambda: Vector3(x=1.0, y=1.0, z=1.0)); #Seteamos los valores a 1 para que este en tamaño normal

#Que define a un actor en Unreal: ID,Clase,Transform y Propiedades, ESTO ES AMPLIABLE 
class ActorData(BaseModel):
    actor_id: str = Field(description="Nombre o ID único del actor, ej: BP_PlayerSpawn_C_1")
    actor_class: str = Field(description="Ruta de la clase en Unreal, ej: /Script/Engine.StaticMeshActor o una ruta de Blueprint")
    transform: Transform
    properties: Optional[Dict[str, Any]] = Field(default=None, description="Propiedades específicas como StaticMesh, Intensity, Mobility, etc.")

# Definición de la estructura del JSON que deseamos, en este caso una escena de Unreal con este formato : formato que queremos que lo devuelva
class EscenaUnreal(BaseModel):
    scene_name: str = Field(description="Nombre del nivel o escena")
    unreal_version: str = Field(default="5.4", description="Versión de Unreal Engine objetivo")
    timestamp: Optional[str] = Field(default=None, description="Marca de tiempo ISO 8601")
    actors: List[ActorData] = Field(description="Lista de todos los actores que componen la escena")
    
# Conexión al servidor Llamafile
#Usamos el modelo OpenaAI de guidance en lugar del OpenaAI normal
llm = guidance.models.OpenAI(
    model="qwen", #Modelo del Llamaifle
    base_url="http://localhost:8080/v1", #Puerto ubicado
    api_key="sk-no-key-required" #Si requiere alguna clave de activación
)

#Si no usasemos Guidance.OpenAI y solo usasemos OpenAi
#client = OpenAI(
 #   base_url="http://localhost:8080/v1",
  #  api_key="sk-no-key-required"
#)


 #Bucles de comprobación y contexto entre el Llamafile y Guidance
def consultar_llamafile(prompt_usuario: str) -> EscenaUnreal: #Pasamos al método el formato de Json que queremos y el modelo cargado con Guidance OpenAI en la clase EscenaUnreal
    
    """Envía la pregunta a Llamafile y fuerza la salida JSON con Guidance."""
    
    # Construimos la conversación usando los context managers de guidance
    with guidance.system():
        lm = llm + "Eres un diseñador de niveles experto de Unreal Engine y tu trabajo es generar una escena en el formato de JSON dado que cumpla con los requisitos del usuario."
        
    with guidance.user():
        lm += prompt_usuario
        
    with guidance.assistant():
        # Forzamos la estructura basada en la clase Pydantic y guardamos el resultado en la variable "respuesta_generada"
        lm += guidance.json(name="respuesta_generada", schema=EscenaUnreal)
    
    # Guardamos el string JSON generado de forma segura
    json_raw = lm["respuesta_generada"]

    # Variable que le indica cuanto puede desvariar del tema para evitar que sea tan    
    temperature = 0.2
    # Validamos e instanciamos el objeto Pydantic
    return EscenaUnreal.model_validate_json(json_raw)

# Bucle de conversación con mi archivo llama 
if __name__ == "__main__":
    print("Starting conversation with the model, write exit to finish conversetion")
    
    while True:
        pregunta = input("\n¿Qué escena quieres generar? ")
        if pregunta.lower() in ["salir", "exit", "quit"]:
            break
            
        try:
            # Llamada al modelo
            resultado = consultar_llamafile(pregunta)
            
            # Mostrar resultado JSON y guardarlo
            nombre_archivo = "resultado.json"

            with open(nombre_archivo, "w", encoding="utf-8") as f:
                f.write(resultado.model_dump_json(indent=2))

            print(f"\nRespuesta JSON guardada con éxito en: {nombre_archivo}")
            
        except Exception as e:
            print(f"Error procesando la respuesta: {e}")