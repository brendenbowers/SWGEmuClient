#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "SWGInstallation.generated.h"

class USWGTangibleComponent;
class USWGConditionComponent;
class USWGDefenderComponent;
class USWGInstallationComponent;
class USWGHarvesterComponent;

/**
 * INSO — harvesters, turrets, factories. Base3 is a full Tangible3 payload plus
 * an active flag and two power scalars, which USWGInstallationComponent holds.
 */
UCLASS()
class SWGEMUCLIENT_API ASWGInstallation : public ASWGObject
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGInstallationComponent> InstallationComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGHarvesterComponent> HarvesterComponent;
};
