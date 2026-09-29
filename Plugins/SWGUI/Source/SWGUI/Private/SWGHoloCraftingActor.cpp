#include "SWGHoloCraftingActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Subsystems/SWGItemIconSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"

namespace
{
	/** Glow of the holo shell over each real model; higher reads more solid (the material is additive). */
	constexpr float OverlayIntensityScale = 0.22f;
	void PlaceIndicator(UStaticMeshComponent* Line, const FVector& Start, const FVector& End, float Width)
	{
		const FVector Delta = End - Start;
		Line->SetRelativeLocation((Start + End) * 0.5f);
		Line->SetRelativeRotation(FRotationMatrix::MakeFromZ(Delta).Rotator());
		Line->SetRelativeScale3D(FVector(Width / 100.f, Width / 100.f, Delta.Size() / 100.f));
	}
}

ASWGHoloCraftingActor::ASWGHoloCraftingActor()
{
	DiscDiameter = 230.f;
	ProjectionLift = 5.f;
	ProjectionRayCount = 0;
	// The base class's own "sweep rays for a moment" effect (NoteProjectionChange,
	// already called by SetItems/SetCandidateRows/SetPrototypeObject) — a droid
	// scan, for free, whenever the floor's contents actually change.
	SweepRayCount = 3;
	DroidReach = 0.95f;
	DroidHeight = 0.8f;
	GlowLight->SetIntensity(8.f);
	GlowLight->SetLightColor(FLinearColor(0.9f, 0.95f, 1.f));
	GlowLight->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	AssemblyPan = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AssemblyPan"));
	AssemblyPan->SetupAttachment(GetRootComponent());
	AssemblyPan->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AssemblyPan->SetCastShadow(false);
	AssemblyPan->SetStaticMesh(BeamMesh);
	AssemblyPan->SetRelativeScale3D(FVector(0.f, 0.f, 0.03f));
	AssemblyPan->SetVisibility(false);
}

FVector ASWGHoloCraftingActor::GetDroidHover() const
{
	// Assembly's camera looks steeply down, so the droid drops into its frame.
	return bAssemblyMode ? FVector(60.f, -100.f, 55.f) : FVector(105.f, -80.f, 105.f);
}

void ASWGHoloCraftingActor::SetAssemblyMode(bool bEnabled)
{
	if (bAssemblyMode == bEnabled) { return; }
	bAssemblyMode = bEnabled;
	PanTransition = bEnabled ? 0.f : 1.f;
	if (!bEnabled) { ComponentPanelRayTargets.Reset(); }
	BaseComponent->SetVisibility(!bEnabled && !Items.IsEmpty());
	GlowLight->SetVisibility(true);
	AssemblyPan->SetVisibility(bEnabled);
	SetExtraRayRadius(bEnabled ? 900.f : GetFadeRadius());
	if (bEnabled) { AssemblyPan->SetMaterial(0, MakeHoloMaterial(HoloColor, HoloIntensity * 0.05f)); }
	NoteProjectionChange();
}

void ASWGHoloCraftingActor::SetSlotFillCounts(const TArray<int32>& Counts)
{
	SlotFillCounts = Counts;
	auto AddLine = [this](TArray<TObjectPtr<UStaticMeshComponent>>& Lines, float Intensity)
	{
		UStaticMeshComponent* Line = NewObject<UStaticMeshComponent>(this);
		Line->SetStaticMesh(BeamMesh);
		Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Line->SetCastShadow(false);
		Line->SetMaterial(0, MakeHoloMaterial(HoloColor, HoloIntensity * Intensity));
		Line->SetupAttachment(GetRootComponent());
		Line->RegisterComponent();
		Lines.Add(Line);
	};
	while (LaneLines.Num() < Counts.Num() + 1) { AddLine(LaneLines, 0.14f); }
	while (ContributionLines.Num() < Counts.Num()) { AddLine(ContributionLines, 0.35f); }
	NoteProjectionChange();
}

