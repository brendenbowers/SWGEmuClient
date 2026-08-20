#include "TRE/SWGDoorStyleRow.h"
#include "TRE/SWGDataTableReader.h"
#include "UObject/Package.h"

namespace SWGDoorStyle
{
	const FString DataTablePath = TEXT("/Game/SWGEmu/Generated/DT_DoorStyles.DT_DoorStyles");
}

UDataTable* SWGDoorStyle::BuildDataTable(const FSWGDataTableData& RawData)
{
	const int32 NameCol = RawData.GetColumnIndex(TEXT("doorStyleName"));
	if (NameCol == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("SWGDoorStyle::BuildDataTable: door_style.iff has no doorStyleName column"));
		return nullptr;
	}

	const int32 AppearanceCol = RawData.GetColumnIndex(TEXT("doorAppearance"));
	const int32 Appearance2Col = RawData.GetColumnIndex(TEXT("doorAppearance2"));
	const int32 Flip2Col = RawData.GetColumnIndex(TEXT("doorFlip2"));
	const int32 MoveXCol = RawData.GetColumnIndex(TEXT("doorMoveX"));
	const int32 MoveYCol = RawData.GetColumnIndex(TEXT("doorMoveY"));
	const int32 MoveZCol = RawData.GetColumnIndex(TEXT("doorMoveZ"));
	const int32 OpenTimeCol = RawData.GetColumnIndex(TEXT("openTime"));
	const int32 CloseTimeCol = RawData.GetColumnIndex(TEXT("closeTime"));
	const int32 SpringinessCol = RawData.GetColumnIndex(TEXT("springiness"));
	const int32 SmoothnessCol = RawData.GetColumnIndex(TEXT("smoothness"));
	const int32 TriggerRadiusCol = RawData.GetColumnIndex(TEXT("triggerRadius"));
	const int32 IsForceFieldCol = RawData.GetColumnIndex(TEXT("isForceField"));
	const int32 OpenBeginCol = RawData.GetColumnIndex(TEXT("openBeginEffect"));
	const int32 OpenEndCol = RawData.GetColumnIndex(TEXT("openEndEffect"));
	const int32 CloseBeginCol = RawData.GetColumnIndex(TEXT("closeBeginEffect"));
	const int32 CloseEndCol = RawData.GetColumnIndex(TEXT("closeEndEffect"));

	auto CellAt = [](const FSWGDataTableRow& Row, int32 ColIdx) -> FString
	{
		return (ColIdx != INDEX_NONE && Row.Cells.IsValidIndex(ColIdx)) ? Row.Cells[ColIdx] : FString();
	};

	const FString PackagePath = FPackageName::ObjectPathToPackageName(DataTablePath);
	UPackage* Package = CreatePackage(*PackagePath);
	const FString ObjectName = FPackageName::ObjectPathToObjectName(DataTablePath);

	UDataTable* Table = NewObject<UDataTable>(Package, FName(*ObjectName), RF_Public | RF_Standalone);
	Table->RowStruct = FSWGDoorStyleRow::StaticStruct();

	for (const FSWGDataTableRow& RawRow : RawData.Rows)
	{
		const FString StyleName = CellAt(RawRow, NameCol);
		if (StyleName.IsEmpty())
		{
			continue;
		}

		FSWGDoorStyleRow Row;
		Row.DoorAppearance = CellAt(RawRow, AppearanceCol);
		Row.DoorAppearance2 = CellAt(RawRow, Appearance2Col);
		Row.bFlip2 = FCString::Atoi(*CellAt(RawRow, Flip2Col)) != 0;
		Row.MoveOffset = FVector(
			FCString::Atof(*CellAt(RawRow, MoveXCol)),
			FCString::Atof(*CellAt(RawRow, MoveZCol)),
			FCString::Atof(*CellAt(RawRow, MoveYCol)));
		Row.OpenTime = FCString::Atof(*CellAt(RawRow, OpenTimeCol));
		Row.CloseTime = FCString::Atof(*CellAt(RawRow, CloseTimeCol));
		Row.Springiness = FCString::Atof(*CellAt(RawRow, SpringinessCol));
		Row.Smoothness = FCString::Atof(*CellAt(RawRow, SmoothnessCol));
		Row.TriggerRadius = FCString::Atof(*CellAt(RawRow, TriggerRadiusCol));
		Row.bIsForceField = FCString::Atoi(*CellAt(RawRow, IsForceFieldCol)) != 0;
		Row.OpenBeginEffect = CellAt(RawRow, OpenBeginCol);
		Row.OpenEndEffect = CellAt(RawRow, OpenEndCol);
		Row.CloseBeginEffect = CellAt(RawRow, CloseBeginCol);
		Row.CloseEndEffect = CellAt(RawRow, CloseEndCol);

		Table->AddRow(FName(*StyleName), Row);
	}

	return Table;
}
