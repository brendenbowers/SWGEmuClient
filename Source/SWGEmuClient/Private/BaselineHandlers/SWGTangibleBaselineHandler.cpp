#include "BaselineHandlers/SWGTangibleBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"

namespace
{
	// TANO base3: SWGTangibleBaselineParser::ParseBase3.
	bool ApplyTangibleBaseline(AActor& Actor, uint8 Slot, FSWGPacket& Packet)
	{
		switch (Slot)
		{
			case 3:
			{
				USWGTangibleComponent* TangibleComponent = Actor.GetComponentByClass<USWGTangibleComponent>();
				USWGConditionComponent* ConditionComponent = Actor.GetComponentByClass<USWGConditionComponent>();
				if (!TangibleComponent || !ConditionComponent)
				{
					return false;
				}

				TangibleComponent->ApplyBase3Part1(Packet);
				ConditionComponent->ApplyBase3(Packet);
				TangibleComponent->ApplyBase3Part2(Packet);
				return true;
			}
			case 6:
			{
				USWGDefenderComponent* DefenderComponent = Actor.GetComponentByClass<USWGDefenderComponent>();
				if (!DefenderComponent)
				{
					return false;
				}

				DefenderComponent->ApplyBase6(Packet);
				return true;
			}
			default:
				UE_LOG(LogTemp, Verbose, TEXT("FSWGTangibleBaselineHandler: no TANO baseline dispatch for slot %d"), Slot);
				return true;
		}
	}
}

bool FSWGTangibleBaselineHandler::CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const
{
	// the filer on the message type should be enough, this may need to be updated in the future
	return true;
}

bool FSWGTangibleBaselineHandler::HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();

	if (!ApplyTangibleBaseline(Actor, Msg.BaselineType, Packet))
	{
		// Declining lets any other handler registered for this type try, then
		// the object graph's own fallback dispatch.
		UE_LOG(LogTemp, Warning, TEXT("FSWGTangibleBaselineHandler: %s (object %lld) can't take a %s slot %d baseline — missing components"),
			*Actor.GetClass()->GetName(), Msg.ObjectId, *Msg.GetObjectTypeFourCC(), Msg.BaselineType);
		return false;
	}

	return true;
}

REGISTER_SWG_BASELINE_HANDLER(FSWGTangibleBaselineHandler,
	ESWGObjectType::TANO, ESWGObjectType::WEAO)
