#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SWGResourceClassRow.generated.h"

/** One node of the resource class tree (metal -> metal_ferrous -> steel -> steel_duralloy), keyed by the class's ENUM. */
USTRUCT(BlueprintType)
struct SWGTRE_API FSWGResourceClassRow : public FTableRowBase
{
	GENERATED_BODY()

	/** ENUM of the class one level up, empty at the root. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString ParentClass;

	/** Human-readable name from the class column at this row's depth, e.g. "Duralloy Steel". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString DisplayName;
};

struct FSWGDataTableData;

namespace SWGResourceClass
{
	/** Full object path of the resource-class DataTable. */
	SWGTRE_API extern const FString DataTablePath;

	/**
	 * Builds the resource-class DataTable (RowStruct = FSWGResourceClassRow)
	 * from an already-parsed resource_tree.iff.
	 *
	 * That table stores the tree the way a spreadsheet would: every row has an
	 * ENUM plus eight "CLASS 1".."CLASS 8" columns of which exactly one is
	 * non-empty, and which one it is gives the row's depth. A row's parent is
	 * therefore the nearest preceding row one level shallower.
	 */
	SWGTRE_API UDataTable* BuildDataTable(const FSWGDataTableData& RawData);

	/**
	 * Ordered "ui_res_<name>" base names to try for a resource class, most
	 * specific first, walking up the ancestry — a specific resource such as
	 * "steel_duralloy" has no icon of its own and resolves to its parent's.
	 *
	 * Icons are named for the ancestry path rather than the bare class, so a
	 * class whose enum doesn't already carry its parent's prefix gets it
	 * prepended: "steel" under "metal_ferrous" yields "metal_ferrous_steel".
	 * Both forms are offered per level since classes like "metal_ferrous"
	 * already embed their parent.
	 */
	SWGTRE_API TArray<FString> GetIconCandidates(const UDataTable* Table, const FString& ResourceType);
}
