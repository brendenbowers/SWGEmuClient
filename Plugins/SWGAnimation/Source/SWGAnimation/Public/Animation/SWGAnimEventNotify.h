#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "UObject/Interface.h"
#include "SWGAnimEventNotify.generated.h"

class USkeletalMeshComponent;

UINTERFACE(MinimalAPI)
class USWGAnimEventReceiver : public UInterface
{
	GENERATED_BODY()
};

/** Implemented by a component on the mesh's owning actor to receive the .ans MSGS events. */
class SWGANIMATION_API ISWGAnimEventReceiver
{
	GENERATED_BODY()

public:
	virtual void OnAnimEvent(FName EventName, USkeletalMeshComponent* MeshComp) = 0;
};

/** One .ans MSGS event ("event_footstep", "sound_rancor_walk", "fire1", ...) baked onto a generated UAnimSequence. */
UCLASS(MinimalAPI)
class USWGAnimEventNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "SWG")
	FName EventName;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return EventName.ToString(); }
};