void ASWGHoloCraftingActor::SetContributionOrigins(const TArray<FVector>& WorldOrigins)
{
	ContributionOrigins = WorldOrigins;
}

void ASWGHoloCraftingActor::SetComponentPanelRayTargets(const TArray<FVector>& WorldTargets)
{
	ComponentPanelRayTargets = WorldTargets;
}

void ASWGHoloCraftingActor::RedrawStage()
{
	PanTransition = 0.f;
	NoteProjectionChange();
}

void ASWGHoloCraftingActor::SetItems(const TArray<FEntry>& InItems)
{
	BaseComponent->SetVisibility(!bAssemblyMode && !InItems.IsEmpty());
	GlowLight->SetVisibility(bAssemblyMode || !InItems.IsEmpty());
	if (UMaterialInstanceDynamic* DiscMaterial = Cast<UMaterialInstanceDynamic>(BaseComponent->GetMaterial(0)))
	{
		DiscMaterial->SetScalarParameterValue(TEXT("Intensity"), HoloIntensity * 0.035f);
	}
	++Revision;
	for (FProjectedItem& Item : Items)
	{
		if (Item.Model) { Item.Model->DestroyComponent(); }
		if (Item.HoloOverlay) { Item.HoloOverlay->DestroyComponent(); }
		if (Item.Label) { Item.Label->DestroyComponent(); }
		if (Item.Pivot) { Item.Pivot->DestroyComponent(); }
	}
	Items.Reset();
	ItemObjects.Reset();
	SelectedIndex = 0;
	USWGMeshGeneratorSubsystem* Meshes = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	for (const FEntry& Entry : InItems)
	{
		const int32 Index = Items.Num();
		FProjectedItem& Item = Items.AddDefaulted_GetRef();
		Item.Name = Entry.Name;
		Item.TemplateCrc = Entry.CraftedTemplateCrc;
		Item.Pivot = NewObject<USceneComponent>(this);
		Item.Pivot->SetupAttachment(GetRootComponent());
		Item.Pivot->RegisterComponent();
		Item.Label = NewObject<UTextRenderComponent>(this);
		Item.Label->SetText(FText::FromString(Entry.Name));
		Item.Label->SetWorldSize(5.5f);
		Item.Label->SetHorizontalAlignment(EHTA_Center);
		Item.Label->SetVerticalAlignment(EVRTA_TextCenter);
		Item.Label->SetTextRenderColor(FColor(0x97, 0xFF, 0xFF));
		if (UMaterialInterface* TextMaterial = GetHoloTextMaterial()) { Item.Label->SetTextMaterial(TextMaterial); }
		Item.Label->SetupAttachment(Item.Pivot);
		Item.Label->SetRelativeLocation(FVector(0.f, 0.f, 34.f));
		Item.Label->RegisterComponent();
		ItemObjects.Append({ Item.Pivot, Item.Label });
		if (!Meshes || Entry.CraftedTemplateCrc == 0) { continue; }
		const int32 RequestedRevision = Revision;
		TWeakObjectPtr<ASWGHoloCraftingActor> WeakThis(this);
		Meshes->RequestItemMesh(Entry.CraftedTemplateCrc, 0, {},
			[WeakThis, Index, RequestedRevision](UStaticMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials)
			{
				if (ASWGHoloCraftingActor* Actor = WeakThis.Get()) { Actor->AttachModel(Index, RequestedRevision, Mesh, Materials); }
			},
			[WeakThis, Index, RequestedRevision](USkeletalMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials)
			{
				if (ASWGHoloCraftingActor* Actor = WeakThis.Get()) { Actor->AttachModel(Index, RequestedRevision, Mesh, Materials); }
			});
	}
	NoteProjectionChange();
}

