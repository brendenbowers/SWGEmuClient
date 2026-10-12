#pragma once

#include "CoreMinimal.h"

class FSWGIffReader;

/**
 * A bone mask (appearance/mask/*.iff): FORM XFMS > FORM 0000 > INGR, a uint16
 * count then that many null-terminated joint names. The joints listed are the
 * ones a masked animation drives; every other joint keeps what lower layers
 * gave it. Named by the second string of a LAT clip's PPTR chunk.
 */
class SWGANIMATION_API FSWGMaskReader
{
public:
	static bool ReadMask(const FSWGIffReader& Reader, TArray<FString>& OutBoneNames);

private:
	FSWGMaskReader() = default;
};
