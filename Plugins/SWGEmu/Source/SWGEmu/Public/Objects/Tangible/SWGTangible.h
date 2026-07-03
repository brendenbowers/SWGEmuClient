

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Objects/SWGObject.h"
#include "SWGTangible.generated.h"

struct FTangibleObjectBaseline;

UCLASS()
class SWGEMU_API ASWGTangible : public ASWGObject
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASWGTangible() = default;
	
	void ApplyBaseline(const FTangibleObjectBaseline& Baseline);
	
};
