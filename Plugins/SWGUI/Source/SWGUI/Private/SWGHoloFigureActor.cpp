#include "SWGHoloFigureActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	enum EFigureRay : int32 { HeadRay, LeftShoulderRay, RightShoulderRay, LeftFootRay, RightFootRay, FigureRayCount };

	/** Fractions of the height from the toes to the head joint; the clavicles and upper arms sit at 0.9 or so. */
	constexpr float ShoulderBandLow = 0.82f;
	constexpr float ShoulderBandHigh = 0.96f;
	constexpr float FootBandHigh = 0.12f;
	/** The head joint is the base of the skull; the crown is about this much higher. */
	constexpr float CrownAboveHeadJoint = 0.1f;
	/** Half the height band a sweep ray looks in for the figure's edge. */
	constexpr float SweepBandHalf = 0.08f;

	TAutoConsoleVariable<bool> CVarShowFigureTargets(
		TEXT("swg.HoloFigure.ShowTargets"),
		false,
		TEXT("Draws where the holo figure's projection rays land (head, shoulders, feet)."));

	/** Equip slot (by prefix) to the bone it hangs from, in the humanoid skeleton's joint names. First match wins. */
	const TPair<const TCHAR*, const TCHAR*> SlotBones[] = {
		{ TEXT("hat"), TEXT("head") }, { TEXT("hair"), TEXT("head") }, { TEXT("eyes"), TEXT("head") }, { TEXT("earring_l"), TEXT("head") }, { TEXT("earring_r"), TEXT("head") },
		{ TEXT("neck"), TEXT("neck") },
		{ TEXT("chest"), TEXT("spine3") }, { TEXT("back"), TEXT("spine3") }, { TEXT("cloak"), TEXT("spine3") }, { TEXT("bandolier"), TEXT("spine3") },
		{ TEXT("bicep_l"), TEXT("lArm") }, { TEXT("bicep_r"), TEXT("rArm") },
		{ TEXT("bracer_upper_l"), TEXT("lForeArm") }, { TEXT("bracer_upper_r"), TEXT("rForeArm") },
		{ TEXT("bracer_lower_l"), TEXT("lUlna") }, { TEXT("bracer_lower_r"), TEXT("rUlna") },
		{ TEXT("wrist_l"), TEXT("lWrist") }, { TEXT("wrist_r"), TEXT("rWrist") }, { TEXT("gloves"), TEXT("rWrist") },
		{ TEXT("ring_l"), TEXT("lRing01") }, { TEXT("ring_r"), TEXT("rRing01") },
		{ TEXT("utility_belt"), TEXT("spine1") }, { TEXT("mission_bag"), TEXT("spine1") },
		{ TEXT("pants"), TEXT("lThigh") }, { TEXT("shoes"), TEXT("lAnkle") },
		{ TEXT("hold_l"), TEXT("hold_l") }, { TEXT("hold_r"), TEXT("hold_r") },
	};
	const FName FallbackBone(TEXT("spine2"));

	bool ShouldCopy(const UPrimitiveComponent& Component)
	{
		return Component.IsVisible() && !Component.bHiddenInGame;
	}

	void MakeQuiet(UPrimitiveComponent& Copy)
	{
		Copy.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Copy.SetCastShadow(false);
	}
}

ASWGHoloFigureActor::ASWGHoloFigureActor()
{
	FigureRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Figure"));
	FigureRoot->SetupAttachment(GetRootComponent());
	DiscDiameter = 90.f;
	DroidScale = 1.8f;
	DroidReach = 0.85f;
	// Overlapping clothing layers add up under an additive material.
	HoloIntensity = 0.45f;
	ProjectionRayCount = FigureRayCount;
	// Shorter rays than the map's, seen from about as far; thicker so they still read.
	RayWidth = 1.2f;
	// Behind the figure and off to one side, so it never sits right behind the head from the viewer.
	DroidSide = FVector2D(-1.f, 0.55f).GetSafeNormal();
}

void ASWGHoloFigureActor::SetSource(ACharacter* InSource)
{
	Source = InSource;
	Rebuild();
}

void ASWGHoloFigureActor::SetFigureYaw(float Degrees)
{
	FigureYaw = FRotator::NormalizeAxis(Degrees);
	FigureRoot->SetRelativeRotation(FRotator(0.f, FigureYaw, 0.f));
}

