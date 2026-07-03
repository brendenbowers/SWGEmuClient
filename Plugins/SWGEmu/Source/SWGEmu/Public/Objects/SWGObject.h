

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWGObject.generated.h"

UCLASS()
class SWGEMU_API ASWGObject : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASWGObject() = default;

	int64   SWGObjectId = 0;
	int32   SWGObjectCRC = 0;
	bool    bBaselinesComplete = false;
	
	virtual void OnBaselineComplete() {}
};