void ASWGHoloCraftingActor::AttachModel(int32 Index, int32 RequestedRevision, UObject* Mesh, const TArray<UMaterialInterface*>& Materials)
{
	if (RequestedRevision != Revision || !Items.IsValidIndex(Index) || !Mesh || Items[Index].Model) { return; }
	FProjectedItem& Item = Items[Index];
	UMeshComponent* Component = nullptr;
	UMeshComponent* Overlay = nullptr;
	FBoxSphereBounds Bounds;
	if (UStaticMesh* Static = Cast<UStaticMesh>(Mesh))
	{
		UStaticMeshComponent* Model = NewObject<UStaticMeshComponent>(this);
		Model->SetStaticMesh(Static);
		Component = Model;
		UStaticMeshComponent* Holo = NewObject<UStaticMeshComponent>(this);
		Holo->SetStaticMesh(Static);
		Overlay = Holo;
		Bounds = Static->GetBounds();
	}
	else if (USkeletalMesh* Skeletal = Cast<USkeletalMesh>(Mesh))
	{
		USkeletalMeshComponent* Model = NewObject<USkeletalMeshComponent>(this);
		Model->SetSkeletalMeshAsset(Skeletal);
		Component = Model;
		USkeletalMeshComponent* Holo = NewObject<USkeletalMeshComponent>(this);
		Holo->SetSkeletalMeshAsset(Skeletal);
		Overlay = Holo;
		Bounds = Skeletal->GetBounds();
	}
	if (!Component) { return; }
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
	{
		if (Materials[MaterialIndex]) { Component->SetMaterial(MaterialIndex, Materials[MaterialIndex]); }
	}
	Component->SetupAttachment(Item.Pivot);
	Component->RegisterComponent();
	Overlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Overlay->SetCastShadow(false);
	UMaterialInstanceDynamic* OverlayMaterial = MakeHoloMaterial(HoloColor, HoloIntensity * OverlayIntensityScale);
	for (int32 MaterialIndex = 0; MaterialIndex < Overlay->GetNumMaterials(); ++MaterialIndex) { Overlay->SetMaterial(MaterialIndex, OverlayMaterial); }
	Overlay->SetupAttachment(Item.Pivot);
	Overlay->RegisterComponent();
	Item.Model = Component;
	Item.HoloOverlay = Overlay;
	Item.ModelExtent = Bounds.BoxExtent.GetMax();
	Item.ModelOrigin = Bounds.Origin;
	const float Scale = 34.f / FMath::Max(2.f * Item.ModelExtent, UE_KINDA_SMALL_NUMBER);
	Component->SetRelativeScale3D(FVector(Scale));
	Component->SetRelativeLocation(-Item.ModelOrigin * Scale);
	Overlay->SetRelativeScale3D(FVector(Scale * 1.025f));
	Overlay->SetRelativeLocation(-Item.ModelOrigin * Scale);
	ItemObjects.Add(Component);
	ItemObjects.Add(Overlay);
	NoteProjectionChange();
}

FVector ASWGHoloCraftingActor::ItemLocation(int32 Index) const
{
	const float Offset = Index - SelectedIndex;
	return FVector(FMath::Abs(Offset) * 10.f, Offset * 46.f, 53.f);
}

bool ASWGHoloCraftingActor::GetItemLocation(int32 Index, FVector& OutWorld) const
{
	if (!Items.IsValidIndex(Index) || FMath::Abs(Index - SelectedIndex) > 2) { return false; }
	OutWorld = GetActorTransform().TransformPosition(ItemLocation(Index));
	return true;
}

