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
	FSWGPacket Packet = Msg.AsPayloadPacket();
	FResourceContainerObjectBaseline Baseline;

	// The resource fields are members of ASWGItem rather than component state.
	ASWGItem* Item = Cast<ASWGItem>(&Actor);

	switch (Msg.BaselineType)
	{
		case 3:
			SWGResourceContainerBaselineParser::ParseBase3(Packet, Baseline);

			if (USWGTangibleComponent* TangibleComponent = Actor.GetComponentByClass<USWGTangibleComponent>())
			{
				TangibleComponent->ApplyBase3(Baseline.Tangible);
			}
			if (USWGConditionComponent* ConditionComponent = Actor.GetComponentByClass<USWGConditionComponent>())
			{
				ConditionComponent->ApplyBase3(Baseline.Tangible);
			}
			if (Item)
			{
				Item->ResourceQuantity = Baseline.Quantity;
			}
			break;

		case 6:
			SWGResourceContainerBaselineParser::ParseBase6(Packet, Baseline);

			if (Item)
			{
				Item->ResourceType = Baseline.ResourceType;
				Item->ResourceName = Baseline.ResourceName;
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGResourceContainerBaselineHandler: no RCNO baseline dispatch for slot %d"), Msg.BaselineType);
			break;
	}

	return true;
}

REGISTER_SWG_BASELINE_HANDLER(FSWGResourceContainerBaselineHandler, ESWGObjectType::RCNO)
