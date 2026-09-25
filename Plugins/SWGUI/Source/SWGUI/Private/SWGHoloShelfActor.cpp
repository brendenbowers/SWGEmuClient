#include "SWGHoloShelfActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Subsystems/SWGItemIconSubsystem.h"

namespace
{
	constexpr float HoverSpinFactor = 4.f;
	constexpr float HoverGlowFactor = 2.2f;
	/** A hovered model grows by this much. */
	constexpr float HoverGrowth = 0.2f;
}

ASWGHoloShelfActor::ASWGHoloShelfActor()
{
	// Projected by the figure's droid; nothing of its own.
	bHasProjector = false;
	HoloIntensity = 0.55f;
}

int32 ASWGHoloShelfActor::IndexOf(int64 ObjectId) const
{
	return Items.IndexOfByPredicate([ObjectId](const FShelfItem& Item) { return Item.ObjectId == ObjectId; });
}

void ASWGHoloShelfActor::SetGridSize(int32 InColumns, int32 InVisibleRows)
{
	Columns = FMath::Max(1, InColumns);
	VisibleRows = FMath::Max(1, InVisibleRows);
	Scroll(0);
}

void ASWGHoloShelfActor::SetGridFrame(const FVector& TopLeft, const FVector& ColumnStep, const FVector& RowStep, float InItemSize)
{
	// Actor space: +Y along a row, -Z down the columns, +X out of the screen.
	const FVector Across = ColumnStep.GetSafeNormal();
	const FVector Down = RowStep.GetSafeNormal();
	SetActorLocationAndRotation(TopLeft, FRotationMatrix::MakeFromYZ(Across, -Down).Rotator());
	ColumnSpacing = ColumnStep.Size();
	RowSpacing = RowStep.Size();
	if (!FMath::IsNearlyEqual(ItemSize, InItemSize, ItemSize * 0.01f))
	{
		ItemSize = InItemSize;
		for (FShelfItem& Item : Items)
		{
			FitModel(Item);
		}
	}
}

void ASWGHoloShelfActor::SetItems(const TArray<int64>& ObjectIds)
{
	for (int32 Index = Items.Num() - 1; Index >= 0; --Index)
	{
		FShelfItem& Item = Items[Index];
		if (ObjectIds.Contains(Item.ObjectId))
		{
			continue;
		}
		if (Item.Model)
		{
			Item.Model->DestroyComponent();
		}
		if (Item.Pivot)
		{
			Item.Pivot->DestroyComponent();
		}
		for (UObject* Object : { (UObject*)Item.Pivot, (UObject*)Item.Model, (UObject*)Item.Material })
		{
			ItemObjects.Remove(Object);
		}
		Items.RemoveAt(Index);
	}
	for (const int64 ObjectId : ObjectIds)
	{
		if (IndexOf(ObjectId) == INDEX_NONE)
		{
			AddItem(ObjectId);
		}
	}
	// Same order as the list.
	Items.Sort([&ObjectIds](const FShelfItem& Left, const FShelfItem& Right) { return ObjectIds.IndexOfByKey(Left.ObjectId) < ObjectIds.IndexOfByKey(Right.ObjectId); });
	ItemIds = ObjectIds;
	Scroll(0);
	NoteProjectionChange();
}

void ASWGHoloShelfActor::AddItem(int64 ObjectId)
{
	FShelfItem& Item = Items.AddDefaulted_GetRef();
	Item.ObjectId = ObjectId;
	Item.Pivot = NewObject<USceneComponent>(this);
	Item.Pivot->SetupAttachment(GetRootComponent());
	Item.Pivot->RegisterComponent();
	Item.Material = MakeHoloMaterial(HoloColor, HoloIntensity);
	// Start each at a different turn so the list doesn't spin in lockstep.
	Item.Spin = FMath::FRandRange(0.f, 360.f);
	ItemObjects.Append({ Item.Pivot, Item.Material });
	RequestModel(ObjectId);
}

void ASWGHoloShelfActor::RequestModel(int64 ObjectId)
{
	USWGItemIconSubsystem* Icons = GetWorld() ? GetWorld()->GetSubsystem<USWGItemIconSubsystem>() : nullptr;
	if (!Icons)
	{
		return;
	}
	TWeakObjectPtr<ASWGHoloShelfActor> WeakThis(this);
	Icons->RequestItemModel(ObjectId, [WeakThis, ObjectId](UObject* Mesh, const TArray<UMaterialInterface*>&)
	{
		if (ASWGHoloShelfActor* Shelf = WeakThis.Get())
		{
			Shelf->AttachModel(ObjectId, Mesh);
		}
	});
}

void ASWGHoloShelfActor::AttachModel(int64 ObjectId, UObject* Mesh)
{
	const int32 Index = IndexOf(ObjectId);
	if (Index == INDEX_NONE || !Mesh || Items[Index].Model)
	{
		return;
	}
	FShelfItem& Item = Items[Index];
	UMeshComponent* Model = nullptr;
	FBoxSphereBounds Bounds;
	if (UStaticMesh* Rigid = Cast<UStaticMesh>(Mesh))
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		Component->SetStaticMesh(Rigid);
		Bounds = Rigid->GetBounds();
		Model = Component;
	}
	else if (USkeletalMesh* Skinned = Cast<USkeletalMesh>(Mesh))
	{
		// Bind pose; clothing and creatures read fine that way.
		USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(this);
		Component->SetSkeletalMeshAsset(Skinned);
		Bounds = Skinned->GetBounds();
		Model = Component;
	}
	if (!Model)
	{
		return;
	}
	Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Model->SetCastShadow(false);
	for (int32 MaterialIndex = 0; MaterialIndex < Model->GetNumMaterials(); ++MaterialIndex)
	{
		Model->SetMaterial(MaterialIndex, Item.Material);
	}
	Model->SetupAttachment(Item.Pivot);
	Model->RegisterComponent();
	Item.Model = Model;
	Item.ModelExtent = Bounds.BoxExtent.GetMax();
	Item.ModelOrigin = Bounds.Origin;
	FitModel(Item);
	ItemObjects.Add(Model);
	NoteProjectionChange();
}