void ASWGHoloCraftingActor::SetPrototypeObject(int64 ObjectId)
{
	if (PrototypeObjectId == ObjectId && (ObjectId == 0 || PrototypeModel)) { return; }
	PrototypeObjectId = ObjectId;
	++PrototypeRevision;
	if (PrototypeModel) { PrototypeModel->DestroyComponent(); PrototypeModel = nullptr; }
	if (PrototypeHoloOverlay) { PrototypeHoloOverlay->DestroyComponent(); PrototypeHoloOverlay = nullptr; }
	PrototypeObjects.Reset();
	if (!PrototypePivot)
	{
		PrototypePivot = NewObject<USceneComponent>(this);
		PrototypePivot->SetupAttachment(GetRootComponent());
		// Off to the right, roughly level with the candidate floor's own
		// height band — the card (screen-space, tracked by the widget) sits
		// beside this in screen space, not in world space.
		PrototypePivot->SetRelativeLocation(FVector(0.f, 55.f, 40.f));
		PrototypePivot->RegisterComponent();
	}
	if (ObjectId == 0) { return; }
	USWGItemIconSubsystem* Icons = GetWorld() ? GetWorld()->GetSubsystem<USWGItemIconSubsystem>() : nullptr;
	if (!Icons) { return; }
	const int32 RequestedRevision = PrototypeRevision;
	TWeakObjectPtr<ASWGHoloCraftingActor> WeakThis(this);
	Icons->RequestItemModel(ObjectId, [WeakThis, RequestedRevision](UObject* Mesh, const TArray<UMaterialInterface*>& Materials)
	{
		if (ASWGHoloCraftingActor* Actor = WeakThis.Get()) { Actor->AttachPrototypeModel(RequestedRevision, Mesh, Materials); }
	});
}

FVector ASWGHoloCraftingActor::GetPrototypeLocation() const
{
	return PrototypePivot ? PrototypePivot->GetComponentLocation() : GetActorLocation();
}

void ASWGHoloCraftingActor::SetPrototypeTangibility(float Progress)
{
	PrototypeTangibility = FMath::Clamp(Progress, 0.f, 1.f);
}

void ASWGHoloCraftingActor::AttachPrototypeModel(int32 RequestedRevision, UObject* Mesh, const TArray<UMaterialInterface*>& Materials)
{
	if (RequestedRevision != PrototypeRevision || !Mesh || PrototypeModel || !PrototypePivot) { return; }
	UMeshComponent* Component = nullptr;
	UMeshComponent* Overlay = nullptr;
	FBoxSphereBounds Bounds;
	if (UStaticMesh* Static = Cast<UStaticMesh>(Mesh))
	{
		UStaticMeshComponent* Model = NewObject<UStaticMeshComponent>(this);
		Model->SetStaticMesh(Static);
		Component = Model;
		UStaticMeshComponent* Holo = NewObject<UStaticMeshComponent>(this);
		Holo->SetStaticMesh(Static);
		Overlay = Holo;
		Bounds = Static->GetBounds();
	}
	else if (USkeletalMesh* Skeletal = Cast<USkeletalMesh>(Mesh))
	{
		USkeletalMeshComponent* Model = NewObject<USkeletalMeshComponent>(this);
		Model->SetSkeletalMeshAsset(Skeletal);
		Component = Model;
		USkeletalMeshComponent* Holo = NewObject<USkeletalMeshComponent>(this);
		Holo->SetSkeletalMeshAsset(Skeletal);
		Overlay = Holo;
		Bounds = Skeletal->GetBounds();
	}
	if (!Component) { return; }
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
	{
		if (Materials[MaterialIndex]) { Component->SetMaterial(MaterialIndex, Materials[MaterialIndex]); }
	}
	Component->SetupAttachment(PrototypePivot);
	Component->RegisterComponent();
	Overlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Overlay->SetCastShadow(false);
	// Tangibility starts this off strongly holo (see Tick, which drives the
	// overlay's own intensity from PrototypeTangibility every frame) —
	// MakeHoloMaterial's own baseline here is overwritten there immediately.
	UMaterialInstanceDynamic* OverlayMaterial = MakeHoloMaterial(HoloColor, HoloIntensity * OverlayIntensityScale);
	for (int32 MaterialIndex = 0; MaterialIndex < Overlay->GetNumMaterials(); ++MaterialIndex) { Overlay->SetMaterial(MaterialIndex, OverlayMaterial); }
	Overlay->SetupAttachment(PrototypePivot);
	Overlay->RegisterComponent();
	PrototypeModel = Component;
	PrototypeHoloOverlay = Overlay;
	PrototypeExtent = Bounds.BoxExtent.GetMax();
	PrototypeOrigin = Bounds.Origin;
	constexpr float PrototypeTargetSize = 46.f;
	const float Scale = PrototypeTargetSize / FMath::Max(2.f * PrototypeExtent, UE_KINDA_SMALL_NUMBER);
	Component->SetRelativeScale3D(FVector(Scale));
	Component->SetRelativeLocation(-PrototypeOrigin * Scale);
	Overlay->SetRelativeScale3D(FVector(Scale * 1.025f));
	Overlay->SetRelativeLocation(-PrototypeOrigin * Scale);
	PrototypeObjects.Append({ Component, Overlay });
	NoteProjectionChange();
}

