

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Objects/Creature/SWGCreature.h"
#include "SWGPlayer.generated.h"

struct FPlayerObjectBaseline;

UCLASS()
class SWGEMU_API ASWGPlayer : public ASWGCreature
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASWGPlayer() = default;

	void ApplyBaseline(const FPlayerObjectBaseline& Baseline);
};
