#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGDataTableReader.h"

/**
 * datatables/customization/allow_bald.iff: species_gender -> whether that body type may pick "no hair"
 * (humans, bothans, mon calamari, rodians, wookiees can; zabraks, twi'leks, trandoshans cannot).
 * Retail loads it with the other creation tables from one 8.7 KB routine (SWGEmu.exe 0x1215cc0,
 * ALLOW_BALD string at 0x18e271c); only the character-creation and Image Designer hair lists ask.
 */
class SWGTRE_API FSWGAllowBaldTable
{
public:
	static bool Read(const FSWGDataTableData& Table, FSWGAllowBaldTable& OutResult);

	/** Species keys look like "human_male"; unknown species default to allowed. */
	bool IsBaldAllowed(const FString& SpeciesGender) const;

	const TMap<FString, bool>& GetAll() const { return BySpeciesGender; }

private:
	TMap<FString, bool> BySpeciesGender;
};

/** One customization_group_shared.iff row: how a named customization group behaves everywhere. */
struct FSWGCustomizationGroupShared
{
	FString Group;
	bool bIsMarking = false;
	bool bIsHair = false;
};

/** One customization_group_instance.iff row: a group as one species_gender shows it. */
struct FSWGCustomizationGroupInstance
{
	FString SpeciesGender;
	FString Group;
	bool bColorLinkedToHair = false;
	bool bColorLinkedToBody = false;
	/** Bone the preview camera looks at. */
	FString LookAtBone;
	float ZoomFactor = 1.0f;
};

/** datatables/customization/customization_group_shared.iff and customization_group_instance.iff. */
class SWGTRE_API FSWGCustomizationGroupTable
{
public:
	static bool Read(const FSWGDataTableData& SharedTable, const FSWGDataTableData& InstanceTable, FSWGCustomizationGroupTable& OutResult);

	const FSWGCustomizationGroupShared* FindShared(const FString& Group) const;

	/** The groups one species_gender offers, in file order. */
	TArray<const FSWGCustomizationGroupInstance*> GetGroupsFor(const FString& SpeciesGender) const;

	TArray<FSWGCustomizationGroupShared> Shared;
	TArray<FSWGCustomizationGroupInstance> Instances;
};

/** One customization_data.iff row: a slider or palette picker on a species' creation / Image Designer screen. */
struct FSWGCustomizationDataRow
{
	FString SpeciesGender;
	FString Group;
	/** Widget kind: "hslider", "vslider", "dualhslider", "color", "discrete"... as authored. */
	FString Type;
	FString Name;
	/** The customization variable(s) the control writes, as authored (space or comma separated). */
	FString Variables;
	bool bIsScale = false;
	bool bReverse = false;
	bool bDiscrete = false;
	bool bRandomizable = true;
	bool bIsVarHairColor = false;
	FString ImageDesignSkillMod;
	int32 SkillModValue = 0;
	float CameraYaw = 0.0f;
	/** Every cell by column name, for the columns not broken out above. */
	TMap<FString, FString> Cells;
};

class SWGTRE_API FSWGCustomizationDataTable
{
public:
	static bool Read(const FSWGDataTableData& Table, FSWGCustomizationDataTable& OutResult);

	TArray<const FSWGCustomizationDataRow*> GetRowsFor(const FString& SpeciesGender, const FString& Group = FString()) const;

	TArray<FSWGCustomizationDataRow> Rows;
};

/** One palette_columns.iff row: how a palette is cut into columns for the creation and Image Designer pickers. */
struct FSWGPaletteColumns
{
	FString Palette;
	int32 CreationColumns = 0;
	int32 CreationIndexes = 0;
	/** Image Designer tiers, novice first then levels 1-4 and master: {columns, indexes}. */
	TArray<TPair<int32, int32>> ImageDesignTiers;
};

class SWGTRE_API FSWGPaletteColumnsTable
{
public:
	static bool Read(const FSWGDataTableData& Table, FSWGPaletteColumnsTable& OutResult);

	/** Palette names are stored without folder or extension ("pc_skin_hum"). */
	const FSWGPaletteColumns* Find(const FString& PaletteName) const;

	TArray<FSWGPaletteColumns> Rows;
};

/** One hair_assets_skill_mods.iff row. */
struct FSWGHairAsset
{
	FString SharedTemplate;
	FString PlayerTemplate;
	int32 SkillModValue = 0;
	bool bAvailableAtCreation = false;
};

class SWGTRE_API FSWGHairAssetTable
{
public:
	static bool Read(const FSWGDataTableData& Table, FSWGHairAssetTable& OutResult);

	TArray<FSWGHairAsset> Rows;
};