UMeshComponent* ASWGHoloFigureActor::FindCopy(const UPrimitiveComponent* SourceComponent) const
{
	if (!SourceComponent)
	{
		return nullptr;
	}
	const int32 Index = CopySources.IndexOfByPredicate([SourceComponent](const TWeakObjectPtr<const UPrimitiveComponent>& Candidate) { return Candidate.Get() == SourceComponent; });
	return Index != INDEX_NONE ? Copies[Index].Get() : nullptr;
}

TArray<TPair<const UObject*, const UObject*>> ASWGHoloFigureActor::SourceSignature() const
{
	TArray<TPair<const UObject*, const UObject*>> Signature;
	const ACharacter* Character = Source.Get();
	const USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	if (!Body)
	{
		return Signature;
	}
	TArray<UMeshComponent*> Meshes;
	Character->GetComponents(Meshes);
	for (const UMeshComponent* Mesh : Meshes)
	{
		if (Mesh != Body && (!Mesh->IsAttachedTo(Body) || !ShouldCopy(*Mesh)))
		{
			continue;
		}
		if (const USkeletalMeshComponent* Skinned = Cast<USkeletalMeshComponent>(Mesh))
		{
			Signature.Emplace(Skinned, Skinned->GetSkeletalMeshAsset());
		}
		else if (const UStaticMeshComponent* Rigid = Cast<UStaticMeshComponent>(Mesh))
		{
			Signature.Emplace(Rigid, Rigid->GetStaticMesh());
		}
	}
	return Signature;
}

void ASWGHoloFigureActor::ClearCopies()
{
	for (UMeshComponent* Copy : Copies)
	{
		if (Copy)
		{
			Copy->DestroyComponent();
		}
	}
	Copies.Reset();
	CopySources.Reset();
	BodyCopy = nullptr;
	HighlightedCopy = nullptr;
}

void ASWGHoloFigureActor::Rebuild()
{
	ClearCopies();
	BuiltSignature = SourceSignature();
	NextSourceScanTime = FPlatformTime::Seconds() + SourceScanSeconds;
	ACharacter* Character = Source.Get();
	USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	if (!Body || !Body->GetSkeletalMeshAsset())
	{
		return;
	}
	if (!FigureMaterial)
	{
		FigureMaterial = MakeHoloMaterial(HoloColor, HoloIntensity);
	}
	FigureRoot->SetRelativeScale3D(FVector(FigureScale));
	FigureRoot->SetRelativeLocation(FVector::ZeroVector);
	bGrounded = false;

	auto CopySkinned = [this, Body](const USkeletalMeshComponent& Original, USceneComponent* Parent)
	{
		USkeletalMeshComponent* Copy = NewObject<USkeletalMeshComponent>(this);
		MakeQuiet(*Copy);
		Copy->SetSkeletalMeshAsset(Original.GetSkeletalMeshAsset());
		for (int32 MaterialIndex = 0; MaterialIndex < Copy->GetNumMaterials(); ++MaterialIndex)
		{
			Copy->SetMaterial(MaterialIndex, FigureMaterial);
		}
		Copy->SetupAttachment(Parent);
		Copy->SetRelativeTransform(Original.GetRelativeTransform());
		Copy->RegisterComponent();
		// After both meshes are set: the bone map is built at link time. Following
		// the real body, not the body copy, so a copy never lags a frame behind.
		Copy->SetLeaderPoseComponent(Body);
		// Skin the gear covers stays hidden, as on the real body.
		for (int32 Lod = 0; Lod < FMath::Min(Original.LODInfo.Num(), Copy->LODInfo.Num()); ++Lod)
		{
			Copy->LODInfo[Lod].HiddenMaterials = Original.LODInfo[Lod].HiddenMaterials;
		}
		Copy->MarkRenderStateDirty();
		Copies.Add(Copy);
		CopySources.Add(&Original);
		return Copy;
	};

	// Relative to the capsule the body sits at the feet, turned to face the actor's +X.
	BodyCopy = CopySkinned(*Body, FigureRoot);

	TArray<UMeshComponent*> Meshes;
	Character->GetComponents(Meshes);
	for (const UMeshComponent* Mesh : Meshes)
	{
		if (Mesh == Body || !Mesh->IsAttachedTo(Body) || !ShouldCopy(*Mesh))
		{
			continue;
		}
		if (const USkeletalMeshComponent* Skinned = Cast<USkeletalMeshComponent>(Mesh); Skinned && Skinned->GetSkeletalMeshAsset())
		{
			CopySkinned(*Skinned, BodyCopy);
		}
	}
	// Held items after the wearables, so whatever they hang from has its copy already.
	for (const UMeshComponent* Mesh : Meshes)
	{
		const UStaticMeshComponent* Rigid = Cast<UStaticMeshComponent>(Mesh);
		if (!Rigid || !Rigid->GetStaticMesh() || !Rigid->IsAttachedTo(Body) || !ShouldCopy(*Rigid))
		{
			continue;
		}
		USceneComponent* Parent = FindCopy(Cast<UPrimitiveComponent>(Rigid->GetAttachParent()));
		UStaticMeshComponent* Copy = NewObject<UStaticMeshComponent>(this);
		MakeQuiet(*Copy);
		Copy->SetStaticMesh(Rigid->GetStaticMesh());
		for (int32 MaterialIndex = 0; MaterialIndex < Copy->GetNumMaterials(); ++MaterialIndex)
		{
			Copy->SetMaterial(MaterialIndex, FigureMaterial);
		}
		Copy->SetupAttachment(Parent ? Parent : BodyCopy.Get(), Rigid->GetAttachSocketName());
		Copy->SetRelativeTransform(Rigid->GetRelativeTransform());
		Copy->RegisterComponent();
		Copies.Add(Copy);
		CopySources.Add(Rigid);
	}
	NoteProjectionChange();
}

