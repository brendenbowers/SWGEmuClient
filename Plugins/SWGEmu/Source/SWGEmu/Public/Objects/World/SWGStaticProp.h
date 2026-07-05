#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "SWGStaticProp.generated.h"

/**
 * STAO — world decoration. Confirmed networked (StaticObjectMessage3, Core3
 * StaticObject.idl) but with a near-trivial baseline: int(0), ObjectName,
 * CustomName, int(0xFF). No condition/HAM/defender data, so no components
 * beyond identity are needed.
 */
UCLASS()
class SWGEMU_API ASWGStaticProp : public ASWGObject
{
	GENERATED_BODY()

public:
	ASWGStaticProp() = default;

	FSWGStringId ObjectName;
	FString      CustomName;
	bool bHasBase3 = false;
};
