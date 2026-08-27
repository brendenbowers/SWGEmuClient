#include "DeltaHandlers/SWGCreatureDeltaHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Creature/CreatureObjectDelta.h"
#include "Objects/Creature/SWGCreature.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"
#include "Components/SWGHealthComponent.h"
#include "Components/SWGSkillComponent.h"
#include "Components/SWGEncumbranceComponent.h"
#include "Components/SWGSpaceMissionComponent.h"
#include "Components/SWGEquipmentComponent.h"
#include "Components/SWGCombatStateComponent.h"
#include "Components/SWGGroupComponent.h"
#include "Components/SWGPerformanceComponent.h"
#include "Components/SWGMovementComponent.h"

namespace
{
	// The loose fields are members of ASWGCreature rather than component state.
	void ApplyLooseFields(ASWGCreature* Creature, const FCreatureObjectDelta& Delta)
	{
		if (!Creature)
		{
			return;
		}

		if (Delta.BankCredits.IsSet())    { Creature->BankCredits = *Delta.BankCredits; }
		if (Delta.CashCredits.IsSet())    { Creature->CashCredits = *Delta.CashCredits; }
		if (Delta.CreatureLinkId.IsSet()) { Creature->CreatureLinkId = *Delta.CreatureLinkId; }
		if (Delta.Height.IsSet())         { Creature->Height = *Delta.Height; }
		if (Delta.Level.IsSet())          { Creature->Level = *Delta.Level; }
		if (Delta.GuildId.IsSet())        { Creature->GuildId = *Delta.GuildId; }
	}
}

bool FSWGCreatureDeltaHandler::CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const
{
	return true;
}

bool FSWGCreatureDeltaHandler::HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();
	FCreatureObjectDelta Delta;

	ASWGCreature* Creature = Cast<ASWGCreature>(&Actor);

	switch (Msg.DeltaType)
	{
		case 1:
			SWGCreatureDeltaParser::ParseDelta1(Packet, Delta, Msg.UpdateCount);

			if (USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>())
			{
				HealthComponent->ApplyDelta1(Delta);
			}
			if (USWGSkillComponent* SkillComponent = Actor.GetComponentByClass<USWGSkillComponent>())
			{
				SkillComponent->ApplyDelta1(Delta);
			}
			break;

		case 3:
			SWGCreatureDeltaParser::ParseDelta3(Packet, Delta, Msg.UpdateCount);

			if (USWGTangibleComponent* TangibleComponent = Actor.GetComponentByClass<USWGTangibleComponent>())
			{
				TangibleComponent->ApplyDelta3(Delta.Tangible);
			}
			if (USWGConditionComponent* ConditionComponent = Actor.GetComponentByClass<USWGConditionComponent>())
			{
				ConditionComponent->ApplyDelta3(Delta.Tangible);
			}
			if (USWGCombatStateComponent* CombatStateComponent = Actor.GetComponentByClass<USWGCombatStateComponent>())
			{
				CombatStateComponent->ApplyDelta3(Delta);
			}
			if (USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>())
			{
				HealthComponent->ApplyDelta3(Delta);
			}
			break;

		case 4:
			SWGCreatureDeltaParser::ParseDelta4(Packet, Delta, Msg.UpdateCount);

			// The movement component is the character's own, so it isn't found by class.
			if (USWGMovementComponent* Movement = Creature ? Creature->GetSWGMovementComponent() : nullptr)
			{
				Movement->ApplyDelta4(Delta);
			}
			if (USWGEncumbranceComponent* EncumbranceComponent = Actor.GetComponentByClass<USWGEncumbranceComponent>())
			{
				EncumbranceComponent->ApplyDelta4(Delta);
			}
			if (USWGSkillComponent* SkillComponent = Actor.GetComponentByClass<USWGSkillComponent>())
			{
				SkillComponent->ApplyDelta4(Delta);
			}
			if (USWGSpaceMissionComponent* SpaceMissionComponent = Actor.GetComponentByClass<USWGSpaceMissionComponent>())
			{
				SpaceMissionComponent->ApplyDelta4(Delta);
			}
			break;

		case 6:
			SWGCreatureDeltaParser::ParseDelta6(Packet, Delta, Msg.UpdateCount);

			if (USWGDefenderComponent* DefenderComponent = Actor.GetComponentByClass<USWGDefenderComponent>())
			{
				DefenderComponent->ApplyDelta6(Delta.Tangible);
			}
			if (USWGPerformanceComponent* PerformanceComponent = Actor.GetComponentByClass<USWGPerformanceComponent>())
			{
				PerformanceComponent->ApplyDelta6(Delta);
			}
			if (USWGCombatStateComponent* CombatStateComponent = Actor.GetComponentByClass<USWGCombatStateComponent>())
			{
				CombatStateComponent->ApplyDelta6(Delta);
			}
			if (USWGGroupComponent* GroupComponent = Actor.GetComponentByClass<USWGGroupComponent>())
			{
				GroupComponent->ApplyDelta6(Delta);
			}
			if (USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>())
			{
				HealthComponent->ApplyDelta6(Delta);
			}
			if (USWGEquipmentComponent* EquipmentComponent = Actor.GetComponentByClass<USWGEquipmentComponent>())
			{
				EquipmentComponent->ApplyDelta6(Delta);
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGCreatureDeltaHandler: no CREO delta dispatch for slot %d"), Msg.DeltaType);
			return true;
	}

	ApplyLooseFields(Creature, Delta);
	return true;
}

REGISTER_SWG_DELTA_HANDLER(FSWGCreatureDeltaHandler, ESWGObjectType::CREO)