void ASWGHoloFigureActor::Tick(float DeltaSeconds)
{
	if (FPlatformTime::Seconds() >= NextSourceScanTime)
	{
		NextSourceScanTime = FPlatformTime::Seconds() + SourceScanSeconds;
		if (SourceSignature() != BuiltSignature)
		{
			Rebuild();
		}
	}
	// Before Super::Tick, which aims the droid's rays at the shape.
	UpdateShape();
	Super::Tick(DeltaSeconds);
	if (CVarShowFigureTargets.GetValueOnGameThread())
	{
		for (int32 RayIndex = 0; RayIndex < FigureRayCount; ++RayIndex)
		{
			DrawDebugSphere(GetWorld(), GetActorTransform().TransformPosition(GetProjectionTarget(RayIndex)), 2.f, 8, FColor::Orange);
		}
	}
}

void ASWGHoloFigureActor::UpdateShape()
{
	BonePoints.Reset();
	const USkinnedAsset* Mesh = BodyCopy ? BodyCopy->GetSkinnedAsset() : nullptr;
	if (!Mesh)
	{
		return;
	}
	const FTransform ActorToWorld = GetActorTransform();
	const int32 BoneCount = BodyCopy->GetNumBones();
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		BonePoints.Add(ActorToWorld.InverseTransformPosition(BodyCopy->GetBoneTransform(BoneIndex).GetLocation()));
	}
	if (BonePoints.IsEmpty())
	{
		return;
	}
	float HighestZ = BonePoints[0].Z;
	float LowestZ = BonePoints[0].Z;
	for (const FVector& Point : BonePoints)
	{
		HighestZ = FMath::Max(HighestZ, Point.Z);
		LowestZ = FMath::Min(LowestZ, Point.Z);
	}
	// A leader without a pose yet reports every bone at the origin.
	if (HighestZ - LowestZ < 10.f)
	{
		return;
	}
	if (!bGrounded)
	{
		// The body's origin is at the pelvis; stand the toes on the disc. Once, so idling doesn't bob it.
		bGrounded = true;
		FigureRoot->AddRelativeLocation(FVector(0.f, 0.f, -LowestZ));
		for (FVector& Point : BonePoints)
		{
			Point.Z -= LowestZ;
		}
		HighestZ -= LowestZ;
		LowestZ = 0.f;
	}
	HeadJointZ = HighestZ;
	FigureBottom = LowestZ;
	FigureTop = HighestZ + (HighestZ - LowestZ) * CrownAboveHeadJoint;
	ProjectionLift = (FigureTop + FigureBottom) * 0.5f;
}

bool ASWGHoloFigureActor::GetBoneAnchor(FName Bone, FVector& OutWorld) const
{
	if (!BodyCopy || !BodyCopy->GetSkinnedAsset())
	{
		return false;
	}
	// Joint names vary in case between skeletons ("Head", "head").
	for (int32 BoneIndex = 0; BoneIndex < BodyCopy->GetNumBones(); ++BoneIndex)
	{
		if (BodyCopy->GetBoneName(BoneIndex).IsEqual(Bone, ENameCase::IgnoreCase))
		{
			OutWorld = BodyCopy->GetBoneTransform(BoneIndex).GetLocation();
			return true;
		}
	}
	return false;
}

