// Copyright Epic Games, Inc. All Rights Reserved.
#include "LLMSceneGenerator.h"
#include "LLMSceneGeneratorStyle.h"
#include "LLMSceneGeneratorCommands.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"


#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h" 
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Editor.h" 
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "IPythonScriptPlugin.h" 
#include "SceneParser.h" 

static const FName LLMSceneGeneratorTabName("LLMSceneGenerator");

#define LOCTEXT_NAMESPACE "FLLMSceneGeneratorModule"


/// ---
/// MODULE
/// ---

void FLLMSceneGeneratorModule::StartupModule()
{
	FLLMSceneGeneratorStyle::Initialize();
	FLLMSceneGeneratorStyle::ReloadTextures();

	FLLMSceneGeneratorCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FLLMSceneGeneratorCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FLLMSceneGeneratorModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLLMSceneGeneratorModule::RegisterMenus));

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

void FLLMSceneGeneratorModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(LLMSceneGeneratorTabName);
	LoadDirConfig();
}

TSharedRef<SDockTab> FLLMSceneGeneratorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	if (RootDirectories.Num() == 0)
	{
		PopulateDirectoryTree();
	}
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SVerticalBox)
				//JSON FILENAME INPUT
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 10, 10, 2)
				[
					SNew(STextBlock).Text(LOCTEXT("SceneNameLabel", "SCENE NAME (JSON FILENAME):"))
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

			// ORIGINAL PARSE BUTTON
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 0, 10, 10)
				[
					SNew(SButton)
						.Text(LOCTEXT("ExecuteParseButton", "Parse the scene to unreal"))
						.OnClicked_Lambda([this]() -> FReply {
						//REMOVE IN THE FUTURE 
						GetSelectedDirectoriesContext();
						///
						UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
						if (EditorWorld) { SceneParser::parseScene(SceneNameInput, EditorWorld); }
						else { FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("WorldErrorMsg", "ERROR: INVALID EDITORWORLD")); }
						return FReply::Handled();
							})
				]
			//AI GENERATOR PROMPT INPUT
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 5, 10, 2)
				[
					SNew(STextBlock).Text(LOCTEXT("LLMPromptLabel", "LLM SCENE PROMPT (AI GENERATOR):"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 0, 10, 10)
				[
					SNew(SEditableTextBox)
						.HintText(LOCTEXT("LLMPromptHint", "None"))
						.Text(FText::FromString(LLMPromptInput))
						.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type CommitType) {
						LLMPromptInput = InText.ToString();
							})
				]


			// PYTHON AI GENERATION BUTTON
			+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 5, 10, 5)
				[
					SNew(SButton)
						.Text(LOCTEXT("ExecutePythonButton", "▶ Generate JSON via LLM (Python)"))
						.ButtonColorAndOpacity(FLinearColor(0.0f, 0.5f, 1.0f, 1.0f))
						.OnClicked_Lambda([this]() -> FReply {
						AppendLogToTerminal(TEXT("[INFO] Initializing Python subsystem..."));

						IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
						if (PythonPlugin)
						{

							FString ScriptPath = FPaths::ProjectDir() + "prueba.py";

							FString FullPrompt = GetSelectedDirectoriesContext() + LLMPromptInput;
							FString CleanedPrompt = FullPrompt.Replace(TEXT("'"), TEXT("\\'"));
							FString CleanedFileName = SceneNameInput.Replace(TEXT("'"), TEXT("\\'"));

							if (CleanedFileName.IsEmpty()) { CleanedFileName = TEXT("output_scene"); }

							// Step 1: Load and execute the Python file environment
							FString LoadCommand = FString::Printf(TEXT("exec(open('%s', encoding='utf-8').read())"), *ScriptPath);
							PythonPlugin->ExecPythonCommand(*LoadCommand);

							// Step 2: Invoke the specific target function passing both parameters
							FString ExecutionCommand = FString::Printf(TEXT("generate_scene('%s', '%s')"), *CleanedPrompt, *CleanedFileName);

							AppendLogToTerminal(TEXT("[INFO] Processing prompt using Guidance and Ministral-3B..."));
							bool bSuccess = PythonPlugin->ExecPythonCommand(*ExecutionCommand);

							if (bSuccess)
							{
								AppendLogToTerminal(TEXT("[SYSTEM] Script finished successfully. You can now press 'Parse the scene to unreal'."));
							}
							else
							{
								AppendLogToTerminal(TEXT("[ERROR] Execution failed inside the internal Python environment script."));
							}
						}
						else
						{
							AppendLogToTerminal(TEXT("[ERROR] PythonScriptPlugin dependency is not accessible."));
						}

						return FReply::Handled();
							})
				]

			//  TERMINAL DISPLAY WIDGET
			+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(10)
				[
					SAssignNew(TerminalWidget, SMultiLineEditableTextBox)
						.Text(FText::FromString(TerminalOutputText))
						.IsReadOnly(true)
						.BackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
						.ForegroundColor(FLinearColor(0.0f, 1.0f, 0.2f, 1.0f))
						.AutoWrapText(true)
				]

			// TREE DIR 
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 10, 10, 2)
				[
					SNew(STextBlock).Text(LOCTEXT("DirectoryTreeLabel", "SELECT DIRECTORIES TO INCLUDE:"))
				]
				+ SVerticalBox::Slot()
				.FillHeight(0.5f) 
				.Padding(10, 0, 10, 10)
				[
					SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(2.0f)
						[
							SAssignNew(DirectoryTreeView, STreeView<TSharedPtr<FDirectoryNode>>)
								.TreeItemsSource(&RootDirectories)
								.OnGenerateRow_Raw(this, &FLLMSceneGeneratorModule::OnGenerateDirectoryRow)
								.OnGetChildren_Raw(this, &FLLMSceneGeneratorModule::OnGetDirectoryChildren)
						]
				]
		];
	for (TSharedPtr<FDirectoryNode>& RootNode : RootDirectories)
	{
		DirectoryTreeView->SetItemExpansion(RootNode, true);
	}
	return Tab;
}

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

