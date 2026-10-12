#include "Animation/SWGAnimEventNotify.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void USWGAnimEventNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}

	for (UActorComponent* Component : Owner->GetComponents())
	{
		if (ISWGAnimEventReceiver* Receiver = Cast<ISWGAnimEventReceiver>(Component))
		{
			Receiver->OnAnimEvent(EventName, MeshComp);
			return;
		}
	}
}
