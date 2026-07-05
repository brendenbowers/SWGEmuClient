#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "SWGInstallation.generated.h"

class USWGTangibleComponent;
class USWGConditionComponent;
class USWGDefenderComponent;

/**
 * INSO — harvesters, turrets, factories. Confirmed (InstallationObjectMessage3)
 * to be a full Tangible3 payload plus 3 extra scalars — no new component
 * needed, just USWGTangibleComponent + these loose fields.
 */
UCLASS()
class SWGEMU_API ASWGInstallation : public ASWGObject
{
	GENERATED_BODY()

public:
	ASWGInstallation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGTangibleComponent> TangibleComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGConditionComponent> ConditionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGDefenderComponent> DefenderComponent;

	// Installation-specific base3 tail (InstallationObjectMessage3.h:39-41)
	uint8 bActive         = 0;
	float SurplusPower    = 0.f;
	float BasePowerRate   = 0.f;
};
