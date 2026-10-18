// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "LLMSceneGeneratorStyle.h"

class FLLMSceneGeneratorCommands : public TCommands<FLLMSceneGeneratorCommands>
{
public:

	FLLMSceneGeneratorCommands()
		: TCommands<FLLMSceneGeneratorCommands>(TEXT("LLMSceneGenerator"), NSLOCTEXT("Contexts", "LLMSceneGenerator", "LLMSceneGenerator Plugin"), NAME_None, FLLMSceneGeneratorStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
