#include "BaselineHandlers/SWGResourceContainerBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Objects/Tangible/SWGItem.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"

bool FSWGResourceContainerBaselineHandler::CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const
{
	// just the registraio of RCNO should be enough of a filter
	return true;
}

bool FSWGResourceContainerBaselineHandler::HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args)
{
	ASWGItem* Item = Cast<ASWGItem>(&Actor);
	if (!Item)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGResourceContainerBaselineHandler: RCNO baseline for object %lld landed on %s, not an ASWGItem — skipping slot %d"),
			Msg.ObjectId, *Actor.GetClass()->GetName(), Msg.BaselineType);
		return false;
	}

	FSWGPacket Packet = Msg.AsPayloadPacket();

	switch (Msg.BaselineType)
	{
		case 3:
		{
			USWGTangibleComponent* TangibleComponent = Item->GetComponentByClass<USWGTangibleComponent>();
			USWGConditionComponent* ConditionComponent = Item->GetComponentByClass<USWGConditionComponent>();
			if (!TangibleComponent || !ConditionComponent)
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGResourceContainerBaselineHandler: %s (object %lld) can't take an RCNO slot 3 baseline — missing components"),
					*Actor.GetClass()->GetName(), Msg.ObjectId);
				return false;
			}

			TangibleComponent->ApplyBase3Part1(Packet);
			ConditionComponent->ApplyBase3(Packet);
			TangibleComponent->ApplyBase3Part2(Packet);
			Item->ResourceQuantity = Packet.ReadInt32();
			Packet.ReadInt64(); // spawnID
			return true;
		}

		case 6:
			Packet.ReadAsciiString();  // unused, server sends ""
			Packet.ReadInt32();
			Packet.ReadAsciiString();  // unused, server sends ""
			Packet.ReadUnicodeString();
			Packet.ReadInt32();        // max stack size
			Item->ResourceType = Packet.ReadAsciiString();
			Item->ResourceName = Packet.ReadUnicodeString();
			return true;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGResourceContainerBaselineHandler: no RCNO baseline dispatch for slot %d"), Msg.BaselineType);
			return true;
	}
}

REGISTER_SWG_BASELINE_HANDLER(FSWGResourceContainerBaselineHandler, ESWGObjectType::RCNO)
