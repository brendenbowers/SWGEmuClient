#include "Common/SWGMovementTables.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGIffReader.h"
#include "UObject/Class.h"

#include <atomic>

namespace
{
	// Posture 255 (Invalid) is never a row in movement_human.iff, so the array
	// only has to cover the real postures — Upright(0) through Dead(14).
	constexpr int32 NumPostures = 15;

	struct FSWGMovementTableState
	{
		FSWGPostureMovement Postures[NumPostures];
		bool bHasPosture[NumPostures] = {};
		// Keyed by state bit index (the ESWGState value), not by mask — the
		// tables are three rows between them, so testing each entry's bit
		// against the mask beats maintaining a parallel mask-keyed structure.
		TMap<uint8, ESWGSpeedCategory> StateSpeedCaps;
		TMap<uint8, float> StateRateModifiers;
	};

	FSWGMovementTableState& GetTables()
	{
		static FSWGMovementTableState Tables;
		return Tables;
	}

	std::atomic<bool> bTablesLoaded{ false };

	bool ReadTable(const USWGTreSubsystem& TreSubsystem, const TCHAR* VirtualPath, FSWGDataTableData& OutData)
	{
		FSWGIffReader Reader = TreSubsystem.CreateIffReader(VirtualPath);
		if (!Reader.IsValid() || !FSWGDataTableReader::ReadDataTable(Reader, OutData))
		{
			UE_LOG(LogTemp, Warning, TEXT("SWGMovementTables: failed to read %s"), VirtualPath);
			return false;
		}
		return true;
	}

	/**
	 * Cross-checks one of the datatables/include enum tables against the
	 * matching UENUM in SWGPostureTypes.h. The ESWG* enums are compile-time
	 * values (they index switch statements and array slots), so a mismatch
	 * can't be repaired at runtime — but it silently misinterprets every
	 * posture/state byte the server sends, which is worth a loud warning on a
	 * modded or newer TRE set.
	 */
	void VerifyEnumTable(const USWGTreSubsystem& TreSubsystem, const TCHAR* VirtualPath, const TCHAR* KeyColumn, const TCHAR* EnumName)
	{
		FSWGDataTableData Data;
		if (!ReadTable(TreSubsystem, VirtualPath, Data))
		{
			return;
		}

		const UEnum* Enum = FindObject<UEnum>(nullptr, EnumName);
		if (!Enum)
		{
			return;
		}

		for (int32 RowIdx = 0; RowIdx < Data.Rows.Num(); ++RowIdx)
		{
			const FString Name = Data.GetCell(RowIdx, KeyColumn);
			const FString Value = Data.GetCell(RowIdx, TEXT("value"));
			if (Name.IsEmpty() || Name == TEXT("Invalid"))
			{
				continue;
			}

			const int64 TableValue = FCString::Atoi64(*Value);
			const int64 EnumValue = Enum->GetValueByNameString(Name);
			if (EnumValue == INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning, TEXT("SWGMovementTables: %s has '%s' (=%lld), which %s doesn't declare"), VirtualPath, *Name, TableValue, EnumName);
			}
			else if (EnumValue != TableValue)
			{
				UE_LOG(LogTemp, Warning, TEXT("SWGMovementTables: %s says %s=%lld but %s says %lld"), VirtualPath, *Name, TableValue, EnumName, EnumValue);
			}
		}
	}

	/** movement_human.iff stores unset locomotion columns as -1; everything else is a locomotion.iff value. */
	ESWGLocomotion CellToLocomotion(const FString& Cell)
	{
		const int32 Value = FCString::Atoi(*Cell);
		return (Value < 0 || Value > 21) ? ESWGLocomotion::Invalid : (ESWGLocomotion)(uint8)Value;
	}

	float CellToScale(const FString& Cell, float Fallback)
	{
		return Cell.IsEmpty() ? Fallback : FCString::Atof(*Cell);
	}
}

