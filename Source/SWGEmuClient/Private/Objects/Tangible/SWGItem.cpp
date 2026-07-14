#include "Objects/Tangible/SWGItem.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"

ASWGItem::ASWGItem()
{
	PrimaryActorTick.bCanEverTick = false;

	TangibleComponent = CreateDefaultSubobject<USWGTangibleComponent>(TEXT("TangibleComponent"));
	ConditionComponent = CreateDefaultSubobject<USWGConditionComponent>(TEXT("ConditionComponent"));
	DefenderComponent = CreateDefaultSubobject<USWGDefenderComponent>(TEXT("DefenderComponent"));
}