void ASWGHoloCraftingActor::SetCandidateRows(const TArray<TArray<FEntry>>& RowsBySlot)
{
	++CandidateRevision;
	for (FProjectedCandidate& Candidate : Candidates)
	{
		if (Candidate.Model) { Candidate.Model->DestroyComponent(); }
		if (Candidate.HoloOverlay) { Candidate.HoloOverlay->DestroyComponent(); }
		if (Candidate.Label) { Candidate.Label->DestroyComponent(); }
		if (Candidate.Pivot) { Candidate.Pivot->DestroyComponent(); }
	}
	Candidates.Reset();
	CandidateObjects.Reset();
	SelectedCandidateColumn = 0;
	USWGItemIconSubsystem* Icons = GetWorld() ? GetWorld()->GetSubsystem<USWGItemIconSubsystem>() : nullptr;
	for (int32 SlotIndex = 0; SlotIndex < RowsBySlot.Num(); ++SlotIndex)
	{
		const TArray<FEntry>& Row = RowsBySlot[SlotIndex];
		for (int32 Column = 0; Column < Row.Num(); ++Column)
		{
			const FEntry& Entry = Row[Column];
			const int32 Index = Candidates.Num();
			FProjectedCandidate& Candidate = Candidates.AddDefaulted_GetRef();
			Candidate.Name = Entry.Name;
			Candidate.ObjectId = Entry.ObjectId;
			Candidate.SlotIndex = SlotIndex;
			Candidate.Column = Column;
			Candidate.RowColumnCount = Row.Num();
			Candidate.Pivot = NewObject<USceneComponent>(this);
			Candidate.Pivot->SetupAttachment(GetRootComponent());
			Candidate.Pivot->RegisterComponent();
			Candidate.Label = NewObject<UTextRenderComponent>(this);
			Candidate.Label->SetText(FText::FromString(Entry.Name));
			Candidate.Label->SetWorldSize(4.2f);
			Candidate.Label->SetHorizontalAlignment(EHTA_Center);
			Candidate.Label->SetVerticalAlignment(EVRTA_TextBottom);
			Candidate.Label->SetTextRenderColor(FColor(0x8B, 0xDC, 0xF5));
			if (UMaterialInterface* TextMaterial = GetHoloTextMaterial()) { Candidate.Label->SetTextMaterial(TextMaterial); }
			Candidate.Label->SetupAttachment(GetRootComponent());
			Candidate.Label->RegisterComponent();
			CandidateObjects.Append({ Candidate.Pivot, Candidate.Label });
			if (Entry.ObjectId != 0 && Icons)
			{
				const int32 RequestedRevision = CandidateRevision;
				TWeakObjectPtr<ASWGHoloCraftingActor> WeakThis(this);
				Icons->RequestItemModel(Entry.ObjectId, [WeakThis, Index, RequestedRevision](UObject* Mesh, const TArray<UMaterialInterface*>& Materials)
				{
					if (ASWGHoloCraftingActor* Actor = WeakThis.Get()) { Actor->AttachCandidateModel(Index, RequestedRevision, Mesh, Materials); }
				});
			}
		}
	}
	NoteProjectionChange();
}

