// Copyright Natali Caggiano. All Rights Reserved.

#include "UnrealAICLIModule.h"
#include "UnrealAICLICommands.h"
#include "ClaudeEditorWidget.h"
#include "ClaudeCodeRunner.h"
#include "ClaudeSubsystem.h"
#include "ScriptExecutionManager.h"
#include "MCP/UnrealAICLIMCPServer.h"
#include "ProjectContext.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HttpServerModule.h"

DEFINE_LOG_CATEGORY(LogUnrealAICLI);

#define LOCTEXT_NAMESPACE "FUnrealAICLIModule"

static const FName ClaudeTabName("ClaudeAssistant");

void FUnrealAICLIModule::StartupModule()
{
	UE_LOG(LogUnrealAICLI, Warning, TEXT("=== UnrealAICLI BUILD 20260107-1450 THREAD_TESTS_DISABLED ==="));

	const bool bMCPOnlyMode = FClaudeCodeRunner::IsMCPOnlyMode(); // ProviderMode/legacy bMCPOnlyMode resolved in FClaudeCodeRunner

	if (!bMCPOnlyMode)
	{
		// Register commands
		FUnrealAICLICommands::Register();

		PluginCommands = MakeShared<FUICommandList>();
		bClaudeUIEnabled = true;

		// Map commands to actions
		PluginCommands->MapAction(
			FUnrealAICLICommands::Get().OpenClaudePanel,
			FExecuteAction::CreateLambda([]()
			{
				FGlobalTabmanager::Get()->TryInvokeTab(ClaudeTabName);
			}),
			FCanExecuteAction()
		);

		// Map QuickAsk command - shows a popup for quick questions
		PluginCommands->MapAction(
			FUnrealAICLICommands::Get().QuickAsk,
			FExecuteAction::CreateLambda([]()
			{
				// Create a simple input dialog
				TSharedRef<SWindow> QuickAskWindow = SNew(SWindow)
					.Title(LOCTEXT("QuickAskTitle", "Quick Ask Provider"))
					.ClientSize(FVector2D(500, 100))
					.SupportsMinimize(false)
					.SupportsMaximize(false);

				TSharedPtr<SEditableTextBox> InputBox;

				QuickAskWindow->SetContent(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.Padding(10)
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("QuickAskLabel", "Ask a quick question:"))
					]
					+ SVerticalBox::Slot()
					.Padding(10, 0, 10, 10)
					.FillHeight(1.0f)
					[
						SAssignNew(InputBox, SEditableTextBox)
						.HintText(LOCTEXT("QuickAskHint", "Type your question here..."))
						.OnTextCommitted_Lambda([QuickAskWindow](const FText& Text, ETextCommit::Type CommitType)
						{
							if (CommitType == ETextCommit::OnEnter && !Text.IsEmpty())
							{
								QuickAskWindow->RequestDestroyWindow();

								FString Prompt = Text.ToString();
								FClaudePromptOptions Options;
								Options.bIncludeEngineContext = true;
								Options.bIncludeProjectContext = true;
								FClaudeCodeSubsystem::Get().SendPrompt(
									Prompt,
									FOnClaudeResponse::CreateLambda([](const FString& Response, bool bSuccess)
									{
										FNotificationInfo Info(FText::FromString(
											bSuccess
												? (Response.Len() > 300 ? Response.Left(300) + TEXT("...") : Response)
												: TEXT("Error: ") + Response));
										Info.ExpireDuration = bSuccess ? 15.0f : 5.0f;
										Info.bUseLargeFont = false;
										Info.bUseSuccessFailIcons = true;
										FSlateNotificationManager::Get().AddNotification(Info);
									}),
									Options
								);
							}
						})
					]
				);

				FSlateApplication::Get().AddWindow(QuickAskWindow);
				if (InputBox.IsValid())
				{
					FSlateApplication::Get().SetKeyboardFocus(InputBox);
				}
			}),
			FCanExecuteAction::CreateLambda([]()
			{
				return FClaudeCodeRunner::IsConfiguredProviderAvailable();
			})
		);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			ClaudeTabName,
			FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args) -> TSharedRef<SDockTab>
			{
				return SNew(SDockTab)
					.TabRole(ETabRole::NomadTab)
					.Label(LOCTEXT("ClaudeTabTitle", "AI Assistant"))
					[
						SNew(SClaudeEditorWidget)
					];
			}))
			.SetDisplayName(LOCTEXT("ClaudeTabTitle", "AI Assistant"))
			.SetTooltipText(LOCTEXT("ClaudeTabTooltip", "Open AI Assistant for Unreal development"))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help"));

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealAICLIModule::RegisterMenus));

		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetGlobalLevelEditorActions()->Append(PluginCommands.ToSharedRef());

		if (FClaudeCodeRunner::IsConfiguredProviderAvailable())
		{
			UE_LOG(LogUnrealAICLI, Log, TEXT("%s CLI found"), *FClaudeCodeRunner::GetConfiguredProviderName());
		}
		else
		{
			UE_LOG(LogUnrealAICLI, Warning, TEXT("%s CLI not found. Install with: %s"),
				*FClaudeCodeRunner::GetConfiguredProviderName(),
				*FClaudeCodeRunner::GetConfiguredProviderInstallHint());
		}
	}
	else
	{
		UE_LOG(LogUnrealAICLI, Log, TEXT("ProviderMode=MCPOnly: chat UI disabled, only MCP server is started.")); // Explicit MCP-only startup path
		bClaudeUIEnabled = false;
	}

	StartMCPServer();
	FProjectContextManager::Get().RefreshContext();
	FScriptExecutionManager::Get();
}

