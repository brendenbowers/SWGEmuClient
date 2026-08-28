#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Creature/CreatureObjectBaseline.h"
#include "Network/Objects/Zone/Creature/CreatureObjectDelta.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Creature/SkillModifier.h"
#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"
#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"
#include "SWGSkillComponent.generated.h"

struct FSWGPacket;

/**
 * CREO base1 (SkillList) + base4 (SkillMods), and the PLAY base9 ability list.
 * Abilities and certifications come from the player object rather than the
 * creature, but they're the same concern as the skills that grant them.
 */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGSkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGSkillComponent();

	TSWGBaselineList<FString> SkillList;
	bool bHasBase1 = false;

	TSWGBaselineList<FSkillModifier> SkillMods;
	bool bHasBase4 = false;

	/** Certifications and command names, from the player object's base9. */
	TSWGBaselineList<FString> AbilityList;
	bool bHasPlayerBase9 = false;

	void ApplyBase1(const FCreatureObjectBaseline& Baseline);
	void ApplyDelta1(const FCreatureObjectDelta& Delta);
	void ApplyBase4(const FCreatureObjectBaseline& Baseline);
	void ApplyDelta4(const FCreatureObjectDelta& Delta);
	void ApplyBase9(const FPlayerObjectBaseline& Baseline);
	void ApplyDelta9(const FPlayerObjectDelta& Delta);
};
