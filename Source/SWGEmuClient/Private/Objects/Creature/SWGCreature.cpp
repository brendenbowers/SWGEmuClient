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

ASWGCreature::ASWGCreature(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USWGMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = false;

	TangibleComponent = CreateDefaultSubobject<USWGTangibleComponent>(TEXT("TangibleComponent"));
	ConditionComponent = CreateDefaultSubobject<USWGConditionComponent>(TEXT("ConditionComponent"));
	DefenderComponent = CreateDefaultSubobject<USWGDefenderComponent>(TEXT("DefenderComponent"));

	HealthComponent = CreateDefaultSubobject<USWGHealthComponent>(TEXT("HealthComponent"));
	SkillComponent = CreateDefaultSubobject<USWGSkillComponent>(TEXT("SkillComponent"));
	EncumbranceComponent = CreateDefaultSubobject<USWGEncumbranceComponent>(TEXT("EncumbranceComponent"));
	SpaceMissionComponent = CreateDefaultSubobject<USWGSpaceMissionComponent>(TEXT("SpaceMissionComponent"));
	EquipmentComponent = CreateDefaultSubobject<USWGEquipmentComponent>(TEXT("EquipmentComponent"));
	CombatStateComponent = CreateDefaultSubobject<USWGCombatStateComponent>(TEXT("CombatStateComponent"));
	GroupComponent = CreateDefaultSubobject<USWGGroupComponent>(TEXT("GroupComponent"));
	PerformanceComponent = CreateDefaultSubobject<USWGPerformanceComponent>(TEXT("PerformanceComponent"));
}

USWGMovementComponent* ASWGCreature::GetSWGMovementComponent() const
{
	return Cast<USWGMovementComponent>(GetCharacterMovement());
}
