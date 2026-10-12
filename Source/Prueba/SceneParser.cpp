// Fill out your copyright notice in the Description page of Project Settings.



#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"
#include "Engine/World.h"

// Actors and components
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"


#include "SceneParser.h"



SceneParser::SceneParser()
{
}



SceneParser::~SceneParser()
{
}

bool SceneParser::parseScene(const FString& fileName, UWorld* World)
{
	FString path = FPaths::ProjectDir() + fileName;
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("SceneParser: WORLD POINTER == NULLPTR."));
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *path))
	{
		UE_LOG(LogTemp, Error, TEXT("SceneParser: FAIL LOADING %s"), *path);
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TSharedPtr<FJsonObject> RootJsonObject;

	if (!FJsonSerializer::Deserialize(Reader, RootJsonObject) || !RootJsonObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("SceneParser: ERROR TRYING TO DESERIALIZE THE JSON"));
		return false;
	}

	// Procesar la lista de actores directamente
	const TArray<TSharedPtr<FJsonValue>>* ActorsArray;
	if (RootJsonObject->TryGetArrayField(TEXT("actors"), ActorsArray))
	{
		for (const TSharedPtr<FJsonValue>& ActorValue : *ActorsArray)
		{
			TSharedPtr<FJsonObject> ActorObj = ActorValue->AsObject();
			if (!ActorObj.IsValid()) continue;

			///====================
			///ACTOR NAME AND CLASS
			///====================


			FString ActorID, ActorClass;
			ActorObj->TryGetStringField(TEXT("actor_id"), ActorID);
			ActorObj->TryGetStringField(TEXT("actor_class"), ActorClass);


			///==========
			///TRANSFORM
			///==========

			FVector Position = FVector::ZeroVector;
			FRotator Rotation = FRotator::ZeroRotator;
			FVector Scale = FVector::OneVector;

			TSharedPtr<FJsonObject> TransformObj = ActorObj->GetObjectField(TEXT("transform"));
			if (TransformObj.IsValid())
			{
				TSharedPtr<FJsonObject> PosObj = TransformObj->GetObjectField(TEXT("position"));
				if (PosObj.IsValid())
				{
					Position.X = PosObj->GetNumberField(TEXT("x"));
					Position.Y = PosObj->GetNumberField(TEXT("y"));
					Position.Z = PosObj->GetNumberField(TEXT("z"));
				}
				TSharedPtr<FJsonObject> RotObj = TransformObj->GetObjectField(TEXT("rotation"));
				if (RotObj.IsValid())
				{
					Rotation.Roll = RotObj->GetNumberField(TEXT("x"));
					Rotation.Pitch = RotObj->GetNumberField(TEXT("y"));
					Rotation.Yaw = RotObj->GetNumberField(TEXT("z"));
				}
				TSharedPtr<FJsonObject> ScaleObj = TransformObj->GetObjectField(TEXT("scale"));
				if (ScaleObj.IsValid())
				{
					Scale.X = ScaleObj->GetNumberField(TEXT("x"));
					Scale.Y = ScaleObj->GetNumberField(TEXT("y"));
					Scale.Z = ScaleObj->GetNumberField(TEXT("z"));
				}
			}

			FTransform SpawnTransform(Rotation, Position, Scale);
			FActorSpawnParameters SpawnParams;
			SpawnParams.Name = FName(*ActorID);
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			TSharedPtr<FJsonObject> PropertiesObj = ActorObj->GetObjectField(TEXT("properties"));


			///==========
			///MESH ACTOR
			///==========
			
			if (ActorClass.Equals(TEXT("MeshActor"), ESearchCase::IgnoreCase))
			{
				// Spawn a new meshActor
				AStaticMeshActor* NewMeshActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), SpawnTransform, SpawnParams);
				if (NewMeshActor && PropertiesObj.IsValid())
				{
					NewMeshActor->SetActorLabel(*ActorID);
					UStaticMeshComponent* MeshComp = NewMeshActor->GetStaticMeshComponent();
					TSharedPtr<FJsonObject> MeshDataObj = PropertiesObj->GetObjectField(TEXT("mesh"));

					if (MeshComp && MeshDataObj.IsValid())
					{
						FString AssetPath;
						MeshDataObj->TryGetStringField(TEXT("asset_path"), AssetPath);

						// Load the mesh from the path
						UStaticMesh* LoadedMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *AssetPath));
						if (LoadedMesh)
						{
							MeshComp->SetStaticMesh(LoadedMesh);
						}
						else
						{
							UE_LOG(LogTemp, Warning, TEXT("FAILED LOADING: %s"), *AssetPath);
							// Using the cube mesh by default
							UStaticMesh* EngineCube = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
							if (EngineCube) MeshComp->SetStaticMesh(EngineCube);
						}
					}
				}
			}

			///============
			///CAMERA ACTOR
			///============

			else if (ActorClass.Equals(TEXT("CameraActor"), ESearchCase::IgnoreCase))
			{
				// Spawn a new CameraActor
				ACameraActor* NewCameraActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), SpawnTransform, SpawnParams);
				if (NewCameraActor && PropertiesObj.IsValid())
				{
					NewCameraActor->SetActorLabel(*ActorID);
					UCameraComponent* CameraComp = NewCameraActor->GetCameraComponent();
					if (CameraComp)
					{
						double FOV = 60.0;
						PropertiesObj->TryGetNumberField(TEXT("fov"), FOV);
						CameraComp->SetFieldOfView(FOV);

						double NearPlane = 0.1;
						PropertiesObj->TryGetNumberField(TEXT("near_plane"), NearPlane);
						

					}
					
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("SceneParser: UNKNOWN ACTOR CLASS: '%s'"), *ActorClass);
			}
		}
	}

	return true;
}