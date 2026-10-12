// Fill out your copyright notice in the Description page of Project Settings.



#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"

#include "SceneParser.h"



SceneParser::SceneParser()
{
}

bool SceneParser::parseScene(const FString& path)
{
	FString JsonString;

	// 1. Leer el archivo JSON desde la ruta proporcionada
	if (!FFileHelper::LoadFileToString(JsonString, *path))
	{
		UE_LOG(LogTemp, Error, TEXT("SceneParser: No se pudo abrir el archivo en la ruta: %s"), *path);
		return false;
	}

	FJSScene scene;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonString, &scene, 0, 0))
	{
		UE_LOG(LogTemp, Error, TEXT("SceneParser: Error al parsear el contenido JSON."));
		return false;
	}

	for (const FJSActor& ActorData : scene.actors)
	{
		UE_LOG(LogTemp, Log, TEXT("Actor detectado en JSON -> ID: %s, Clase: %s"), *ActorData.actorID, *ActorData.actorClass);
	}

	return true;

	return false;
}

SceneParser::~SceneParser()
{
}
