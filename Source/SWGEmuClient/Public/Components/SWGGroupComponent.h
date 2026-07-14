#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWGGroupComponent.generated.h"

struct FSWGPacket;

/** CREO base6. GroupInviterId + GroupInviteCounter are sent as one combined
 *  delta update on the wire, not two independent field indices. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGGroupComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGGroupComponent();

	int64 GroupId             = 0;
	int64 GroupInviterId      = 0;
	int64 GroupInviteCounter  = 0;
	bool bHasBase6 = false;

	void ApplyBase6(FSWGPacket& Packet);
};
