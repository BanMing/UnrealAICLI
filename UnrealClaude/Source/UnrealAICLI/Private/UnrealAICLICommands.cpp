// Copyright Natali Caggiano. All Rights Reserved.

#include "UnrealAICLICommands.h"

#define LOCTEXT_NAMESPACE "UnrealAICLI"

void FUnrealAICLICommands::RegisterCommands()
{
	UI_COMMAND(
		OpenClaudePanel,
		"AI Assistant",
		"Open the AI Assistant panel for UE5.7 help",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		QuickAsk,
		"Quick Ask AI",
		"Quickly ask an AI question",
		EUserInterfaceActionType::Button,
		FInputChord()
	);
}

#undef LOCTEXT_NAMESPACE


