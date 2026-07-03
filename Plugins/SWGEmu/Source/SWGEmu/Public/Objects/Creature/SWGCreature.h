

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Objects/SWGObject.h"
#include "Objects/Tangible/SWGTangible.h"
#include "SWGCreature.generated.h"

struct FCreatureObjectBaseline;

UCLASS()
class SWGEMU_API ASWGCreature : public ASWGTangible
{
	GENERATED_BODY()
public:
	ASWGCreature() = default;

	UPROPERTY(VisibleAnywhere)
	USkeletalMeshComponent* Mesh;

	UPROPERTY(VisibleAnywhere)
	UCapsuleComponent* Capsule;

	void ApplyBaseline(const FCreatureObjectBaseline& Baseline);
		
};
