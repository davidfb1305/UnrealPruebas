// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Views/STreeView.h"

enum class EFolderTag
{
	Code,
	Blueprint,
	Asset,
	Scene
};

struct FDirectoryNode
{
	FString FolderPath;
	FString FolderName;
	FString Description;
	bool bIsSelected = false;
	bool bIsDirectory = true;
	EFolderTag Tag = EFolderTag::Asset;
	TArray<TSharedPtr<FDirectoryNode>> Children;
};

// Forward declarations to keep the header lightweight and prevent "identifier not found" compilation errors
class FUICommandList;
class SWidget;
class SEditableTextBox;


class FLLMSceneGeneratorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** This function will be bound to our command, and will show our tab */
	void PluginButtonClicked();

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

	// --- MEMBER VARIABLES ---
	FString SceneNameInput;
	FString LLMPromptInput;
	FString TerminalOutputText;

	// --- DIR TREE ---
	TArray<TSharedPtr<FDirectoryNode>> RootDirectories;
	TSharedPtr<STreeView<TSharedPtr<FDirectoryNode>>> DirectoryTreeView;

	// Using TSharedPtr<SWidget> here bypasses missing internal Slate types during header parsing
	TSharedPtr<SWidget> TerminalWidget;
	TSharedPtr<FUICommandList> PluginCommands;

	/// LOG METHODS

	/// <summary>
	/// Send a text to the addon log
	/// </summary>
	/// <param name="NewLog"></param>
	void AppendLogToTerminal(const FString& NewLog);


	/// DIR METHODS

	/// <summary>
	/// Difine the visual format of the dir list in the widget
	/// </summary>
	/// <param name="Item"></param>
	/// <param name="OwnerTable"></param>
	/// <returns></returns>
	TSharedRef<ITableRow> OnGenerateDirectoryRow(TSharedPtr<FDirectoryNode> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/// <summary>
	///  Required method for Slate
	/// </summary>
	/// <param name="Item"></param>
	/// <param name="OutChildren"></param>
	void OnGetDirectoryChildren(TSharedPtr<FDirectoryNode> Item, TArray<TSharedPtr<FDirectoryNode>>& OutChildren);

	/// <summary>
	/// Scan the project directories.
	/// </summary>
	void PopulateDirectoryTree();

	/// <summary>
	/// Parse the dirList to a plain text.
	/// </summary>
	/// <returns>return the dirList in plain text</returns>
	FString GetSelectedDirectoriesContext();

	/// <summary>
	/// Save the dirConfig in a .json file. (path: FPaths::ProjectSavedDir() / TEXT("LLMSceneGeneratorSettings.json"))
	/// </summary>
	/// 
	void SaveDirConfig();
	/// <summary>
	/// Load the dirConfig int the module startup
	/// </summary>
	void LoadDirConfig();

	/// <summary>
	/// Parser a umap from disc to a json format in a string
	/// </summary>
	/// <param name="path">umap path</param>
	/// <returns></returns>
	FString ParserUmapToJsonStream(const FString& path);
};