void FUnrealAICLIModule::ShutdownModule()
{
	UE_LOG(LogUnrealAICLI, Log, TEXT("UnrealAICLI module shutting down"));

	StopMCPServer();

	if (bClaudeUIEnabled)
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		FUnrealAICLICommands::Unregister();
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ClaudeTabName);
		bClaudeUIEnabled = false;
	}
}

FUnrealAICLIModule& FUnrealAICLIModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUnrealAICLIModule>("UnrealAICLI");
}

bool FUnrealAICLIModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("UnrealAICLI");
}

void FUnrealAICLIModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);
	
	// Add to the main menu bar under Tools
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->FindOrAddSection("UnrealAICLI");

		Section.AddMenuEntryWithCommandList(
			FUnrealAICLICommands::Get().OpenClaudePanel,
			PluginCommands,
			LOCTEXT("OpenClaudeMenuItem", "AI Assistant"),
			LOCTEXT("OpenClaudeMenuItemTooltip", "Open AI Assistant for UE5.7 help (Ctrl+Shift+C)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
		);

		Section.AddMenuEntryWithCommandList(
			FUnrealAICLICommands::Get().QuickAsk,
			PluginCommands,
			LOCTEXT("QuickAskMenuItem", "Quick Ask AI"),
			LOCTEXT("QuickAskMenuItemTooltip", "Quickly ask a question (Ctrl+Alt+C)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
		);
	}
	
	// Add to the toolbar
	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("UnrealAICLI");
		
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			FUnrealAICLICommands::Get().OpenClaudePanel,
			LOCTEXT("ClaudeToolbarButton", "AI"),
			LOCTEXT("ClaudeToolbarTooltip", "Open AI Assistant (Ctrl+Shift+C)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
		));
	}
}

void FUnrealAICLIModule::UnregisterMenus()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FUnrealAICLIModule::StartMCPServer()
{
	if (MCPServer.IsValid())
	{
		UE_LOG(LogUnrealAICLI, Warning, TEXT("MCP Server already exists"));
		return;
	}

	MCPServer = MakeShared<FUnrealAICLIMCPServer>();

	if (!MCPServer->Start(GetMCPServerPort()))
	{
		UE_LOG(LogUnrealAICLI, Error, TEXT("Failed to start MCP Server on port %d"), GetMCPServerPort());
		MCPServer.Reset();
	}
}

void FUnrealAICLIModule::StopMCPServer()
{
	if (MCPServer.IsValid())
	{
		MCPServer->Stop();
		MCPServer.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUnrealAICLIModule, UnrealAICLI)



