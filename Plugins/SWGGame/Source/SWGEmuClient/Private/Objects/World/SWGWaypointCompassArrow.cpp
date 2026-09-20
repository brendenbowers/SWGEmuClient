#include "Objects/World/SWGWaypointCompassArrow.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGWaypointCompassArrow, Log, All);

namespace
{
	// Same retail asset ASWGWaypointMarker's ground trail uses.
	const TCHAR* PathArrowAppearancePath = TEXT("appearance/path_arrow.msh");
}

ASWGWaypointCompassArrow::ASWGWaypointCompassArrow()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Arrow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(GetRootComponent());
	Arrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Arrow->SetCastShadow(false);
	Arrow->SetCanEverAffectNavigation(false);

	SetActorEnableCollision(false);
}

void ASWGWaypointCompassArrow::BeginPlay()
{
	Super::BeginPlay();

	USWGMeshGeneratorSubsystem* MeshGenerator = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	if (!MeshGenerator)
	{
		UE_LOG(LogSWGWaypointCompassArrow, Warning, TEXT("BeginPlay: no USWGMeshGeneratorSubsystem — mesh request never made"));
		return;
	}

	TWeakObjectPtr<ASWGWaypointCompassArrow> WeakThis(this);
	MeshGenerator->RequestAppearanceMesh(PathArrowAppearancePath, [WeakThis](UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
	{
		ASWGWaypointCompassArrow* CompassArrow = WeakThis.Get();
		if (!CompassArrow || !Mesh)
		{
			return;
		}

		CompassArrow->Arrow->SetStaticMesh(Mesh);
		CompassArrow->Arrow->SetRelativeScale3D(FVector(CompassArrow->ArrowScale));

		CompassArrow->ArrowMIDs.Reset();
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			UMaterialInstanceDynamic* MID = Materials[Index] ? UMaterialInstanceDynamic::Create(Materials[Index], CompassArrow) : nullptr;
			if (MID)
			{
				CompassArrow->Arrow->SetMaterial(Index, MID);
				CompassArrow->ArrowMIDs.Add(MID);
			}
		}
		CompassArrow->SetColor(CompassArrow->PendingColor);

		// path_arrow.msh is authored centered on itself, tip and tail straddling
		// its own origin — but the actor's origin is the player's feet, and the
		// ask is a tail there with the head reaching out toward the waypoint.
		// SWG's +z (native forward) always lands on UE +X (see
		// Common/SWGWorldScale.h), so the mesh's local +X bound is its head and
		// -X bound its tail; shifting the component so that tail sits at the
		// actor's origin needs no guessed offset, just the mesh's own (scaled)
		// bounds.
		const FBox Bounds = Mesh->GetBoundingBox();
		CompassArrow->Arrow->SetRelativeLocation(FVector(-Bounds.Min.X * CompassArrow->ArrowScale, 0.f, 0.f));
	});
}

void ASWGWaypointCompassArrow::SetHeadingDegrees(float YawDegrees)
{
	const FRotator Heading(0.f, YawDegrees, 0.f);
	SetActorRotation(Heading);
	// Offset along the heading, not just a fixed world-space nudge — it should
	// stay ahead of the player (clear of the character model) whichever way
	// the waypoint is, not just to one fixed side.
	AddActorWorldOffset(Heading.Vector() * ForwardOffset);
}

void ASWGWaypointCompassArrow::SetColor(FLinearColor Color)
{
	PendingColor = Color;
	for (UMaterialInstanceDynamic* MID : ArrowMIDs)
	{
		if (MID)
		{
			// Best-effort: USWGMeshGeneratorSubsystem's palette-recolour hookup —
			// a shader with neither parameter just ignores these silently.
			MID->SetVectorParameterValue(TEXT("TintColor"), Color);
			MID->SetVectorParameterValue(TEXT("TintColor2"), Color);
		}
	}
}
