// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "string.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SceneParser.generated.h"
/**
 *
 */

USTRUCT(BlueprintType)
struct FJSActor
{
	GENERATED_BODY()

	UPROPERTY()
	FString actor_id;

	UPROPERTY()
	FString actor_class;

	UPROPERTY()
	FVector postition;

	UPROPERTY()
	FRotator rotation;

	UPROPERTY()
	FVector scale;
};

USTRUCT(BlueprintType)
struct FJSScene
{
	GENERATED_BODY()

	UPROPERTY()
	FString scene_name;

	UPROPERTY()
	FString unreal_version;

	UPROPERTY()
	FString timestamp;

	UPROPERTY()
	TArray<FJSActor> actors;
};


class PRUEBA_API SceneParser
{
public:
	SceneParser();
	static bool parseScene(const FString& fileName, UWorld* World);
	~SceneParser();
};
