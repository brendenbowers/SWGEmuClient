#include "BaselineHandlers/SWGResourceContainerBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Resource/ResourceContainerObjectBaseline.h"
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
	FResourceContainerObjectBaseline Baseline;

	switch (Msg.BaselineType)
	{
		case 3:
		{
			SWGResourceContainerBaselineParser::ParseBase3(Packet, Baseline);

			USWGTangibleComponent* TangibleComponent = Item->GetComponentByClass<USWGTangibleComponent>();
			USWGConditionComponent* ConditionComponent = Item->GetComponentByClass<USWGConditionComponent>();
			if (!TangibleComponent || !ConditionComponent)
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGResourceContainerBaselineHandler: %s (object %lld) can't take an RCNO slot 3 baseline — missing components"),
					*Actor.GetClass()->GetName(), Msg.ObjectId);
				return false;
			}

			TangibleComponent->ApplyBase3(Baseline.Tangible);
			ConditionComponent->ApplyBase3(Baseline.Tangible);
			Item->ResourceQuantity = Baseline.Quantity;
			return true;
		}

		case 6:
			SWGResourceContainerBaselineParser::ParseBase6(Packet, Baseline);
			Item->ResourceType = Baseline.ResourceType;
			Item->ResourceName = Baseline.ResourceName;
			return true;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGResourceContainerBaselineHandler: no RCNO baseline dispatch for slot %d"), Msg.BaselineType);
			return true;
	}
}

REGISTER_SWG_BASELINE_HANDLER(FSWGResourceContainerBaselineHandler, ESWGObjectType::RCNO)
