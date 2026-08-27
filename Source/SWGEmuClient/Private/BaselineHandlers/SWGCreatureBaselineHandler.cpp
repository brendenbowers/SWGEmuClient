#include "BaselineHandlers/SWGCreatureBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
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
	bool ApplyCreatureBaseline(AActor& Actor, uint8 Slot, FSWGPacket& Packet)
	{

		// The some fields are members of ASWGCreature rather than component state
		ASWGCreature* Creature = Cast<ASWGCreature>(&Actor);

		switch (Slot)
		{
			case 1:
			{
				USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>();
				USWGSkillComponent* SkillComponent = Actor.GetComponentByClass<USWGSkillComponent>();
				if (!HealthComponent || !SkillComponent)
				{
					return false;
				}

				if (Creature)
				{
					Creature->BankCredits = Packet.ReadInt32();
					Creature->CashCredits = Packet.ReadInt32();
				}
				else
				{
					Packet.Skip(sizeof(int32) * 2);
				}

				HealthComponent->ApplyBase1(Packet);
				SkillComponent->ApplyBase1(Packet);
				return true;
			}
			case 3:
			{
				USWGTangibleComponent* TangibleComponent = Actor.GetComponentByClass<USWGTangibleComponent>();
				USWGConditionComponent* ConditionComponent = Actor.GetComponentByClass<USWGConditionComponent>();
				USWGCombatStateComponent* CombatStateComponent = Actor.GetComponentByClass<USWGCombatStateComponent>();
				USWGHealthComponent* HealthComponent = Actor.GetComponentByClass<USWGHealthComponent>();
				if (!TangibleComponent || !ConditionComponent || !CombatStateComponent || !HealthComponent)
				{
					return false;
				}

				//TangibleObjectMessage3 fields come first on the wire.
				TangibleComponent->ApplyBase3Part1(Packet);
				ConditionComponent->ApplyBase3(Packet);
				TangibleComponent->ApplyBase3Part2(Packet);
				CombatStateComponent->ApplyBase3Part1(Packet);
				if (Creature)
				{
					Creature->CreatureLinkId = Packet.ReadInt64();
					Creature->Height = Packet.ReadFloat();
				}
				else
				{
					Packet.Skip(sizeof(int64) + sizeof(float));
				}
				HealthComponent->ApplyBase3Part1(Packet);
				CombatStateComponent->ApplyBase3Part2(Packet);
				HealthComponent->ApplyBase3Part2(Packet);
				return true;
			}
			case 4:
			{
				// The movement component IS the character's movement component, so it
				// comes off ASWGCreature rather than a component search.
				USWGMovementComponent* Movement = Actor.GetComponentByClass<USWGMovementComponent>();
				USWGEncumbranceComponent* EncumbranceComponent = Actor.GetComponentByClass<USWGEncumbranceComponent>();
				USWGSkillComponent* SkillComponent = Actor.GetComponentByClass<USWGSkillComponent>();
				USWGSpaceMissionComponent* SpaceMissionComponent = Actor.GetComponentByClass<USWGSpaceMissionComponent>();
				if (!Movement || !EncumbranceComponent || !SkillComponent || !SpaceMissionComponent)
				{
					return false;
				}

				Movement->ApplyBase4Part1(Packet);
				EncumbranceComponent->ApplyBase4(Packet);
				SkillComponent->ApplyBase4(Packet);
				Movement->ApplyBase4Part2(Packet);
				SpaceMissionComponent->ApplyBase4Part1(Packet);
				Movement->ApplyBase4Part3(Packet);
				SpaceMissionComponent->ApplyBase4Part2(Packet);
				return true;
			}
			case 6:
			{
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

				//TangibleObjectMessage6 fields (Unknown076 + DefenderList) come first.
				DefenderComponent->ApplyBase6(Packet);
				if (Creature)
				{
					Creature->Level = Packet.ReadUInt16();
				}
				else
				{
					Packet.Skip(sizeof(uint16));
				}
				PerformanceComponent->ApplyBase6Part1(Packet);
				CombatStateComponent->ApplyBase6Part1(Packet);
				GroupComponent->ApplyBase6(Packet);
				if (Creature)
				{
					Creature->GuildId = Packet.ReadInt32();
				}
				else
				{
					Packet.Skip(sizeof(int32));
				}
				CombatStateComponent->ApplyBase6Part2(Packet);
				PerformanceComponent->ApplyBase6Part2(Packet);
				HealthComponent->ApplyBase6(Packet);
				EquipmentComponent->ApplyBase6(Packet);
				CombatStateComponent->ApplyBase6Part3(Packet);
				return true;
			}
			default:
				UE_LOG(LogTemp, Verbose, TEXT("FSWGCreatureBaselineHandler: no CREO baseline dispatch for slot %d"), Slot);
				return true;
		}
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
