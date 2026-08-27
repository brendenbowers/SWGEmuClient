#include "BaselineHandlers/SWGTangibleBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Object/TangibleObjectBaseline.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"

namespace
{
	bool ApplyTangibleBaseline(AActor& Actor, uint8 Slot, FSWGPacket& Packet)
	{
		FTangibleObjectBaseline Baseline;

		switch (Slot)
		{
			case 3:
			{
				SWGTangibleBaselineParser::ParseBase3(Packet, Baseline);

				USWGTangibleComponent* TangibleComponent = Actor.GetComponentByClass<USWGTangibleComponent>();
				USWGConditionComponent* ConditionComponent = Actor.GetComponentByClass<USWGConditionComponent>();
				if (!TangibleComponent || !ConditionComponent)
				{
					return false;
				}

				TangibleComponent->ApplyBase3(Baseline);
				ConditionComponent->ApplyBase3(Baseline);
				return true;
			}
			case 6:
			{
				SWGTangibleBaselineParser::ParseBase6(Packet, Baseline);

				USWGDefenderComponent* DefenderComponent = Actor.GetComponentByClass<USWGDefenderComponent>();
				if (!DefenderComponent)
				{
					return false;
				}

				DefenderComponent->ApplyBase6(Baseline);
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
		UE_LOG(LogTemp, Warning, TEXT("FSWGTangibleBaselineHandler: %s (object %lld) can't take a %s slot %d baseline — missing components"),
			*Actor.GetClass()->GetName(), Msg.ObjectId, *Msg.GetObjectTypeFourCC(), Msg.BaselineType);
		return false;
	}

	return true;
}

REGISTER_SWG_BASELINE_HANDLER(FSWGTangibleBaselineHandler,
	ESWGObjectType::TANO, ESWGObjectType::WEAO)
