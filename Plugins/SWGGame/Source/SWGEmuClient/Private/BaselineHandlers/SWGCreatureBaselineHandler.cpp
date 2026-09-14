#include "BaselineHandlers/SWGCreatureBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Creature/CreatureObjectBaseline.h"
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

bool FSWGCreatureBaselineHandler::CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const
{
	return true;
}

bool FSWGCreatureBaselineHandler::HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();
	FCreatureObjectBaseline Baseline;

	// The loose fields are members of ASWGCreature rather than component state.
	ASWGCreature* Creature = Cast<ASWGCreature>(&Actor);

	switch (Msg.BaselineType)
	{
		case 1:
			SWGCreatureBaselineParser::ParseBase1(Packet, Baseline);

			if (USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>())
			{
				HealthComponent->ApplyBase1(Baseline);
			}
			if (USWGSkillComponent* SkillComponent = Actor.GetComponentByClass<USWGSkillComponent>())
			{
				SkillComponent->ApplyBase1(Baseline);
			}
			if (Creature)
			{
				Creature->BankCredits = Baseline.BankCredits;
				Creature->CashCredits = Baseline.CashCredits;
			}
			break;

		case 3:
			SWGCreatureBaselineParser::ParseBase3(Packet, Baseline);

			if (USWGTangibleComponent* TangibleComponent = Actor.GetComponentByClass<USWGTangibleComponent>())
			{
				TangibleComponent->ApplyBase3(Baseline.Tangible);
			}
			if (USWGConditionComponent* ConditionComponent = Actor.GetComponentByClass<USWGConditionComponent>())
			{
				ConditionComponent->ApplyBase3(Baseline.Tangible);
			}
			if (USWGCombatStateComponent* CombatStateComponent = Actor.GetComponentByClass<USWGCombatStateComponent>())
			{
				CombatStateComponent->ApplyBase3(Baseline);
			}
			if (USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>())
			{
				HealthComponent->ApplyBase3(Baseline);
			}
			if (Creature)
			{
				Creature->CreatureLinkId = Baseline.CreatureLinkId;
				Creature->Height = Baseline.Height;
			}
			break;

		case 4:
			SWGCreatureBaselineParser::ParseBase4(Packet, Baseline);

			// The movement component is the character's own, so it isn't found by class.
			if (USWGMovementComponent* Movement = Creature ? Creature->GetSWGMovementComponent() : nullptr)
			{
				Movement->ApplyBase4(Baseline);
			}
			if (USWGEncumbranceComponent* EncumbranceComponent = Actor.GetComponentByClass<USWGEncumbranceComponent>())
			{
				EncumbranceComponent->ApplyBase4(Baseline);
			}
			if (USWGSkillComponent* SkillComponent = Actor.GetComponentByClass<USWGSkillComponent>())
			{
				SkillComponent->ApplyBase4(Baseline);
			}
			if (USWGSpaceMissionComponent* SpaceMissionComponent = Actor.GetComponentByClass<USWGSpaceMissionComponent>())
			{
				SpaceMissionComponent->ApplyBase4(Baseline);
			}
			break;

		case 6:
			SWGCreatureBaselineParser::ParseBase6(Packet, Baseline);

			if (USWGDefenderComponent* DefenderComponent = Actor.GetComponentByClass<USWGDefenderComponent>())
			{
				DefenderComponent->ApplyBase6(Baseline.Tangible);
			}
			if (USWGPerformanceComponent* PerformanceComponent = Actor.GetComponentByClass<USWGPerformanceComponent>())
			{
				PerformanceComponent->ApplyBase6(Baseline);
			}
			if (USWGCombatStateComponent* CombatStateComponent = Actor.GetComponentByClass<USWGCombatStateComponent>())
			{
				CombatStateComponent->ApplyBase6(Baseline);
			}
			if (USWGGroupComponent* GroupComponent = Actor.GetComponentByClass<USWGGroupComponent>())
			{
				GroupComponent->ApplyBase6(Baseline);
			}
			if (USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>())
			{
				HealthComponent->ApplyBase6(Baseline);
			}
			if (USWGEquipmentComponent* EquipmentComponent = Actor.GetComponentByClass<USWGEquipmentComponent>())
			{
				EquipmentComponent->ApplyBase6(Baseline);
			}
			if (Creature)
			{
				Creature->Level = Baseline.Level;
				Creature->GuildId = Baseline.GuildId;
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGCreatureBaselineHandler: no CREO baseline dispatch for slot %d"), Msg.BaselineType);
			break;
	}

	return true;
}

REGISTER_SWG_BASELINE_HANDLER(FSWGCreatureBaselineHandler, ESWGObjectType::CREO)