void ASWGHoloCraftingActor::AttachCandidateModel(int32 Index, int32 RequestedRevision, UObject* Mesh, const TArray<UMaterialInterface*>& Materials)
{
	if (RequestedRevision != CandidateRevision || !Candidates.IsValidIndex(Index) || !Mesh || Candidates[Index].Model) { return; }
	FProjectedCandidate& Candidate = Candidates[Index];
	UMeshComponent* Component = nullptr;
	UMeshComponent* Overlay = nullptr;
	FBoxSphereBounds Bounds;
	if (UStaticMesh* Static = Cast<UStaticMesh>(Mesh))
	{
		UStaticMeshComponent* Model = NewObject<UStaticMeshComponent>(this);
		Model->SetStaticMesh(Static);
		Component = Model;
		UStaticMeshComponent* Holo = NewObject<UStaticMeshComponent>(this);
		Holo->SetStaticMesh(Static);
		Overlay = Holo;
		Bounds = Static->GetBounds();
	}
	else if (USkeletalMesh* Skeletal = Cast<USkeletalMesh>(Mesh))
	{
		USkeletalMeshComponent* Model = NewObject<USkeletalMeshComponent>(this);
		Model->SetSkeletalMeshAsset(Skeletal);
		Component = Model;
		USkeletalMeshComponent* Holo = NewObject<USkeletalMeshComponent>(this);
		Holo->SetSkeletalMeshAsset(Skeletal);
		Overlay = Holo;
		Bounds = Skeletal->GetBounds();
	}
	if (!Component) { return; }
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
	{
		if (Materials[MaterialIndex]) { Component->SetMaterial(MaterialIndex, Materials[MaterialIndex]); }
	}
	Component->SetupAttachment(Candidate.Pivot);
	Component->RegisterComponent();
	Overlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Overlay->SetCastShadow(false);
	UMaterialInstanceDynamic* OverlayMaterial = MakeHoloMaterial(HoloColor, HoloIntensity * 0.12f);
	for (int32 MaterialIndex = 0; MaterialIndex < Overlay->GetNumMaterials(); ++MaterialIndex) { Overlay->SetMaterial(MaterialIndex, OverlayMaterial); }
	Overlay->SetupAttachment(Candidate.Pivot);
	Overlay->RegisterComponent();
	Candidate.Model = Component;
	Candidate.HoloOverlay = Overlay;
	Candidate.ModelExtent = Bounds.BoxExtent.GetMax();
	Candidate.ModelOrigin = Bounds.Origin;
	const float Scale = 28.f / FMath::Max(2.f * Candidate.ModelExtent, UE_KINDA_SMALL_NUMBER);
	Component->SetRelativeScale3D(FVector(Scale));
	Component->SetRelativeLocation(-Candidate.ModelOrigin * Scale);
	Overlay->SetRelativeScale3D(FVector(Scale * 1.025f));
	Overlay->SetRelativeLocation(-Candidate.ModelOrigin * Scale);
	CandidateObjects.Add(Component);
	CandidateObjects.Add(Overlay);
	NoteProjectionChange();
}

FVector ASWGHoloCraftingActor::CandidateLocation(const FProjectedCandidate& Candidate) const
{
	// The focused slot occupies the front row. Every other slot gets a full
	// row farther from the camera, in its own lane behind the focused one.
	const int32 RowDepthIndex = SlotRowDepthIndex(Candidate.SlotIndex);
	constexpr float RowSpacing = 48.f;
	constexpr float ColumnSpacing = 42.f;
	constexpr float Forward = -34.f;
	constexpr float GroundHeight = 8.f;
	const float Side = (Candidate.Column - (Candidate.RowColumnCount - 1) * 0.5f) * ColumnSpacing;
	return FVector(Forward + RowDepthIndex * RowSpacing, Side, GroundHeight);
}

int32 ASWGHoloCraftingActor::SlotRowDepthIndex(int32 SlotIndex) const
{
	return SlotIndex == SelectedSlotForCandidates ? 0
		: SlotIndex < SelectedSlotForCandidates ? SlotIndex + 1 : SlotIndex;
}

