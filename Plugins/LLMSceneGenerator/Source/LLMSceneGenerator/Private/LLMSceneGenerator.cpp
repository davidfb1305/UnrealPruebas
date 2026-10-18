// Copyright Epic Games, Inc. All Rights Reserved.

#include "LLMSceneGenerator.h"
#include "LLMSceneGeneratorStyle.h"
#include "LLMSceneGeneratorCommands.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"

// Inclusiones para la interfaz Slate y Editor
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Editor.h" 
#include "../Public/SceneParser.h" 

static const FName LLMSceneGeneratorTabName("LLMSceneGenerator");

#define LOCTEXT_NAMESPACE "FLLMSceneGeneratorModule"

void FLLMSceneGeneratorModule::StartupModule()
{
	FLLMSceneGeneratorStyle::Initialize();
	FLLMSceneGeneratorStyle::ReloadTextures();

	FLLMSceneGeneratorCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	// 1. VINCULAR LA ACCIÓN: Vinculamos el comando del plugin para que abra la ventana flotante
	PluginCommands->MapAction(
		FLLMSceneGeneratorCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FLLMSceneGeneratorModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLLMSceneGeneratorModule::RegisterMenus));

	// 2. REGISTRAR LA VENTANA: Cambiamos ETabSpawnerMenuType::Hidden a ETabSpawnerMenuType::Enabled 
	// para que sea visible oficialmente en el menú de Ventanas
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(LLMSceneGeneratorTabName, FOnSpawnTab::CreateRaw(this, &FLLMSceneGeneratorModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FLLMSceneGeneratorTabTitle", "LLM Scene Generator"))
		.SetMenuType(ETabSpawnerMenuType::Enabled);
}

void FLLMSceneGeneratorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FLLMSceneGeneratorStyle::Shutdown();
	FLLMSceneGeneratorCommands::Unregister();
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(LLMSceneGeneratorTabName);
}

// 3. INVOCAR LA VENTANA: Cuando se pulse el botón del plugin, forzamos a Unreal a abrir la pestaña en pantalla
void FLLMSceneGeneratorModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(LLMSceneGeneratorTabName);
}

// 4. DISEÑO DE LA VENTANA: Tu panel vertical con el campo de texto y el botón de parseo
TSharedRef<SDockTab> FLLMSceneGeneratorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10)
				[
					SNew(STextBlock)
						.Text(LOCTEXT("SceneNameLabel", "SCENE NAME:"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 0, 10, 10)
				[
					SNew(SEditableTextBox)
						.Text(FText::FromString(SceneNameInput))
						.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type CommitType) {
						SceneNameInput = InText.ToString();
							})
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10)
				[
					SNew(SButton)
						.Text(LOCTEXT("ExecuteParseButton", "Parse the scene to unreal"))
						.OnClicked_Lambda([this]() -> FReply {
						UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

						if (EditorWorld)
						{
							SceneParser::parseScene(SceneNameInput, EditorWorld);
						}
						else
						{
							FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("WorldErrorMsg", "ERROR: INVALID EDITORWORLD"));
						}

						return FReply::Handled();
							})
				]
		];
}

// 5. ENLAZAR EL BOTÓN VISUAL: Coloca el botón físico en la barra superior que disparará el comando
void FLLMSceneGeneratorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FLLMSceneGeneratorCommands::Get().PluginAction, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FLLMSceneGeneratorCommands::Get().PluginAction));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLLMSceneGeneratorModule, LLMSceneGenerator)