/// ---
/// LOG
/// ---

void FLLMSceneGeneratorModule::AppendLogToTerminal(const FString& NewLog)
{
	TerminalOutputText += NewLog + TEXT("\n");

	if (TerminalWidget.IsValid())
	{
		TSharedPtr<SMultiLineEditableTextBox> TextTerminal = StaticCastSharedPtr<SMultiLineEditableTextBox>(TerminalWidget);
		if (TextTerminal.IsValid())
		{
			TextTerminal->SetText(FText::FromString(TerminalOutputText));
			TextTerminal->ScrollTo(FTextLocation(TerminalOutputText.Len()));
		}
	}
}

/// ---
/// DIR CONFIG
/// ---

TSharedRef<ITableRow> FLLMSceneGeneratorModule::OnGenerateDirectoryRow(TSharedPtr<FDirectoryNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Enum to plain text
	FString TagText = Item->Tag == EFolderTag::Code ? TEXT("Code") :
		(Item->Tag == EFolderTag::Blueprint ? TEXT("Blueprint") :
			(Item->Tag == EFolderTag::Scene ? TEXT("Scene") : TEXT("Asset")));


	return SNew(STableRow<TSharedPtr<FDirectoryNode>>, OwnerTable)
		[
			SNew(SHorizontalBox)

				// Checkbox
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SCheckBox)
						.IsChecked_Lambda([this,Item]() { return Item->bIsSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState NewState) {
						Item->bIsSelected = (NewState == ECheckBoxState::Checked);
						SaveDirConfig();
							})
				]

			// DIR NAME
			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(5, 0)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(FText::FromString(Item->FolderName))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(5, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
						.HintText(FText::FromString("..."))
						.Text_Lambda([Item]() { return FText::FromString(Item->Description); })
						.OnTextChanged_Lambda([this, Item](const FText& NewText) {
						Item->Description = NewText.ToString();
						SaveDirConfig();
							})
				]

				// CHANGE TAG BTN
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
						.Text_Lambda([Item]() {
						if (Item->Tag == EFolderTag::Code) return FText::FromString("Code");
						if (Item->Tag == EFolderTag::Blueprint) return FText::FromString("Blueprint");
						if (Item->Tag == EFolderTag::Scene) return FText::FromString("Scene");
						return FText::FromString("Asset");
							})
						.OnClicked_Lambda([this, Item]() -> FReply {
						if (Item->Tag == EFolderTag::Code) Item->Tag = EFolderTag::Blueprint;
						else if (Item->Tag == EFolderTag::Blueprint) Item->Tag = EFolderTag::Asset;
						else if (Item->Tag == EFolderTag::Asset) Item->Tag = EFolderTag::Scene;
						else Item->Tag = EFolderTag::Code;
						SaveDirConfig();
						return FReply::Handled();
							})
				]
		];
}