FVector ASWGHoloFigureActor::GetItemAnchor(const FString& SlotNames, const UPrimitiveComponent* ItemVisual) const
{
	// A held item's own copy sits exactly where it is shown; a worn one spans the body, so use its slot's bone.
	if (const UMeshComponent* Copy = FindCopy(ItemVisual); Copy && Copy->IsA<UStaticMeshComponent>())
	{
		return Copy->Bounds.Origin;
	}
	TArray<FString> Slots;
	SlotNames.ParseIntoArray(Slots, TEXT(","));
	FVector Anchor;
	for (const FString& Slot : Slots)
	{
		const FString Trimmed = Slot.TrimStartAndEnd();
		for (const TPair<const TCHAR*, const TCHAR*>& Entry : SlotBones)
		{
			if (Trimmed.StartsWith(Entry.Key, ESearchCase::IgnoreCase) && GetBoneAnchor(Entry.Value, Anchor))
			{
				return Anchor;
			}
		}
	}
	return GetBoneAnchor(FallbackBone, Anchor) ? Anchor : GetActorLocation() + FVector(0.f, 0.f, ProjectionLift);
}

void ASWGHoloFigureActor::SetHighlighted(const UPrimitiveComponent* ItemVisual)
{
	UMeshComponent* Copy = FindCopy(ItemVisual);
	if (Copy == HighlightedCopy.Get())
	{
		return;
	}
	auto Paint = [](UMeshComponent* Mesh, UMaterialInterface* Material)
	{
		for (int32 MaterialIndex = 0; Mesh && MaterialIndex < Mesh->GetNumMaterials(); ++MaterialIndex)
		{
			Mesh->SetMaterial(MaterialIndex, Material);
		}
	};
	Paint(HighlightedCopy.Get(), FigureMaterial);
	HighlightedCopy = Copy;
	if (Copy)
	{
		if (!HighlightMaterial)
		{
			HighlightMaterial = MakeHoloMaterial(HoloColor, HoloIntensity * 3.f);
		}
		Paint(Copy, HighlightMaterial);
	}
}

FVector ASWGHoloFigureActor::SidePoint(float MinZ, float MaxZ, bool bRight) const
{
	const FVector* Widest = nullptr;
	for (const FVector& Point : BonePoints)
	{
		if (Point.Z >= MinZ && Point.Z <= MaxZ && (!Widest || (bRight ? Point.Y > Widest->Y : Point.Y < Widest->Y)))
		{
			Widest = &Point;
		}
	}
	const float MidZ = (MinZ + MaxZ) * 0.5f;
	return Widest ? FVector(Widest->X, Widest->Y, MidZ) : FVector(0.f, (bRight ? 20.f : -20.f) * FigureScale, MidZ);
}

FVector ASWGHoloFigureActor::GetDroidHover() const
{
	return FVector(DroidSide.X, DroidSide.Y, 0.f) * DiscDiameter * 0.5f * DroidReach + FVector(0.f, 0.f, FigureTop + DroidClearance);
}

FVector ASWGHoloFigureActor::GetProjectionTarget(int32 RayIndex) const
{
	const float Height = HeadJointZ - FigureBottom;
	switch (RayIndex)
	{
	case LeftShoulderRay:
	case RightShoulderRay:
		return SidePoint(FigureBottom + Height * ShoulderBandLow, FigureBottom + Height * ShoulderBandHigh, RayIndex == RightShoulderRay);
	case LeftFootRay:
	case RightFootRay:
		return SidePoint(FigureBottom, FigureBottom + Height * FootBandHigh, RayIndex == RightFootRay);
	default:
	{
		// Mid-skull, over the head joint.
		const FVector* Head = BonePoints.FindByPredicate([this](const FVector& Point) { return Point.Z >= HeadJointZ; });
		const float SkullZ = (HeadJointZ + FigureTop) * 0.5f;
		return Head ? FVector(Head->X, Head->Y, SkullZ) : FVector(0.f, 0.f, SkullZ);
	}
	}
}

FVector ASWGHoloFigureActor::GetSweepTarget(int32 RayIndex, float Phase) const
{
	// A scan line walking head to toe, one ray on each side of the figure.
	const float Height = FigureTop - FigureBottom;
	const float ScanZ = FigureTop - Phase * Height;
	FVector Edge = SidePoint(ScanZ - Height * SweepBandHalf, ScanZ + Height * SweepBandHalf, RayIndex % 2 == 1);
	Edge.Z = ScanZ;
	return Edge;
}
