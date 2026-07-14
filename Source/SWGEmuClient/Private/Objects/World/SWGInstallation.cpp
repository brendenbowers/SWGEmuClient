#include "Objects/World/SWGInstallation.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"

ASWGInstallation::ASWGInstallation()
{
	PrimaryActorTick.bCanEverTick = false;

	TangibleComponent = CreateDefaultSubobject<USWGTangibleComponent>(TEXT("TangibleComponent"));
	ConditionComponent = CreateDefaultSubobject<USWGConditionComponent>(TEXT("ConditionComponent"));
	DefenderComponent = CreateDefaultSubobject<USWGDefenderComponent>(TEXT("DefenderComponent"));
}
