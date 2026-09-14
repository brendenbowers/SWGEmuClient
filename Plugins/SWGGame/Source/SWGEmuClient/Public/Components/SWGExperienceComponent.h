#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"
#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"
#include "SWGExperienceComponent.generated.h"

/** PLAY base8 — earned experience, keyed by XP type. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGExperienceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGExperienceComponent();

	TSWGBaselineList<FExperience> ExperienceList;
	bool bHasBase8 = false;

	/** Points for an XP type (e.g. "combat_general"), or 0 if the player has none. */
	int32 FindExperience(const FString& Type) const;

	void ApplyBase8(const FPlayerObjectBaseline& Baseline);
	void ApplyDelta8(const FPlayerObjectDelta& Delta);
};