void ASWGHoloShelfActor::FitModel(FShelfItem& Item) const
{
	if (!Item.Model)
	{
		return;
	}
	// The largest side fits ItemSize, centred on the pivot.
	const float Scale = ItemSize / FMath::Max(2.f * Item.ModelExtent, UE_KINDA_SMALL_NUMBER);
	Item.Model->SetRelativeScale3D(FVector(Scale));
	Item.Model->SetRelativeLocation(-Item.ModelOrigin * Scale);
}

int32 ASWGHoloShelfActor::MaxScrollRow() const
{
	const int32 Rows = FMath::DivideAndRoundUp(Items.Num(), Columns);
	return FMath::Max(0, Rows - VisibleRows);
}

void ASWGHoloShelfActor::Scroll(int32 Rows)
{
	TargetScrollRow = FMath::Clamp(FMath::RoundToInt(TargetScrollRow) + Rows, 0, MaxScrollRow());
}

void ASWGHoloShelfActor::ScrollTo(int64 ObjectId)
{
	const int32 Index = IndexOf(ObjectId);
	if (Index == INDEX_NONE)
	{
		return;
	}
	const int32 Row = Index / Columns;
	const int32 Top = FMath::RoundToInt(TargetScrollRow);
	if (Row < Top)
	{
		Scroll(Row - Top);
	}
	else if (Row >= Top + VisibleRows)
	{
		Scroll(Row - (Top + VisibleRows - 1));
	}
}

void ASWGHoloShelfActor::SetHovered(int64 ObjectId)
{
	HoveredId = ObjectId;
}

FVector ASWGHoloShelfActor::CellLocation(int32 Index) const
{
	const int32 Column = Index % Columns;
	const float Row = Index / Columns - CurrentScrollRow;
	return FVector(0.f, Column * ColumnSpacing, -Row * RowSpacing);
}

float ASWGHoloShelfActor::RowVisibility(int32 Index) const
{
	const float Row = Index / Columns - CurrentScrollRow;
	return FMath::Clamp(FMath::Min(Row + 1.f, VisibleRows - Row), 0.f, 1.f);
}

bool ASWGHoloShelfActor::GetItemCenter(int64 ObjectId, FVector& OutWorld) const
{
	const int32 Index = IndexOf(ObjectId);
	if (Index == INDEX_NONE || RowVisibility(Index) < 0.5f)
	{
		return false;
	}
	OutWorld = GetActorTransform().TransformPosition(CellLocation(Index));
	return true;
}

TArray<int64> ASWGHoloShelfActor::GetVisibleItems() const
{
	TArray<int64> Visible;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		if (RowVisibility(Index) >= 0.5f)
		{
			Visible.Add(Items[Index].ObjectId);
		}
	}
	return Visible;
}

int32 ASWGHoloShelfActor::CountHiddenBefore() const
{
	return FMath::Min(Items.Num(), FMath::RoundToInt(CurrentScrollRow) * Columns);
}

int32 ASWGHoloShelfActor::CountHiddenAfter() const
{
	return FMath::Max(0, Items.Num() - (FMath::RoundToInt(CurrentScrollRow) + VisibleRows) * Columns);
}

void ASWGHoloShelfActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Layout(DeltaSeconds);
}

void ASWGHoloShelfActor::Layout(float DeltaSeconds)
{
	CurrentScrollRow = FMath::FInterpConstantTo(CurrentScrollRow, TargetScrollRow, DeltaSeconds, ScrollSpeed * FMath::Max(1.f, FMath::Abs(TargetScrollRow - CurrentScrollRow)));
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		FShelfItem& Item = Items[Index];
		const float Visibility = RowVisibility(Index);
		const bool bHovered = Item.ObjectId == HoveredId;
		Item.Glow = FMath::FInterpTo(Item.Glow, bHovered ? 1.f : 0.f, DeltaSeconds, 12.f);
		Item.Spin = FMath::Fmod(Item.Spin + DeltaSeconds * SpinDegreesPerSecond * (bHovered ? HoverSpinFactor : 1.f), 360.f);
		const bool bShown = bShelfShown && Visibility > 0.01f
			&& !(ItemMask && ItemMask(GetActorTransform().TransformPosition(CellLocation(Index))));
		Item.Pivot->SetVisibility(bShown, /*bPropagateToChildren=*/true);
		if (!bShown)
		{
			continue;
		}
		Item.Pivot->SetRelativeLocation(CellLocation(Index));
		Item.Pivot->SetRelativeRotation(FRotator(0.f, Item.Spin, 0.f));
		Item.Pivot->SetRelativeScale3D(FVector(1.f + Item.Glow * HoverGrowth));
		Item.Material->SetScalarParameterValue(TEXT("Intensity"), HoloIntensity * Visibility * (1.f + Item.Glow * (HoverGlowFactor - 1.f)));
	}
}