void FLLMSceneGeneratorModule::OnGetDirectoryChildren(TSharedPtr<FDirectoryNode> Item, TArray<TSharedPtr<FDirectoryNode>>& OutChildren)
{
	if (Item.IsValid())
	{
		OutChildren = Item->Children;
	}
}

void FLLMSceneGeneratorModule::PopulateDirectoryTree()
{
	RootDirectories.Empty();
	IFileManager& FileManager = IFileManager::Get();

	// Reccursive search
	auto BuildTree = [&](const FString& RootPath, const FString& RootName, EFolderTag DefaultTag) -> TSharedPtr<FDirectoryNode>
		{
			TSharedPtr<FDirectoryNode> RootNode = MakeShared<FDirectoryNode>();
			RootNode->FolderName = RootName;
			RootNode->FolderPath = RootPath;
			RootNode->Tag = DefaultTag;
			RootNode->bIsDirectory = true;

			// Scan subdir
			auto ProcessDir = [&](FString CurrentPath, TSharedPtr<FDirectoryNode> ParentNode, auto& ProcessDirRef) -> void
				{
					TArray<FString> SubDirs;
					// Search only in dirs
					FileManager.FindFiles(SubDirs, *(CurrentPath / TEXT("*")), false, true);

					for (const FString& DirName : SubDirs)
					{
						FString FullPath = CurrentPath / DirName;

						TSharedPtr<FDirectoryNode> ChildNode = MakeShared<FDirectoryNode>();
						ChildNode->FolderName = DirName;
						ChildNode->FolderPath = FullPath;
						RootNode->bIsDirectory = true;

						if (DirName.Contains(TEXT("Blueprint")) || DirName.Contains(TEXT("BP_")))
						{
							ChildNode->Tag = EFolderTag::Blueprint;
						}
						else if (DirName.Contains(TEXT("Source")) || DirName.Contains(TEXT("Classes")))
						{
							ChildNode->Tag = EFolderTag::Code;
						}
						else if (DirName.Contains(TEXT("Maps")) || DirName.Contains(TEXT("Scenes")))
						{
							ChildNode->Tag = EFolderTag::Scene;
						}
						else
						{
							ChildNode->Tag = DefaultTag;
						}

						ParentNode->Children.Add(ChildNode);

						ProcessDirRef(FullPath, ChildNode, ProcessDirRef);
					}

					TArray<FString> FoundFiles;
					FileManager.FindFiles(FoundFiles, *(CurrentPath / TEXT("*.*")), true, false);

					for (const FString& FileName : FoundFiles)
					{	
						//Filter archive type
						if (FileName.EndsWith(TEXT(".uasset")) || FileName.EndsWith(TEXT(".umap")) ||
							FileName.EndsWith(TEXT(".cpp")) || FileName.EndsWith(TEXT(".h")))
						{
							TSharedPtr<FDirectoryNode> FileNode = MakeShared<FDirectoryNode>();
							FileNode->FolderName = FileName;
							FileNode->FolderPath = CurrentPath / FileName;
							FileNode->bIsDirectory = false;

							// Asignar Tag por defecto basado en extensión si se desea
							if (FileName.EndsWith(TEXT(".cpp")) || FileName.EndsWith(TEXT(".h"))) FileNode->Tag = EFolderTag::Code;
							else if (FileName.EndsWith(TEXT(".uasset"))) FileNode->Tag = EFolderTag::Blueprint;
							else if (FileName.EndsWith(TEXT(".umap"))) FileNode->Tag = EFolderTag::Scene;
							else FileNode->Tag = DefaultTag;

							ParentNode->Children.Add(FileNode);
						}
					}

				};

			ProcessDir(RootPath, RootNode, ProcessDir);
			return RootNode;
		};

	// Project dirs
	RootDirectories.Add(BuildTree(FPaths::ProjectContentDir(), TEXT("Project Content"), EFolderTag::Asset));
	RootDirectories.Add(BuildTree(FPaths::ProjectDir() / TEXT("Source"), TEXT("Project Source"), EFolderTag::Code));

	// Unreal Native dirs
	//RootDirectories.Add(BuildTree(FPaths::EngineContentDir(), TEXT("Engine Content"), EFolderTag::Asset));
}

