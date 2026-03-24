// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UnrealAICLIConstants.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUnrealAICLI, Log, All);

class FUnrealAICLIMCPServer;

class FUnrealAICLIModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the singleton instance */
	static FUnrealAICLIModule& Get();

	/** Check if module is available */
	static bool IsAvailable();

	/** Get the MCP server instance */
	TSharedPtr<FUnrealAICLIMCPServer> GetMCPServer() const { return MCPServer; }

	/** Get MCP server port - uses centralized constant */
	static constexpr uint32 GetMCPServerPort() { return UnrealAICLIConstants::MCPServer::DefaultPort; }

private:
	void RegisterMenus();
	void UnregisterMenus();
	void StartMCPServer();
	void StopMCPServer();

	TSharedPtr<class FUICommandList> PluginCommands;
	TSharedPtr<class SDockTab> ClaudeTab;
	TSharedPtr<FUnrealAICLIMCPServer> MCPServer;
	bool bClaudeUIEnabled = false;
};

