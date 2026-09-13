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

	/** The template this one DERVs from (FORM SHOT > FORM DERV > XXXX path\0), or false at the chain's root. */
	static bool FindDervParentPath(const FSWGIffReader& Reader, FString& OutParentPath);

private:
	FSWGObjectTemplateReader() = default;
};
