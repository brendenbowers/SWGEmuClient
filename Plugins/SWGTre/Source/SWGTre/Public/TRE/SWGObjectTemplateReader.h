#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * Field readers for shared object templates (object/**\/shared_*.iff):
 * FORM <type> > FORM SHOT > [FORM DERV] + FORM <version> > CHUNK XXXX per
 * key/value. Only reads one file at a time — DERV chain walking (a field
 * left unset here is inherited from the base template) is the subsystem's
 * job, see USWGTreSubsystem::FindTemplateStringId.
 */
class SWGTRE_API FSWGObjectTemplateReader
{
public:
	/**
	 * Reads a StringId-typed XXXX field ("objectName", "detailedDescription",
	 * "lookAtText"). Wire shape, from shared_bantha.iff:
	 *   key\0  01(has value)  01(literal)  table\0  01(literal)  text\0
	 * Returns false when the key is absent or its value flag is 0 (unset in
	 * this layer — check the DERV parent).
	 */
	static bool FindStringIdField(const FSWGIffReader& Reader, const TCHAR* Key, FString& OutTable, FString& OutText);

	/**
	 * Reads an integer XXXX field ("collisionMaterialBlockFlags", "containerType"):
	 *   key\0  01(has value)  20(numeric literal)  int32
	 * Only the literal form is accepted; ranges/die rolls return false.
	 * False also when the key is absent or unset in this layer.
	 */
	static bool FindIntField(const FSWGIffReader& Reader, const TCHAR* Key, int32& OutValue);

	/**
	 * Reads one entry of a float-array field in the creature layer — FORM
	 * SCOT's own version form, not SHOT ("speed", "turnRate",
	 * "acceleration"; index 0 = run, 1 = walk). Wire shape, from
	 * shared_landspeeder_x31.iff:
	 *   key\0  int32 count  then per entry 01(has value) 20(literal) float, or 00 20 when unset
	 * False when absent, out of range, or unset in this layer.
	 */
	static bool FindCreatureFloatArrayField(const FSWGIffReader& Reader, const TCHAR* Key, int32 Index, float& OutValue);

	/** The template this one DERVs from (FORM SHOT > FORM DERV > XXXX path\0), or false at the chain's root. */
	static bool FindDervParentPath(const FSWGIffReader& Reader, FString& OutParentPath);

private:
	FSWGObjectTemplateReader() = default;
};
