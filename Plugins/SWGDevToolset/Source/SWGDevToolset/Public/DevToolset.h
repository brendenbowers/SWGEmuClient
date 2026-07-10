// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "DevToolset.generated.h"

/**
 * Editor-dev tools for the SWGEmuClient iteration workflow: running console
 * commands and controlling Live Coding recompiles remotely (via the MCP
 * bridge), since we otherwise have no way to trigger these while the editor
 * itself owns the only session with Live Coding enabled.
 */
UCLASS(BlueprintType, MinimalAPI)
class UDevToolset : public UToolsetDefinition
{
	GENERATED_BODY()
public:

	/**
	 * Executes an arbitrary console command in the editor and returns any output
	 * logged during execution.
	 * @param Command The console command to run, e.g. "stat fps" or "LiveCoding.Compile".
	 * @return The captured output text (may be empty if the command produced no log output).
	 */
	UFUNCTION(meta = (AICallable), Category = "DevToolset")
	static FString ExecuteConsoleCommand(const FString& Command);

	/**
	 * Triggers a Live Coding recompile and blocks until it completes.
	 * @return True if the compile succeeded or there were no changes to compile;
	 *         false if Live Coding isn't enabled for this session or the compile failed.
	 */
	UFUNCTION(meta = (AICallable), Category = "DevToolset")
	static bool RecompileLiveCoding();
};
