#include "Objects/World/SWGWaypointMarker.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"

namespace
{
	// /Engine/BasicShapes/Cone is a 100x100x100 unit box centered on its own
	// origin (confirmed via StaticMeshTools.get_bounds), not base-pivoted —
	// everything below divides by that 100 (or its 50 half-extent) to turn a
	// world-space size into the right component scale.
	constexpr float BasicShapeSize = 100.f;
	constexpr float BeaconRadius = 60.f;
	constexpr float BeaconHeight = 400.f;

	// appearance/path_arrow.msh — retail's own ground path-arrow mesh (the
	// same asset the newbie tutorial's follow-the-arrow guidance used).
	const TCHAR* PathArrowAppearancePath = TEXT("appearance/path_arrow.msh");
}

ASWGWaypointMarker::ASWGWaypointMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	UStaticMesh* ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UMaterialInterface* MarkerMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGWaypointMarker.M_SWGWaypointMarker"));

	Beacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beacon"));
	Beacon->SetupAttachment(GetRootComponent());
	if (ConeMesh)
	{
		Beacon->SetStaticMesh(ConeMesh);
	}
	// Lifted so the cone's base sits on the ground (the actor's own origin) and it points up, not centered through the ground.
	Beacon->SetRelativeLocation(FVector(0.f, 0.f, BeaconHeight * 0.5f));
	Beacon->SetRelativeScale3D(FVector(BeaconRadius * 2.f / BasicShapeSize, BeaconRadius * 2.f / BasicShapeSize, BeaconHeight / BasicShapeSize));
	Beacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beacon->SetCastShadow(false);
	Beacon->SetCanEverAffectNavigation(false);

	BeaconSpin = CreateDefaultSubobject<URotatingMovementComponent>(TEXT("BeaconSpin"));
	BeaconSpin->UpdatedComponent = Beacon;
	BeaconSpin->RotationRate = FRotator(0.f, 90.f, 0.f);

	// Mesh assigned once RequestAppearanceMesh resolves in BeginPlay — the
	// generated mesh pipeline needs the game instance's subsystems, not
	// available yet at CDO/constructor time.
	Breadcrumbs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Breadcrumbs"));
	Breadcrumbs->SetupAttachment(GetRootComponent());
	Breadcrumbs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Breadcrumbs->SetCastShadow(false);
	Breadcrumbs->SetCanEverAffectNavigation(false);

	if (MarkerMaterial)
	{
		BeaconMID = UMaterialInstanceDynamic::Create(MarkerMaterial, this);
		Beacon->SetMaterial(0, BeaconMID);
	}

	SetActorEnableCollision(false);
}

void ASWGWaypointMarker::BeginPlay()
{
	Super::BeginPlay();

	USWGMeshGeneratorSubsystem* MeshGenerator = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	if (!MeshGenerator)
	{
		return;
	}

	TWeakObjectPtr<ASWGWaypointMarker> WeakThis(this);
	MeshGenerator->RequestAppearanceMesh(PathArrowAppearancePath, [WeakThis](UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
	{
		ASWGWaypointMarker* Marker = WeakThis.Get();
		if (!Marker || !Mesh)
		{
			return;
		}

		Marker->Breadcrumbs->SetStaticMesh(Mesh);
		Marker->BreadcrumbMIDs.Reset();
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			UMaterialInstanceDynamic* MID = USWGMeshGeneratorSubsystem::CreateOwnedMaterialCopy(Materials[Index], Marker);
			if (MID)
			{
				Marker->Breadcrumbs->SetMaterial(Index, MID);
				Marker->BreadcrumbMIDs.Add(MID);
			}
		}
		Marker->SetColor(Marker->PendingColor);

		Marker->bBreadcrumbMeshReady = true;
		if (!Marker->PendingBreadcrumbTransforms.IsEmpty())
		{
			Marker->SetBreadcrumbPoints(Marker->PendingBreadcrumbTransforms);
		}
	});
}

void ASWGWaypointMarker::SetColor(FLinearColor Color)
{
	PendingColor = Color;
	if (BeaconMID)
	{
		BeaconMID->SetVectorParameterValue(TEXT("Color"), Color);
	}
	for (UMaterialInstanceDynamic* MID : BreadcrumbMIDs)
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

void ASWGWaypointMarker::SetBreadcrumbPoints(const TArray<FTransform>& WorldTransforms)
{
	if (!bBreadcrumbMeshReady)
	{
		// Remember the most recent request; BeginPlay's callback replays it once the mesh lands.
		PendingBreadcrumbTransforms = WorldTransforms;
		return;
	}

	Breadcrumbs->ClearInstances();
	for (const FTransform& Transform : WorldTransforms)
	{
		Breadcrumbs->AddInstance(Transform, /*bWorldSpace=*/true);
	}
}
