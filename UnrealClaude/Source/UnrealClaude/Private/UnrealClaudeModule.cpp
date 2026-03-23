// Copyright Natali Caggiano. All Rights Reserved.

#include "UnrealClaudeModule.h"
#include "UnrealAICLICommands.h"
#include "ClaudeEditorWidget.h"
#include "ClaudeCodeRunner.h"
#include "ClaudeSubsystem.h"
#include "ScriptExecutionManager.h"
#include "MCP/UnrealClaudeMCPServer.h"
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

DEFINE_LOG_CATEGORY(LogUnrealClaude);

#define LOCTEXT_NAMESPACE "FUnrealClaudeModule"

static const FName ClaudeTabName("ClaudeAssistant");

void FUnrealClaudeModule::StartupModule()
{
	UE_LOG(LogUnrealClaude, Warning, TEXT("=== UnrealClaude BUILD 20260107-1450 THREAD_TESTS_DISABLED ==="));

	const bool bMCPOnlyMode = FClaudeCodeRunner::IsMCPOnlyMode();

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

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealClaudeModule::RegisterMenus));

		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetGlobalLevelEditorActions()->Append(PluginCommands.ToSharedRef());

		if (FClaudeCodeRunner::IsConfiguredProviderAvailable())
		{
			UE_LOG(LogUnrealClaude, Log, TEXT("%s CLI found"), *FClaudeCodeRunner::GetConfiguredProviderName());
		}
		else
		{
			UE_LOG(LogUnrealClaude, Warning, TEXT("%s CLI not found. Install with: %s"),
				*FClaudeCodeRunner::GetConfiguredProviderName(),
				*FClaudeCodeRunner::GetConfiguredProviderInstallHint());
		}
	}
	else
	{
		UE_LOG(LogUnrealClaude, Log, TEXT("ProviderMode=MCPOnly: chat UI disabled, only MCP server is started."));
		bClaudeUIEnabled = false;
	}

	StartMCPServer();
	FProjectContextManager::Get().RefreshContext();
	FScriptExecutionManager::Get();
}

void FUnrealClaudeModule::ShutdownModule()
{
	UE_LOG(LogUnrealClaude, Log, TEXT("UnrealClaude module shutting down"));

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

FUnrealClaudeModule& FUnrealClaudeModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUnrealClaudeModule>("UnrealClaude");
}

bool FUnrealClaudeModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("UnrealClaude");
}

void FUnrealClaudeModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);
	
	// Add to the main menu bar under Tools
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->FindOrAddSection("UnrealClaude");

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
		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("UnrealClaude");
		
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			FUnrealAICLICommands::Get().OpenClaudePanel,
			LOCTEXT("ClaudeToolbarButton", "AI"),
			LOCTEXT("ClaudeToolbarTooltip", "Open AI Assistant (Ctrl+Shift+C)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
		));
	}
}

void FUnrealClaudeModule::UnregisterMenus()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FUnrealClaudeModule::StartMCPServer()
{
	if (MCPServer.IsValid())
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("MCP Server already exists"));
		return;
	}

	MCPServer = MakeShared<FUnrealClaudeMCPServer>();

	if (!MCPServer->Start(GetMCPServerPort()))
	{
		UE_LOG(LogUnrealClaude, Error, TEXT("Failed to start MCP Server on port %d"), GetMCPServerPort());
		MCPServer.Reset();
	}
}

void FUnrealClaudeModule::StopMCPServer()
{
	if (MCPServer.IsValid())
	{
		MCPServer->Stop();
		MCPServer.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUnrealClaudeModule, UnrealClaude)