bool ASWGHoloCraftingActor::GetCandidateLocation(int32 SlotIndex, int32 Column, FVector& OutWorld) const
{
	for (const FProjectedCandidate& Candidate : Candidates)
	{
		if (Candidate.SlotIndex == SlotIndex && Candidate.Column == Column)
		{
			OutWorld = GetActorTransform().TransformPosition(CandidateLocation(Candidate));
			return true;
		}
	}
	return false;
}

void ASWGHoloCraftingActor::SetSelectedSlotForCandidates(int32 SlotIndex)
{
	if (SelectedSlotForCandidates != SlotIndex)
	{
		SelectedSlotForCandidates = SlotIndex;
		SelectedCandidateColumn = 0;
		NoteProjectionChange();
	}
}

void ASWGHoloCraftingActor::SetSelectedCandidateColumn(int32 Column)
{
	SelectedCandidateColumn = Column;
}

FVector ASWGHoloCraftingActor::GetFocusLocation() const
{
	return GetActorTransform().TransformPosition(FVector(0.f, 0.f, 57.f));
}

FVector ASWGHoloCraftingActor::GetAssemblyFocusLocation() const
{
	// The front candidate row sits at Forward=-34 and the other rows recede
	// behind it. Keep the camera aimed low enough to show those rows.
	return GetActorTransform().TransformPosition(FVector(-16.f, 16.f, 30.f));
}

void ASWGHoloCraftingActor::SetSelectedIndex(int32 Index)
{
	const int32 Clamped = FMath::Clamp(Index, 0, FMath::Max(0, Items.Num() - 1));
	if (Clamped != SelectedIndex) { SelectedIndex = Clamped; NoteProjectionChange(); }
}

void ASWGHoloCraftingActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (int32 Index = 0; Index < LaneLines.Num(); ++Index)
	{
		UStaticMeshComponent* Line = LaneLines[Index];
		Line->SetVisibility(bAssemblyMode && Index <= SlotFillCounts.Num());
		if (bAssemblyMode) { PlaceIndicator(Line, FVector(-58.f + Index * 48.f, -108.f, 2.f), FVector(-58.f + Index * 48.f, 108.f, 2.f), 1.1f); }
	}
	for (int32 Index = 0; Index < ContributionLines.Num(); ++Index)
	{
		UStaticMeshComponent* Line = ContributionLines[Index];
		const bool bShown = bAssemblyMode && SlotFillCounts.IsValidIndex(Index) && SlotFillCounts[Index] > 0
			&& ContributionOrigins.IsValidIndex(Index) && !ContributionOrigins[Index].IsNearlyZero() && PrototypeModel;
		Line->SetVisibility(bShown);
		if (bShown)
		{
			const float HeightOffset = (Index - (SlotFillCounts.Num() - 1) * 0.5f) * 7.f;
			PlaceIndicator(Line, GetActorTransform().InverseTransformPosition(ContributionOrigins[Index]),
				PrototypePivot->GetRelativeLocation() + FVector(0.f, 17.f, HeightOffset), 1.1f);
		}
	}
	if (bAssemblyMode)
	{
		PanTransition = FMath::Min(1.f, PanTransition + DeltaSeconds / 0.6f);
		const float Eased = FMath::InterpEaseInOut(0.f, 1.f, PanTransition, 2.f);
		AssemblyPan->SetRelativeScale3D(FVector(2.4f * Eased, 2.4f * Eased, 0.03f));
	}
	FVector CameraLocation = GetActorLocation();
	FRotator CameraRotation;
	if (const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		Player->GetPlayerViewPoint(CameraLocation, CameraRotation);
	}
	const FRotator FaceCamera = (CameraLocation - GetActorLocation()).GetSafeNormal2D().Rotation();
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		FProjectedItem& Item = Items[Index];
		const bool bShown = FMath::Abs(Index - SelectedIndex) <= 2;
		Item.Pivot->SetVisibility(bShown, true);
		Item.Label->SetVisibility(bShown);
		if (!bShown) { continue; }
		Item.Pivot->SetRelativeLocation(ItemLocation(Index));
		Item.Pivot->SetRelativeScale3D(FVector(Index == SelectedIndex ? 1.2f : 0.8f));
		Item.Pivot->AddLocalRotation(FRotator(0.f, DeltaSeconds * 20.f, 0.f));
		Item.Label->SetWorldRotation(FaceCamera);
		Item.Label->SetTextRenderColor(Index == SelectedIndex ? FColor::White : FColor(0x8B, 0xDC, 0xF5));
	}
	for (FProjectedCandidate& Candidate : Candidates)
	{
		const bool bFrontRow = Candidate.SlotIndex == SelectedSlotForCandidates;
		const bool bSelected = bFrontRow && Candidate.Column == SelectedCandidateColumn;
		if (Candidate.Model) { Candidate.SpinYaw += DeltaSeconds * 20.f; }
		Candidate.Pivot->SetRelativeLocation(CandidateLocation(Candidate));
		Candidate.Pivot->SetRelativeRotation(FRotator(0.f, Candidate.SpinYaw, 0.f));
		Candidate.Pivot->SetRelativeScale3D(FVector(bSelected ? 1.15f : bFrontRow ? 0.9f : 0.8f));
		const FVector CandidateWorld = Candidate.Pivot->GetComponentLocation();
		const FVector TowardCamera = (CameraLocation - CandidateWorld).GetSafeNormal2D();
		Candidate.Label->SetWorldLocation(CandidateWorld + TowardCamera * 36.f - FVector(0.f, 0.f, 8.f));
		Candidate.Label->SetWorldRotation(FaceCamera);
		Candidate.Label->SetVisibility(bFrontRow);
		Candidate.Label->SetTextRenderColor(bSelected ? FColor::White : FColor(0x8B, 0xDC, 0xF5));
	}
	if (PrototypeModel)
	{
		PrototypePivot->AddLocalRotation(FRotator(0.f, DeltaSeconds * 14.f, 0.f));
		PrototypeModel->SetVisibility(PrototypeTangibility > 0.f);
		if (UMaterialInstanceDynamic* OverlayMaterial = Cast<UMaterialInstanceDynamic>(PrototypeHoloOverlay ? PrototypeHoloOverlay->GetMaterial(0) : nullptr))
		{
			// Start with the complete projected silhouette; filled slots bring
			// the real mesh through while leaving the selected item's holo rim.
			const float OverlayIntensity = FMath::Lerp(HoloIntensity * 0.35f, HoloIntensity * OverlayIntensityScale, PrototypeTangibility);
			OverlayMaterial->SetScalarParameterValue(TEXT("Intensity"), OverlayIntensity);
		}
	}
	TArray<FVector> Targets;
	TArray<float> Brightness;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		FVector Location;
		if (GetItemLocation(Index, Location))
		{
			Targets.Add(Location + GetActorTransform().TransformVector(FVector(12.f, 0.f, 6.f)));
			Brightness.Add(Index == SelectedIndex ? 0.14f : 0.05f);
		}
	}
	for (const FProjectedCandidate& Candidate : Candidates)
	{
		if (Candidate.SlotIndex != SelectedSlotForCandidates) { continue; }
		Targets.Add(GetActorTransform().TransformPosition(CandidateLocation(Candidate) + FVector(0.f, 0.f, 6.f)));
		Brightness.Add(Candidate.Column == SelectedCandidateColumn ? 0.14f : 0.05f);
	}
	if (PrototypeModel)
	{
		Targets.Add(PrototypePivot->GetComponentLocation());
		Brightness.Add(0.1f);
	}
	if (bAssemblyMode)
	{
		for (const FVector& PanelTarget : ComponentPanelRayTargets)
		{
			Targets.Add(PanelTarget);
			Brightness.Add(0.12f);
		}
	}
	SetExtraRays(Targets, Brightness);
}