bool SWGMovementTables::Load(const USWGTreSubsystem& TreSubsystem)
{
	FSWGMovementTableState& Tables = GetTables();
	FMemory::Memzero(Tables.bHasPosture);
	Tables.StateSpeedCaps.Reset();
	Tables.StateRateModifiers.Reset();

	VerifyEnumTable(TreSubsystem, TEXT("datatables/include/posture.iff"), TEXT("posture"), TEXT("/Script/SWGEmuClient.ESWGPosture"));
	VerifyEnumTable(TreSubsystem, TEXT("datatables/include/locomotion.iff"), TEXT("locomotion"), TEXT("/Script/SWGEmuClient.ESWGLocomotion"));
	VerifyEnumTable(TreSubsystem, TEXT("datatables/include/state.iff"), TEXT("state"), TEXT("/Script/SWGEmuClient.ESWGState"));

	bool bOk = true;

	FSWGDataTableData MovementData;
	if (ReadTable(TreSubsystem, TEXT("datatables/movement/movement_human.iff"), MovementData))
	{
		for (int32 RowIdx = 0; RowIdx < MovementData.Rows.Num(); ++RowIdx)
		{
			const int32 Posture = FCString::Atoi(*MovementData.GetCell(RowIdx, TEXT("posture")));
			if (Posture < 0 || Posture >= NumPostures)
			{
				continue;
			}

			FSWGPostureMovement& Row = Tables.Postures[Posture];
			Row.Stationary = CellToLocomotion(MovementData.GetCell(RowIdx, TEXT("stationary")));
			Row.Slow = CellToLocomotion(MovementData.GetCell(RowIdx, TEXT("slow")));
			Row.Fast = CellToLocomotion(MovementData.GetCell(RowIdx, TEXT("fast")));
			Row.MovementScale = CellToScale(MovementData.GetCell(RowIdx, TEXT("movementScale")), 1.0f);
			Row.AccelerationScale = CellToScale(MovementData.GetCell(RowIdx, TEXT("accelerationScale")), 1.0f);
			Row.TurnScale = CellToScale(MovementData.GetCell(RowIdx, TEXT("turnScale")), 1.0f);
			Row.CanSeeHeightMod = CellToScale(MovementData.GetCell(RowIdx, TEXT("canSeeHeightMod")), 1.0f);
			Tables.bHasPosture[Posture] = true;
		}
	}
	else
	{
		bOk = false;
	}

	FSWGDataTableData StateData;
	if (ReadTable(TreSubsystem, TEXT("datatables/movement/movementstates.iff"), StateData))
	{
		for (int32 RowIdx = 0; RowIdx < StateData.Rows.Num(); ++RowIdx)
		{
			const int32 State = FCString::Atoi(*StateData.GetCell(RowIdx, TEXT("state")));
			const int32 Category = FCString::Atoi(*StateData.GetCell(RowIdx, TEXT("maxSpeedCategory")));
			if (State < 0 || State > 63 || Category < 0 || Category > 2)
			{
				continue;
			}
			Tables.StateSpeedCaps.Add((uint8)State, (ESWGSpeedCategory)(uint8)Category);
		}
	}
	else
	{
		bOk = false;
	}

	FSWGDataTableData RateData;
	if (ReadTable(TreSubsystem, TEXT("datatables/movement/state_rate_modifiers.iff"), RateData))
	{
		for (int32 RowIdx = 0; RowIdx < RateData.Rows.Num(); ++RowIdx)
		{
			const int32 State = FCString::Atoi(*RateData.GetCell(RowIdx, TEXT("state")));
			if (State < 0 || State > 63)
			{
				continue;
			}
			Tables.StateRateModifiers.Add((uint8)State, FCString::Atof(*RateData.GetCell(RowIdx, TEXT("movementRateModifier"))));
		}
	}
	else
	{
		bOk = false;
	}

	// Published last: every getter gates on this, so nothing can observe a
	// half-filled table if a creature spawns while loading is still running.
	bTablesLoaded.store(true, std::memory_order_release);
	return bOk;
}

bool SWGMovementTables::IsLoaded()
{
	return bTablesLoaded.load(std::memory_order_acquire);
}

const FSWGPostureMovement* SWGMovementTables::FindPosture(ESWGPosture Posture)
{
	const uint8 Index = (uint8)Posture;
	if (!IsLoaded() || Index >= NumPostures)
	{
		return nullptr;
	}

	const FSWGMovementTableState& Tables = GetTables();
	return Tables.bHasPosture[Index] ? &Tables.Postures[Index] : nullptr;
}

ESWGLocomotion SWGMovementTables::ResolveLocomotion(ESWGPosture Posture, ESWGSpeedCategory SpeedCategory)
{
	const FSWGPostureMovement* Row = FindPosture(Posture);
	if (!Row)
	{
		return ESWGLocomotion::Invalid;
	}

	if (SpeedCategory == ESWGSpeedCategory::Fast && Row->Fast != ESWGLocomotion::Invalid)
	{
		return Row->Fast;
	}
	if (SpeedCategory >= ESWGSpeedCategory::Slow && Row->Slow != ESWGLocomotion::Invalid)
	{
		return Row->Slow;
	}
	return Row->Stationary;
}

ESWGSpeedCategory SWGMovementTables::GetMaxSpeedCategory(int64 StateBitmask)
{
	ESWGSpeedCategory Cap = ESWGSpeedCategory::Fast;
	if (!IsLoaded() || StateBitmask == 0)
	{
		return Cap;
	}

	for (const TPair<uint8, ESWGSpeedCategory>& Pair : GetTables().StateSpeedCaps)
	{
		if ((StateBitmask & (int64(1) << int64(Pair.Key))) != 0)
		{
			Cap = FMath::Min(Cap, Pair.Value);
		}
	}
	return Cap;
}

float SWGMovementTables::GetStateRateModifier(int64 StateBitmask)
{
	float Modifier = 1.0f;
	if (!IsLoaded() || StateBitmask == 0)
	{
		return Modifier;
	}

	for (const TPair<uint8, float>& Pair : GetTables().StateRateModifiers)
	{
		if ((StateBitmask & (int64(1) << int64(Pair.Key))) != 0)
		{
			Modifier *= Pair.Value;
		}
	}
	return Modifier;
}

ESWGSpeedCategory SWGMovementTables::ClassifySpeed(float Speed, float WalkSpeed, float RunSpeed)
{
	if (Speed <= KINDA_SMALL_NUMBER)
	{
		return ESWGSpeedCategory::Stationary;
	}
	if (RunSpeed <= WalkSpeed)
	{
		// No usable run speed to split against (some creature templates send
		// run <= walk); anything moving is then just "slow".
		return ESWGSpeedCategory::Slow;
	}
	return Speed > (WalkSpeed + RunSpeed) * 0.5f ? ESWGSpeedCategory::Fast : ESWGSpeedCategory::Slow;
}
