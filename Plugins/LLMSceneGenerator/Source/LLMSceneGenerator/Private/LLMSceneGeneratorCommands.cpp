// Copyright Epic Games, Inc. All Rights Reserved.

#include "LLMSceneGeneratorCommands.h"
#include "SceneParser.h"

#define LOCTEXT_NAMESPACE "FLLMSceneGeneratorModule"

void FLLMSceneGeneratorCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "LLMSceneGenerator", "Generate Scene", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
