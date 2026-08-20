

#include "Objects\World\SWGDoor.h"
#include "TRE/SWGDoorStyleRow.h"
#include "Common/SWGWorldScale.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Pawn.h"

ASWGDoor::ASWGDoor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;

    TriggerVolume = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerVolume"));
    TriggerVolume->SetupAttachment(GetRootComponent());
    TriggerVolume->SetSphereRadius(300.0f); // overwritten by InitializeDoorStyle once the style row is known
    TriggerVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    TriggerVolume->SetCollisionObjectType(ECC_WorldDynamic);
    TriggerVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    TriggerVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    TriggerVolume->OnComponentBeginOverlap.AddDynamic(this, &ASWGDoor::OnTriggerBeginOverlap);
    TriggerVolume->OnComponentEndOverlap.AddDynamic(this, &ASWGDoor::OnTriggerEndOverlap);
}

void ASWGDoor::InitializeDoorStyle(const FSWGDoorStyleRow* StyleRow)
{
    if (RootComponent)
    {
        ClosedRelativeTransform = RootComponent->GetRelativeTransform();
    }

    if (!StyleRow)
    {
        return;
    }

    // FSWGDoorStyleRow stores door_style.iff's raw values, in meters like
    // every other SWG file — SWGWorldScale (100) converts to UE's
    // centimeters here, at the point of use, same as every other reader.
    OpenOffset = StyleRow->MoveOffset * SWGWorldScale;
    OpenTime = StyleRow->OpenTime;
    CloseTime = StyleRow->CloseTime;
    Springiness = StyleRow->Springiness;
    Smoothness = StyleRow->Smoothness;

    if (TriggerVolume)
    {
        TriggerVolume->SetSphereRadius(FMath::Max(StyleRow->TriggerRadius * SWGWorldScale, SWGWorldScale));
    }

    // RawMoveOffset is door_style.iff's untouched doorMoveX/Y/Z (pre
    // axis-swap, pre world scale), so it stays directly comparable to a raw
    // dump of the table.
    const AActor* OwningBuildingActor = GetAttachParentActor();
    UE_LOG(LogTemp, Verbose, TEXT("ASWGDoor::InitializeDoorStyle: %s portal=%d building=%s RawMoveOffset=%s OpenOffset(ue,cm)=%s ClosedRelLoc=%s ClosedRelRot=%s BuildingWorldRot=%s"),
        *GetName(), PortalNumber,
        OwningBuildingActor ? *OwningBuildingActor->GetName() : TEXT("<none>"),
        *StyleRow->MoveOffset.ToString(),
        *OpenOffset.ToString(),
        *ClosedRelativeTransform.GetLocation().ToString(),
        *ClosedRelativeTransform.GetRotation().Rotator().ToString(),
        OwningBuildingActor ? *OwningBuildingActor->GetActorRotation().ToString() : TEXT("<none>"));
}

void ASWGDoor::Open()
{
    if (bIsOpen)
    {
        return;
    }
    bIsOpen = true;
    bMoving = true;
    SetActorTickEnabled(true);

    const FVector LocalDelta = ClosedRelativeTransform.TransformVectorNoScale(OpenOffset);
    UE_LOG(LogTemp, Verbose, TEXT("ASWGDoor::Open: %s LocalDelta(buildingSpace)=%s DoorWorldLoc=%s DoorWorldRot=%s"),
        *GetName(), *LocalDelta.ToString(), *GetActorLocation().ToString(), *GetActorRotation().ToString());
}

void ASWGDoor::Close()
{
    if (!bIsOpen)
    {
        return;
    }
    bIsOpen = false;
    bMoving = true;
    SetActorTickEnabled(true);
}

void ASWGDoor::ToggleOpen()
{
    if (bIsOpen)
    {
        Close();
    }
    else
    {
        Open();
    }
}

void ASWGDoor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bMoving)
    {
        return;
    }

    const float Duration = FMath::Max(bIsOpen ? OpenTime : CloseTime, KINDA_SMALL_NUMBER);
    const float Speed = 1.0f / Duration;
    MoveAlpha = FMath::Clamp(MoveAlpha + (bIsOpen ? Speed : -Speed) * DeltaTime, 0.0f, 1.0f);

    const float Exponent = FMath::Clamp(Smoothness * 2.0f, 0.5f, 4.0f);
    const float Eased = FMath::InterpEaseInOut(0.0f, 1.0f, MoveAlpha, Exponent);

    FTransform Target = ClosedRelativeTransform;
    Target.AddToTranslation(ClosedRelativeTransform.TransformVectorNoScale(OpenOffset) * Eased);
    SetActorRelativeTransform(Target);

    if ((bIsOpen && MoveAlpha >= 1.0f) || (!bIsOpen && MoveAlpha <= 0.0f))
    {
        bMoving = false;
        SetActorTickEnabled(false);
    }
}

void ASWGDoor::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!OtherActor || !OtherActor->IsA<APawn>())
    {
        return;
    }

    if (++OverlappingPawnCount == 1)
    {
        Open();
    }
}

void ASWGDoor::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    if (!OtherActor || !OtherActor->IsA<APawn>())
    {
        return;
    }

    if (--OverlappingPawnCount <= 0)
    {
        OverlappingPawnCount = 0;
        Close();
    }
}
