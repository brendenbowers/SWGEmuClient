#include "Audio/SWGInteriorTable.h"

#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGDataTableReader.h"

const FSWGInteriorTable* FSWGInteriorTable::Get(const USWGTreSubsystem& Tre)
{
	static TSharedPtr<FSWGInteriorTable> Table;
	if (!Table.IsValid())
	{
		FSWGDataTableData Data;
		if (!FSWGDataTableReader::ReadDataTable(Tre.CreateIffReader(TEXT("datatables/interior/interior.iff")), Data))
		{
			return nullptr;
		}

		Table = MakeShared<FSWGInteriorTable>();
		for (int32 RowIndex = 0; RowIndex < Data.Rows.Num(); ++RowIndex)
		{
			FSWGInteriorRow& Row = Table->Rows.AddDefaulted_GetRef();
			Row.PobName = Data.GetCell(RowIndex, TEXT("PobName"));
			Row.CellName = Data.GetCell(RowIndex, TEXT("Cell Name"));
			Row.DayAmbient = Data.GetCell(RowIndex, TEXT("Day sound/ambient Sound (2d .snd)"));
			Row.NightAmbient = Data.GetCell(RowIndex, TEXT("Night sound/ambient Sound (2d .snd)"));
			Row.FirstMusic = Data.GetCell(RowIndex, TEXT("First Music (2d .snd)"));
			Row.SurfaceType = Data.GetCell(RowIndex, TEXT("Surface Type"));
			Row.RoomType = FCString::Atoi(*Data.GetCell(RowIndex, TEXT("Room Type")));
		}
	}
	return Table.Get();
}

const FSWGInteriorRow* FSWGInteriorTable::Find(const FString& PobName, const FString& CellName, EMatch* OutMatch) const
{
	const TCHAR* DefaultName = TEXT("default");
	auto FindRow = [this](const FString& Pob, const FString& Cell) -> const FSWGInteriorRow*
	{
		return Rows.FindByPredicate([&](const FSWGInteriorRow& Row)
			{
				return Row.PobName.Equals(Pob, ESearchCase::IgnoreCase) && Row.CellName.Equals(Cell, ESearchCase::IgnoreCase);
			});
	};

	EMatch Match = EMatch::Cell;
	const FSWGInteriorRow* Found = CellName.IsEmpty() ? nullptr : FindRow(PobName, CellName);
	if (!Found)
	{
		Match = EMatch::BuildingDefault;
		Found = FindRow(PobName, DefaultName);
	}
	if (!Found)
	{
		Match = EMatch::GlobalDefault;
		Found = FindRow(DefaultName, DefaultName);
	}
	if (OutMatch)
	{
		*OutMatch = Found ? Match : EMatch::None;
	}
	return Found;
}

bool FSWGInteriorTable::ResolveCellKey(const ASWGCell* Cell, UGameInstance& GameInstance, FString& OutPobName, FString& OutCellName)
{
	USWGTreSubsystem* Tre = GameInstance.GetSubsystem<USWGTreSubsystem>();
	USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance.GetSubsystem<USWGMeshGeneratorSubsystem>();
	const ASWGBuilding* Building = Cell ? Cell->OwningBuilding.Get() : nullptr;
	if (!Tre || !MeshGenerator || !Building)
	{
		return false;
	}

	const FString TemplatePath = Building->StaticTemplatePath.IsEmpty() ? Tre->ResolveTemplatePath(Building->GetObjectCrc()) : Building->StaticTemplatePath;
	FString PobPath;
	if (TemplatePath.IsEmpty() || !MeshGenerator->ResolvePortalLayoutPath(TemplatePath, PobPath))
	{
		return false;
	}

	OutPobName = FPaths::GetBaseFilename(PobPath);
	OutCellName.Reset();
	for (const FSWGPobCell& PobCell : Building->PortalData.Cells)
	{
		if (PobCell.CellIndex == Cell->CellNumber)
		{
			OutCellName = PobCell.CellName;
			break;
		}
	}
	return true;
}

const FSWGInteriorRow* FSWGInteriorTable::FindForCell(const ASWGCell* Cell, UGameInstance& GameInstance, EMatch* OutMatch)
{
	const USWGTreSubsystem* Tre = GameInstance.GetSubsystem<USWGTreSubsystem>();
	const FSWGInteriorTable* Table = Tre ? Get(*Tre) : nullptr;
	FString PobName, CellName;
	if (!Table || !ResolveCellKey(Cell, GameInstance, PobName, CellName))
	{
		return nullptr;
	}
	return Table->Find(PobName, CellName, OutMatch);
}

const TCHAR* FSWGInteriorTable::MatchName(EMatch Match)
{
	switch (Match)
	{
		case EMatch::Cell: return TEXT("cell row");
		case EMatch::BuildingDefault: return TEXT("building default row");
		case EMatch::GlobalDefault: return TEXT("global default row");
		default: return TEXT("none");
	}
}
