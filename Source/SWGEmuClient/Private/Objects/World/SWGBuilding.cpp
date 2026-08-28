#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "Objects/Player/SWGPlayer.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"

ASWGBuilding::ASWGBuilding()
{
	PrimaryActorTick.bCanEverTick = false;

	TangibleComponent = CreateDefaultSubobject<USWGTangibleComponent>(TEXT("TangibleComponent"));
	ConditionComponent = CreateDefaultSubobject<USWGConditionComponent>(TEXT("ConditionComponent"));
	DefenderComponent = CreateDefaultSubobject<USWGDefenderComponent>(TEXT("DefenderComponent"));
}

void ASWGBuilding::RegisterCellTrigger(ASWGCell* Cell, bool bCanSeeParent)
{
	if (!Cell || !Cell->TriggerVolume)
	{
		UE_LOG(LogTemp, Warning, TEXT("ASWGBuilding::RegisterCellTrigger: %s — no trigger volume for cell %s"),
			*GetName(), Cell ? *Cell->GetName() : TEXT("null"));
		return;
	}

	CellSeeParentByTrigger.Add(Cell->TriggerVolume, bCanSeeParent);

	Cell->TriggerVolume->OnComponentBeginOverlap.AddDynamic(this, &ASWGBuilding::OnCellTriggerBeginOverlap);
	Cell->TriggerVolume->OnComponentEndOverlap.AddDynamic(this, &ASWGBuilding::OnCellTriggerEndOverlap);
}

void ASWGBuilding::OnCellTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Local player only — this is a client-side view effect, not gameplay state.
	if (!Cast<ASWGPlayer>(OtherActor))
	{
		return;
	}

	const bool* bCanSeeParent = CellSeeParentByTrigger.Find(OverlappedComponent);
	if (bCanSeeParent && !*bCanSeeParent)
	{
		if (++NonSeeThroughOverlapCount == 1)
		{
			SetExteriorShellHidden(true);
		}
	}
}

void ASWGBuilding::OnCellTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!Cast<ASWGPlayer>(OtherActor))
	{
		return;
	}

	const bool* bCanSeeParent = CellSeeParentByTrigger.Find(OverlappedComponent);
	if (bCanSeeParent && !*bCanSeeParent)
	{
		if (--NonSeeThroughOverlapCount <= 0)
		{
			NonSeeThroughOverlapCount = 0;
			SetExteriorShellHidden(false);
		}
	}
}

void ASWGBuilding::SetExteriorShellHidden(bool bShouldHide)
{
	// Visibility only
	SetActorHiddenInGame(bShouldHide);
}