FString FLLMSceneGeneratorModule::GetSelectedDirectoriesContext()
{
	FString SystemContext = TEXT("SYSTEM: La siguiente estructura de directorios está disponible para usar:\n");
	IFileManager& FileManager = IFileManager::Get();
	//use for avoiding duplicate files
	TSet<FString> ProcesedFiles; 
	// Look for all selected files/dirs
	auto ProcessNode = [&](TSharedPtr<FDirectoryNode> Node, auto& ProcessNodeRef) -> void
		{
			if (Node->bIsSelected)
			{
				FString TagStr = Node->Tag == EFolderTag::Code ? TEXT("[Code]") :
					(Node->Tag == EFolderTag::Blueprint ? TEXT("[Blueprint]") :
						(Node->Tag == EFolderTag::Scene ? TEXT("[Scene]") : TEXT("[Asset]")));

				if (Node->bIsSelected)
				{
					TagStr = Node->Tag == EFolderTag::Code ? TEXT("[Code]") :
						(Node->Tag == EFolderTag::Blueprint ? TEXT("[Blueprint]") : TEXT("[Asset]"));

					if (Node->bIsDirectory)
					{
						// Es una carpeta seleccionada completa
						SystemContext += FString::Printf(TEXT("\nCarpeta %s: %s\n"), *TagStr, *Node->FolderPath);

						if (!Node->Description.IsEmpty() && Node->Description != "...")
						{
							SystemContext += FString::Printf(TEXT("Descripción proporcionada: %s\n"), *Node->Description);
						}

						TArray<FString> FoundFiles;
						FileManager.FindFiles(FoundFiles, *(Node->FolderPath / TEXT("*.*")), true, false);
						for (const FString& FileName : FoundFiles)
						{
							FString FullFilePath = Node->FolderPath / FileName;
							SystemContext += FString::Printf(TEXT("  - %s\n"), *FileName);


							ProcesedFiles.Add(FullFilePath);
						}
					}
					else
					{
						//Direct files
						if (!ProcesedFiles.Contains(Node->FolderPath))
						{
							SystemContext += FString::Printf(TEXT("\nArchivo individual %s: %s\n"), *TagStr, *Node->FolderPath);
							if (!Node->Description.IsEmpty() && Node->Description != "...")
							{
								SystemContext += FString::Printf(TEXT("Descripción proporcionada: %s\n"), *Node->Description);
							}
							if (Node->Tag == EFolderTag::Scene) SystemContext += ParserUmapToJsonStream(Node->FolderPath);
							ProcesedFiles.Add(Node->FolderPath);
						}
					}
				}
			}
		
			for (TSharedPtr<FDirectoryNode> Child : Node->Children)
			{
				ProcessNodeRef(Child, ProcessNodeRef);
			}
		};

	for (TSharedPtr<FDirectoryNode> RootNode : RootDirectories)
	{
		ProcessNode(RootNode, ProcessNode);
	}
	AppendLogToTerminal(TEXT("[SYSTEM PROMPT]: "+ SystemContext));
	return SystemContext + TEXT("\nUSER PROMPT: ");
}

/// ---
/// Save/Load
/// ---

void FLLMSceneGeneratorModule::SaveDirConfig()
{
	FString SavePath = FPaths::ProjectSavedDir() / TEXT("LLMSceneGeneratorSettings.json");
	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();

	auto ProcessNode = [&](TSharedPtr<FDirectoryNode> Node, auto& ProcessNodeRef) -> void
		{
			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetBoolField(TEXT("bIsSelected"), Node->bIsSelected);
			NodeObj->SetBoolField(TEXT("bIsDirectory"), Node->bIsDirectory);
			NodeObj->SetStringField(TEXT("Description"), Node->Description);
			NodeObj->SetNumberField(TEXT("Tag"), (int32)Node->Tag);

			RootObject->SetObjectField(Node->FolderPath, NodeObj);

			for (TSharedPtr<FDirectoryNode> Child : Node->Children)
			{
				ProcessNodeRef(Child, ProcessNodeRef);
			}
		};

	for (TSharedPtr<FDirectoryNode> RootNode : RootDirectories)
	{
		ProcessNode(RootNode, ProcessNode);
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
	{
		FFileHelper::SaveStringToFile(OutputString, *SavePath);
	}
}

void FLLMSceneGeneratorModule::LoadDirConfig()
{
	FString SavePath = FPaths::ProjectSavedDir() / TEXT("LLMSceneGeneratorSettings.json");
	FString InputString;

	if (!FFileHelper::LoadFileToString(InputString, *SavePath)) return;

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InputString);

	if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
	{
		auto ProcessNode = [&](TSharedPtr<FDirectoryNode> Node, auto& ProcessNodeRef) -> void
			{
				if (RootObject->HasField(Node->FolderPath))
				{
					TSharedPtr<FJsonObject> NodeObj = RootObject->GetObjectField(Node->FolderPath);
					Node->bIsSelected = NodeObj->GetBoolField(TEXT("bIsSelected"));
					Node->bIsDirectory = NodeObj->GetBoolField(TEXT("bIsDirectory"));
					Node->Description = NodeObj->GetStringField(TEXT("Description"));
					Node->Tag = (EFolderTag)NodeObj->GetIntegerField(TEXT("Tag"));
				}

				for (TSharedPtr<FDirectoryNode> Child : Node->Children)
				{
					ProcessNodeRef(Child, ProcessNodeRef);
				}
			};

		for (TSharedPtr<FDirectoryNode> RootNode : RootDirectories)
		{
			ProcessNode(RootNode, ProcessNode);
		}
	}
}

