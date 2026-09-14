#include "DeltaHandlers/SWGResourceContainerDeltaHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Resource/ResourceContainerObjectDelta.h"
#include "Objects/Tangible/SWGItem.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"

bool FSWGResourceContainerDeltaHandler::CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const
{
	return true;
}

bool FSWGResourceContainerDeltaHandler::HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();
	FResourceContainerObjectDelta Delta;

	// The resource fields are members of ASWGItem rather than component state.
	ASWGItem* Item = Cast<ASWGItem>(&Actor);

	switch (Msg.DeltaType)
	{
		case 3:
			SWGResourceContainerDeltaParser::ParseDelta3(Packet, Delta, Msg.UpdateCount);

			if (USWGTangibleComponent* TangibleComponent = Actor.GetComponentByClass<USWGTangibleComponent>())
			{
				TangibleComponent->ApplyDelta3(Delta.Tangible);
			}
			if (USWGConditionComponent* ConditionComponent = Actor.GetComponentByClass<USWGConditionComponent>())
			{
				ConditionComponent->ApplyDelta3(Delta.Tangible);
			}
			if (Item && Delta.Quantity.IsSet())
			{
				Item->ResourceQuantity = *Delta.Quantity;
			}
			break;

		case 6:
			SWGResourceContainerDeltaParser::ParseDelta6(Packet, Delta, Msg.UpdateCount);

			if (Item)
			{
				if (Delta.ResourceType.IsSet()) { Item->ResourceType = *Delta.ResourceType; }
				if (Delta.ResourceName.IsSet()) { Item->ResourceName = *Delta.ResourceName; }
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGResourceContainerDeltaHandler: no RCNO delta dispatch for slot %d"), Msg.DeltaType);
			break;
	}

	return true;
}

REGISTER_SWG_DELTA_HANDLER(FSWGResourceContainerDeltaHandler, ESWGObjectType::RCNO)
