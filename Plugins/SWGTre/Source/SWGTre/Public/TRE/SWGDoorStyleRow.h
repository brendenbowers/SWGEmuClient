#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SWGDoorStyleRow.generated.h"

USTRUCT(BlueprintType)
struct SWGTRE_API FSWGDoorStyleRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString DoorAppearance;

	/**
	 * Second leaf of a double door 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString DoorAppearance2;

	/** True on every row where DoorAppearance2 is set — presumably mirrors the second leaf's move direction so both leaves open outward from center, not confirmed against any documented spec. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	bool bFlip2 = false;

	/** Local-space translation from closed to fully open, e.g. (-2.1, 0, 0) — slides along a single axis, never combined, on every style checked so far. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FVector MoveOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	float OpenTime = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	float CloseTime = 0.5f;

	/** Tuning knobs for the open/close interpolation curve — no documented physical meaning found in Core3 or the mesh data, treat as an easing shape, not a rigid spec. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	float Springiness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	float Smoothness = 1.0f;

	/** Auto-open proximity, in meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	float TriggerRadius = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	bool bIsForceField = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString OpenBeginEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString OpenEndEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString CloseBeginEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString CloseEndEffect;
};

struct FSWGDataTableData;

namespace SWGDoorStyle
{
	/**
	 * Full object path of the door-style DataTable
	 */
	SWGTRE_API extern const FString DataTablePath;

	/**
	 * Builds the door-style DataTable (RowStruct = FSWGDoorStyleRow) from an
	 * already-parsed door_style.iff
	 */
	SWGTRE_API UDataTable* BuildDataTable(const FSWGDataTableData& RawData);
}
