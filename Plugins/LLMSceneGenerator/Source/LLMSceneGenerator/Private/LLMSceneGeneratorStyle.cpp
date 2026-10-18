// Copyright Epic Games, Inc. All Rights Reserved.

#include "LLMSceneGeneratorStyle.h"
#include "LLMSceneGenerator.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FLLMSceneGeneratorStyle::StyleInstance = nullptr;

void FLLMSceneGeneratorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FLLMSceneGeneratorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FLLMSceneGeneratorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("LLMSceneGeneratorStyle"));
	return StyleSetName;
}


const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FLLMSceneGeneratorStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("LLMSceneGeneratorStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("LLMSceneGenerator")->GetBaseDir() / TEXT("Resources"));

	Style->Set("LLMSceneGenerator.PluginAction", new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));
	return Style;
}

void FLLMSceneGeneratorStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FLLMSceneGeneratorStyle::Get()
{
	return *StyleInstance;
}
