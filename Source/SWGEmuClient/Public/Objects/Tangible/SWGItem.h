#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "SWGItem.generated.h"

class USWGTangibleComponent;
class USWGConditionComponent;
class USWGDefenderComponent;

/**
 * A plain TANO — items, props, resources, equippable weapons (object/tangible,
 * object/weapon, object/resource_container). Not a pawn; composition over the
 * old ASWGTangible inheritance branch (see world-object-plan.html "Actor hierarchy").
 */
UCLASS()
class SWGEMUCLIENT_API ASWGItem : public ASWGObject
{
	GENERATED_BODY()

public:
	ASWGItem();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGTangibleComponent> TangibleComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGConditionComponent> ConditionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGDefenderComponent> DefenderComponent;
};
