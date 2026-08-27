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

namespace
{
	// The loose fields are members of ASWGCreature rather than component state.
	void ApplyLooseFields(AActor& Actor, uint8 Slot, const FCreatureObjectBaseline& Baseline)
	{
		ASWGCreature* Creature = Cast<ASWGCreature>(&Actor);
		if (!Creature)
		{
			return;
		}

		switch (Slot)
		{
			case 1:
				Creature->BankCredits = Baseline.BankCredits;
				Creature->CashCredits = Baseline.CashCredits;
				break;
			case 3:
				Creature->CreatureLinkId = Baseline.CreatureLinkId;
				Creature->Height = Baseline.Height;
				break;
			case 6:
				Creature->Level = Baseline.Level;
				Creature->GuildId = Baseline.GuildId;
				break;
			default:
				break;
		}
	}

	bool ApplyCreatureBaseline(AActor& Actor, uint8 Slot, FSWGPacket& Packet)
	{
		FCreatureObjectBaseline Baseline;

		switch (Slot)
		{
			case 1:
			{
				SWGCreatureBaselineParser::ParseBase1(Packet, Baseline);

				USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>();
				USWGSkillComponent* SkillComponent = Actor.GetComponentByClass<USWGSkillComponent>();
				if (!HealthComponent || !SkillComponent)
				{
					return false;
				}

				HealthComponent->ApplyBase1(Baseline);
				SkillComponent->ApplyBase1(Baseline);
				break;
			}
			case 3:
			{
				SWGCreatureBaselineParser::ParseBase3(Packet, Baseline);

				USWGTangibleComponent* TangibleComponent = Actor.GetComponentByClass<USWGTangibleComponent>();
				USWGConditionComponent* ConditionComponent = Actor.GetComponentByClass<USWGConditionComponent>();
				USWGCombatStateComponent* CombatStateComponent = Actor.GetComponentByClass<USWGCombatStateComponent>();
				USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>();
				if (!TangibleComponent || !ConditionComponent || !CombatStateComponent || !HealthComponent)
				{
					return false;
				}

				TangibleComponent->ApplyBase3(Baseline.Tangible);
				ConditionComponent->ApplyBase3(Baseline.Tangible);
				CombatStateComponent->ApplyBase3(Baseline);
				HealthComponent->ApplyBase3(Baseline);
				break;
			}
			case 4:
			{
				SWGCreatureBaselineParser::ParseBase4(Packet, Baseline);

				// The movement component is the character's own, so it isn't found by class.
				ASWGCreature* Creature = Cast<ASWGCreature>(&Actor);
				USWGMovementComponent* Movement = Creature ? Creature->GetSWGMovementComponent() : nullptr;
				USWGEncumbranceComponent* EncumbranceComponent = Actor.GetComponentByClass<USWGEncumbranceComponent>();
				USWGSkillComponent* SkillComponent = Actor.GetComponentByClass<USWGSkillComponent>();
				USWGSpaceMissionComponent* SpaceMissionComponent = Actor.GetComponentByClass<USWGSpaceMissionComponent>();
				if (!Movement || !EncumbranceComponent || !SkillComponent || !SpaceMissionComponent)
				{
					return false;
				}

				Movement->ApplyBase4(Baseline);
				EncumbranceComponent->ApplyBase4(Baseline);
				SkillComponent->ApplyBase4(Baseline);
				SpaceMissionComponent->ApplyBase4(Baseline);
				break;
			}
			case 6:
			{
				SWGCreatureBaselineParser::ParseBase6(Packet, Baseline);

				USWGDefenderComponent* DefenderComponent = Actor.GetComponentByClass<USWGDefenderComponent>();
				USWGPerformanceComponent* PerformanceComponent = Actor.GetComponentByClass<USWGPerformanceComponent>();
				USWGCombatStateComponent* CombatStateComponent = Actor.GetComponentByClass<USWGCombatStateComponent>();
				USWGGroupComponent* GroupComponent = Actor.GetComponentByClass<USWGGroupComponent>();
				USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>();
				USWGEquipmentComponent* EquipmentComponent = Actor.GetComponentByClass<USWGEquipmentComponent>();
				if (!DefenderComponent || !PerformanceComponent || !CombatStateComponent || !GroupComponent || !HealthComponent || !EquipmentComponent)
				{
					return false;
				}

				DefenderComponent->ApplyBase6(Baseline.Tangible);
				PerformanceComponent->ApplyBase6(Baseline);
				CombatStateComponent->ApplyBase6(Baseline);
				GroupComponent->ApplyBase6(Baseline);
				HealthComponent->ApplyBase6(Baseline);
				EquipmentComponent->ApplyBase6(Baseline);
				break;
			}
			default:
				UE_LOG(LogTemp, Verbose, TEXT("FSWGCreatureBaselineHandler: no CREO baseline dispatch for slot %d"), Slot);
				return true;
		}

		ApplyLooseFields(Actor, Slot, Baseline);
		return true;
	}
}

bool FSWGCreatureBaselineHandler::CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const
{
	return true;
}

bool FSWGCreatureBaselineHandler::HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();

	if (!ApplyCreatureBaseline(Actor, Msg.BaselineType, Packet))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCreatureBaselineHandler: %s (object %lld) can't take a CREO slot %d baseline — missing components"),
			*Actor.GetClass()->GetName(), Msg.ObjectId, Msg.BaselineType);
		return false;
	}

	return true;
}

REGISTER_SWG_BASELINE_HANDLER(FSWGCreatureBaselineHandler, ESWGObjectType::CREO)