/// ---
/// Parsers
/// ---

FString FLLMSceneGeneratorModule::ParserUmapToJsonStream(const FString& path)
{
	UE_LOG(LogTemp, Error, TEXT("Mellamo: %s"), *path);

	if (path.IsEmpty())
	{
		return TEXT("{}");
	}

	UPackage* MapPackage = LoadPackage(nullptr, *path, LOAD_None);
	if (!MapPackage)
	{
		UE_LOG(LogTemp, Error, TEXT("ERROR LOADING THE UMAP: %s"), *path);
		return TEXT("{}");
	}

	UWorld* worldObj = UWorld::FindWorldInPackage(MapPackage);
	if (!worldObj || !worldObj->PersistentLevel)
	{
		UE_LOG(LogTemp, Error, TEXT("ERROR: INVALID FORMAT."));
		return TEXT("{}");
	}

	TSharedPtr<FJsonObject> RootJsonObject = MakeShareable(new FJsonObject());
	TArray<TSharedPtr<FJsonValue>> ActoresArray;

	for (AActor* Actor : worldObj->PersistentLevel->Actors)
	{
		
		if (!Actor || Actor->IsTemplate()) continue;

		FString actorName = Actor->GetActorLabel();

		// Create json node
		TSharedPtr<FJsonObject> ActorJsonObject = MakeShareable(new FJsonObject());
		ActorJsonObject->SetStringField(TEXT("Name"), actorName);
		ActorJsonObject->SetStringField(TEXT("Class"), Actor->GetClass()->GetName());

		// Transform (Location, Rotation, Scale) ---
		TSharedPtr<FJsonObject> TransformObject = MakeShareable(new FJsonObject());

		// Pos
		FVector Loc = Actor->GetActorLocation();
		TArray<TSharedPtr<FJsonValue>> LocArray = { MakeShareable(new FJsonValueNumber(Loc.X)), MakeShareable(new FJsonValueNumber(Loc.Y)), MakeShareable(new FJsonValueNumber(Loc.Z)) };
		TransformObject->SetArrayField(TEXT("Location"), LocArray);

		// Rot 
		FRotator Rot = Actor->GetActorRotation();
		TArray<TSharedPtr<FJsonValue>> RotArray = { MakeShareable(new FJsonValueNumber(Rot.Roll)), MakeShareable(new FJsonValueNumber(Rot.Pitch)), MakeShareable(new FJsonValueNumber(Rot.Yaw)) };
		TransformObject->SetArrayField(TEXT("Rotation"), RotArray);

		// Scl
		FVector Scale = Actor->GetActorScale3D();
		TArray<TSharedPtr<FJsonValue>> ScaleArray = { MakeShareable(new FJsonValueNumber(Scale.X)), MakeShareable(new FJsonValueNumber(Scale.Y)), MakeShareable(new FJsonValueNumber(Scale.Z)) };
		TransformObject->SetArrayField(TEXT("Scale"), ScaleArray);

		ActorJsonObject->SetObjectField(TEXT("Transform"), TransformObject);

		// Add actor
		ActoresArray.Add(MakeShareable(new FJsonValueObject(ActorJsonObject)));
	}

	RootJsonObject->SetArrayField(TEXT("SceneActors"), ActoresArray);

	FString jsonText;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&jsonText);

	if (FJsonSerializer::Serialize(RootJsonObject.ToSharedRef(), Writer))
	{
		return jsonText;
	}

	return TEXT("{}");
}




#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLLMSceneGeneratorModule, LLMSceneGenerator)
