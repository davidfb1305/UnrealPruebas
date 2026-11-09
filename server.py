import json
# En MCP 2.x se importa MCPServer en lugar de FastMCP
from mcp.server.mcpserver import MCPServer

# 1. Inicializar el servidor MCP con MCPServer
mcp = MCPServer("UnrealEngineHelper")

# Asignación de recompensa por leer :D
@mcp.resoucrce("El recurso esta aquí (path al recurso)")
def get_mesh_for_charachter(personaje : str) ->str:
    switch(personaje)
        case Barbaro:
            malla = carpeta recurso malla Barbaro
        case mago:
            
            
    return malla;
# Creación de las herramientas y recursos que se leen para el Llamafile
@mcp.resource("unreal://camera/transform")
def get_camera_transform_resource() -> str:
    camera_data = {
        "position": {"x": 120.0, "y": -450.5, "z": 85.0},
        "rotation": {"x": 0.0, "y": -15.0, "z": 90.0},
        "focal_length": 35.0
    }
    return json.dumps(camera_data)


@mcp.tool()
def spawn_actor(actor_class: str, location_x: float, location_y: float, location_z: float) -> str:
    response = {
        "status": "success",
        "message": f"Actor '{actor_class}' creado correctamente",
        "location": {"x": location_x, "y": location_y, "z": location_z}
    }
    return json.dumps(response)


if __name__ == "__main__":
    mcp.run(transport="stdio")