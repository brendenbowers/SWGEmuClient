#include "Components/SWGEquipmentComponent.h"
#include "Common/SWGLightingChannels.h"
#include "UObject/StrongObjectPtr.h"
#include "Network/SWGPacket.h"
#include "Customization/SWGCustomizationVariables.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Common/SWGWeaponHardpoints.h"
#include "Components/SWGLightsaberComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Subsystems/SWGCombatVisualsSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGMeshReader.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Algo/AllOf.h"

namespace
{
	// The body build sits in the same LIFO mesh queue as every other spawn,
	// so on a busy zone-in it can be tens of seconds behind an item's mesh.
	constexpr float BodyMeshPollInterval = 0.25f;
	constexpr int32 BodyMeshMaxRetries = 240; // 60s

	// Blade colour when nothing names one (Luke's blue).
	const FLinearColor DefaultBladeColor(0.3f, 0.55f, 1.f);
}

USWGEquipmentComponent::USWGEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGEquipmentComponent::ApplyBase6(const FCreatureObjectBaseline& Baseline)
{
	EquipmentList = Baseline.EquipmentList;
	AlternateAppearance = Baseline.AlternateAppearance;
	bHasBase6 = true;
	// A CREO6 refresh may carry new customization for an already-attached item.
	RequestedItemIds.Reset();

	BuildEquipmentVisuals(GatherCurrentEquipment());
}

void USWGEquipmentComponent::SetContainedItem(const FEquiptmentItem& Item)
{
	ContainedEquipment.Add(Item.ObjectId, Item);
	RequestedItemIds.Remove(Item.ObjectId);
	BuildEquipmentVisuals(GatherCurrentEquipment());
}

void USWGEquipmentComponent::RemoveContainedItem(uint64 ObjectId)
{
	if (ContainedEquipment.Remove(ObjectId) > 0)
	{
		BuildEquipmentVisuals(GatherCurrentEquipment());
	}
}

TArray<FEquiptmentItem> USWGEquipmentComponent::GatherCurrentEquipment() const
{
	TArray<FEquiptmentItem> Result = EquipmentList.Items;
	for (const TPair<uint64, FEquiptmentItem>& Pair : ContainedEquipment)
	{
		const uint64 ObjectId = Pair.Key;
		if (!Result.ContainsByPredicate([ObjectId](const FEquiptmentItem& Existing) { return Existing.ObjectId == ObjectId; }))
		{
			Result.Add(Pair.Value);
		}
	}
	return Result;
}

void USWGEquipmentComponent::ApplyDelta6(const FCreatureObjectDelta& Delta)
{
	if (Delta.AlternateAppearance.IsSet())
	{
		AlternateAppearance = *Delta.AlternateAppearance;
	}

	if (Delta.EquipmentList.Changes.IsEmpty())
	{
		return;
	}

	ApplyIndexedListChanges(Delta.EquipmentList, EquipmentList);
	RequestedItemIds.Reset();

	// Rebuilds against the whole list rather than the changed entries: a removal
	// carries only an index, so the attached meshes can't be reconciled from the
	// change set alone.
	BuildEquipmentVisuals(GatherCurrentEquipment());
}

void USWGEquipmentComponent::SetClientDataWearables(const TArray<FSWGClientDataWearable>& Wearables)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGen = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	if (!MeshGen)
	{
		return;
	}

	// Index-derived ids keep a repeat call idempotent: AttachWearableSkeletalMesh
	// reuses the component already registered under the same id.
	uint64 NextId = ClientDataWearableIdBase;
	for (const FSWGClientDataWearable& Wearable : Wearables)
	{
		for (const FString& LmgPath : Wearable.MeshPaths)
		{
			const uint64 WearableId = NextId++;
			ClientDataWearableIds.Add(WearableId);

			TWeakObjectPtr<USWGEquipmentComponent> WeakThis(this);
			MeshGen->RequestWearableMesh(LmgPath, Wearable.Customization,
				[WeakThis, WearableId](USkeletalMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials)
				{
					if (USWGEquipmentComponent* Equipment = WeakThis.Get())
					{
						Equipment->AttachWearableSkeletalMesh(WearableId, Mesh, Materials);
					}
				});
		}
	}
}

void USWGEquipmentComponent::SetMountCovering(const FString& MountAppearancePath)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGen = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	if (!MeshGen)
	{
		return;
	}

	// Their own id range so a mount's cdf wearables (SetClientDataWearables) keep their indices.
	uint64 NextId = ClientDataWearableIdBase | 0x00FF0000ull;
	for (const FString& LmgPath : MeshGen->ResolveMountCoveringMeshes(MountAppearancePath))
	{
		const uint64 WearableId = NextId++;
		ClientDataWearableIds.Add(WearableId);
		TWeakObjectPtr<USWGEquipmentComponent> WeakThis(this);
		MeshGen->RequestWearableMesh(LmgPath, {},
			[WeakThis, WearableId](USkeletalMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials)
			{
				if (USWGEquipmentComponent* Equipment = WeakThis.Get())
				{
					Equipment->AttachWearableSkeletalMesh(WearableId, Mesh, Materials);
				}
			});
	}
}

void USWGEquipmentComponent::ApplySlotRules()
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGen = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	if (!MeshGen || !MeshGen->GetSlotRuleManager())
	{
		return;
	}

	TMap<uint64, TArray<FString>> SlotsByItem;
	TSet<FString> Occupied;
	for (const FEquiptmentItem& Item : GatherCurrentEquipment())
	{
		TArray<FString> Slots;
		if (SWGIsSlottedArrangement(Item.ContainmentType) && MeshGen->ResolveArrangementSlotNames(Item.TemplateCRC, Item.ContainmentType, Slots))
		{
			Occupied.Append(Slots);
			SlotsByItem.Add(Item.ObjectId, MoveTemp(Slots));
		}
	}

	for (const TPair<uint64, TArray<FString>>& Pair : SlotsByItem)
	{
		USkeletalMeshComponent* Wearable = WearableComponentsByObjectId.FindRef(Pair.Key);
		if (!Wearable)
		{
			continue;
		}
		for (const TPair<FString, float>& Morph : MeshGen->ResolveSlotRuleMorphs(Pair.Value, Occupied))
		{
			Wearable->SetMorphTarget(FName(*Morph.Key), Morph.Value);
		}
	}
}

TArray<FString> USWGEquipmentComponent::DescribeWearables() const
{
	TArray<FString> Lines;
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGen = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	const USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (!MeshGen)
	{
		return Lines;
	}

	TMap<uint64, TArray<FString>> SlotsByItem;
	TSet<FString> Occupied;
	const TArray<FEquiptmentItem> Items = GatherCurrentEquipment();
	for (const FEquiptmentItem& Item : Items)
	{
		TArray<FString> Slots;
		if (SWGIsSlottedArrangement(Item.ContainmentType) && MeshGen->ResolveArrangementSlotNames(Item.TemplateCRC, Item.ContainmentType, Slots))
		{
			Occupied.Append(Slots);
			SlotsByItem.Add(Item.ObjectId, MoveTemp(Slots));
		}
	}
	Lines.Add(FString::Printf(TEXT("%s: %d item(s), %d wearable mesh(es), occupied slots: %s"), *GetOwner()->GetName(), Items.Num(), WearableComponentsByObjectId.Num(),
		*FString::Join(Occupied.Array(), TEXT(", "))));
	for (const FEquiptmentItem& Item : Items)
	{
		const TArray<FString>* Slots = SlotsByItem.Find(Item.ObjectId);
		const USkeletalMeshComponent* Wearable = WearableComponentsByObjectId.FindRef(Item.ObjectId);
		FString Morphs;
		if (Slots)
		{
			for (const TPair<FString, float>& Morph : MeshGen->ResolveSlotRuleMorphs(*Slots, Occupied))
			{
				Morphs += FString::Printf(TEXT(" %s=%.0f"), *Morph.Key, Morph.Value);
			}
		}
		Lines.Add(FString::Printf(TEXT("  %llu %s containment=%d slots=[%s] mesh=%s slotRuleMorphs:%s"), Item.ObjectId,
			Tre ? *Tre->ResolveTemplatePath(Item.TemplateCRC) : TEXT("?"), Item.ContainmentType,
			Slots ? *FString::Join(*Slots, TEXT(",")) : TEXT(""), Wearable ? TEXT("attached") : TEXT("none"), Morphs.IsEmpty() ? TEXT(" (none)") : *Morphs));
	}
	for (const uint64 Id : ClientDataWearableIds)
	{
		Lines.Add(FString::Printf(TEXT("  client wearable %llx mesh=%s"), Id, WearableComponentsByObjectId.Contains(Id) ? TEXT("attached") : TEXT("pending")));
	}
	return Lines;
}

void USWGEquipmentComponent::SetPreviewEquipment(TArray<FEquiptmentItem> InEquipment, FString InAlternateAppearance)
{
	EquipmentList.Items = MoveTemp(InEquipment);
	AlternateAppearance = MoveTemp(InAlternateAppearance);
	RequestedItemIds.Reset();
	BuildEquipmentVisuals(EquipmentList.Items);
}

void USWGEquipmentComponent::RemoveUnequippedVisuals(const TConstArrayView<FEquiptmentItem> CurrentEquipment)
{
	TSet<uint64> EquippedIds;
	EquippedIds.Reserve(CurrentEquipment.Num());
	for (const FEquiptmentItem& Item : CurrentEquipment)
	{
		if (SWGIsSlottedArrangement(Item.ContainmentType))
		{
			EquippedIds.Add(Item.ObjectId);
		}
	}

	for (auto It = WearableComponentsByObjectId.CreateIterator(); It; ++It)
	{
		if (EquippedIds.Contains(It.Key()) || ClientDataWearableIds.Contains(It.Key()))
		{
			continue;
		}

		if (USkeletalMeshComponent* WearableComponent = It.Value())
		{
			WearableComponent->DestroyComponent();
		}
		It.RemoveCurrent();
	}

	for (auto It = HardpointComponentsByObjectId.CreateIterator(); It; ++It)
	{
		if (EquippedIds.Contains(It.Key()) || DebugItemIds.Contains(It.Key()))
		{
			continue;
		}

		ReleaseItemVisual(It.Key(), It.Value());
		It.RemoveCurrent();
	}

	// An unequipped item's in-flight request, if any, still completes and
	// attaches — same as before; a re-equip of that id must request again.
	for (auto It = RequestedItemIds.CreateIterator(); It; ++It)
	{
		if (!EquippedIds.Contains(*It))
		{
			It.RemoveCurrent();
		}
	}

	// Unequipping the last wearable produces no attach callback, so the body's
	// hidden sections would otherwise never be re-shown.
	ReconcileBodyOcclusion();
	ApplySlotRules();
}

void USWGEquipmentComponent::BuildEquipmentVisuals(const TConstArrayView<FEquiptmentItem> CurrentEquipment)
{
	RemoveUnequippedVisuals(CurrentEquipment);

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return;
	}

	USWGMeshGeneratorSubsystem* MeshGen = GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>();
	if(!MeshGen)
	{
		return;
	}

	for (const FEquiptmentItem& Item : CurrentEquipment)
	{
		if (!SWGIsSlottedArrangement(Item.ContainmentType))
		{
			continue;
		}

		// Containment-fed gear arrives one item at a time, each pass rebuilding
		// from the whole list — without this every item would be re-requested
		// per arrival.
		bool bAlreadyRequested = false;
		RequestedItemIds.Add(Item.ObjectId, &bAlreadyRequested);
		if (bAlreadyRequested)
		{
			continue;
		}

		FSWGCustomizationVariables ItemCustomization;
		if (!FSWGCustomizationVariables::Parse(Item.CustomizationBytes, ItemCustomization))
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGEquipmentComponent: item %llu failed to decode customization data (%d byte(s))"),
				Item.ObjectId, Item.CustomizationBytes.Num());
		}

		const uint64 ObjectId = Item.ObjectId;
		TWeakObjectPtr<USWGEquipmentComponent> WeakThis(this);
		MeshGen->RequestItemMesh(Item.TemplateCRC, Item.ContainmentType, ItemCustomization,
			[WeakThis, ObjectId](UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials)
			{
				if (USWGEquipmentComponent* Equipment = WeakThis.Get())
				{
					Equipment->AttachMeshToHardpoint(ObjectId, Mesh, MeshData, Materials);
				}
			},
			[WeakThis, ObjectId](USkeletalMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials)
			{
				if (USWGEquipmentComponent* Equipment = WeakThis.Get())
				{
					Equipment->AttachWearableSkeletalMesh(ObjectId, Mesh, Materials);
				}
			});
	}
}


UMeshComponent* USWGEquipmentComponent::FindItemVisual(uint64 ObjectId) const
{
	if (USkeletalMeshComponent* Wearable = WearableComponentsByObjectId.FindRef(ObjectId))
	{
		return Wearable;
	}
	return HardpointComponentsByObjectId.FindRef(ObjectId);
}

void USWGEquipmentComponent::AttachMeshToHardpoint(uint64 ObjectId, UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials, int32 RetryCount)
{
	if (!Mesh || !GetOwner())
	{
		return;
	}

	// ACharacter::GetMesh(), not FindComponentByClass<USkeletalMeshComponent>()
	// — wearable items (see AttachWearableSkeletalMesh) add their own
	// USkeletalMeshComponents to this same actor, and FindComponentByClass
	// returns whichever one happens to be first, not necessarily the body.
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* SkeletalMeshComponent = Character ? Character->GetMesh() : nullptr;
	if (!SkeletalMeshComponent)
	{
		return;
	}

	//todo: watch for hte mesh to beready
	if (!SkeletalMeshComponent->GetSkeletalMeshAsset())
	{
		if (RetryCount >= BodyMeshMaxRetries)
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGEquipmentComponent: gave up waiting for %s's skeletal mesh after %d retries"), *GetOwner()->GetName(), RetryCount);
			return;
		}

		// Root the mesh/materials across the wait — see AttachWearableSkeletalMesh.
		TWeakObjectPtr<USWGEquipmentComponent> WeakThis(this);
		TStrongObjectPtr<UStaticMesh> HeldMesh(Mesh);
		TArray<TStrongObjectPtr<UMaterialInterface>> HeldMaterials;
		for (UMaterialInterface* Material : Materials)
		{
			HeldMaterials.Emplace(Material);
		}
		FTimerHandle Unused;
		GetWorld()->GetTimerManager().SetTimer(Unused, [WeakThis, ObjectId, HeldMesh, MeshData, HeldMaterials, RetryCount]()
		{
			if (USWGEquipmentComponent* StrongThis = WeakThis.Get())
			{
				TArray<UMaterialInterface*> RetryMaterials;
				for (const TStrongObjectPtr<UMaterialInterface>& Material : HeldMaterials)
				{
					RetryMaterials.Add(Material.Get());
				}
				StrongThis->AttachMeshToHardpoint(ObjectId, HeldMesh.Get(), MeshData, RetryMaterials, RetryCount + 1);
			}
		}, BodyMeshPollInterval, false);
		return;
	}

	// Reuse on re-equip, for the same reason as AttachWearableSkeletalMesh: every
	// equipment change rebuilds from the whole list, so this runs again for items
	// that were already attached.
	UStaticMeshComponent* ItemMeshComponent = HardpointComponentsByObjectId.FindRef(ObjectId);
	if (!ItemMeshComponent)
	{
		ItemMeshComponent = NewObject<UStaticMeshComponent>(GetOwner());
		ItemMeshComponent->SetRelativeTransform(FTransform::Identity);
		ItemMeshComponent->RegisterComponent();
		ItemMeshComponent->AttachToComponent(SkeletalMeshComponent, FAttachmentTransformRules::KeepRelativeTransform, FName("hold_r"));
		HardpointComponentsByObjectId.Add(ObjectId, ItemMeshComponent);
	}

	ItemMeshComponent->SetStaticMesh(Mesh);
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		ItemMeshComponent->SetMaterial(i, Materials[i]);
	}

	BuildItemHardpoints(ObjectId, *ItemMeshComponent, MeshData.Hardpoints);
	if (MeshData.SourceAppearancePath.EndsWith(TEXT(".lsb")))
	{
		EnsureSaber(ObjectId, *ItemMeshComponent, MeshData.SourceAppearancePath);
	}
}

void USWGEquipmentComponent::AttachWearableSkeletalMesh(uint64 ObjectId, USkeletalMesh* Mesh, const TArray<UMaterialInterface*>& Materials, int32 RetryCount)
{
	if (!Mesh || !GetOwner())
	{
		return;
	}

	// See AttachMeshToHardpoint's matching comment for why this is
	// ACharacter::GetMesh(), not FindComponentByClass<USkeletalMeshComponent>().
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* BodyMesh = Character ? Character->GetMesh() : nullptr;
	if (!BodyMesh)
	{
		return;
	}

	if (!BodyMesh->GetSkeletalMeshAsset())
	{
		if (RetryCount >= BodyMeshMaxRetries)
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGEquipmentComponent: gave up waiting for %s's body skeletal mesh after %d retries — wearable %llu not attached"), *GetOwner()->GetName(), RetryCount, ObjectId);
			return;
		}

		// TODO: switch this to wait on the body mesh's OnSkeletalMeshChanged delegate instead a timer
		// Nothing else references the freshly built mesh/materials while we
		// wait, so the lambda has to root them or GC collects them mid-retry.
		TWeakObjectPtr<USWGEquipmentComponent> WeakThis(this);
		TStrongObjectPtr<USkeletalMesh> HeldMesh(Mesh);
		TArray<TStrongObjectPtr<UMaterialInterface>> HeldMaterials;
		for (UMaterialInterface* Material : Materials)
		{
			HeldMaterials.Emplace(Material);
		}
		FTimerHandle Unused;
		GetWorld()->GetTimerManager().SetTimer(Unused, [WeakThis, ObjectId, HeldMesh, HeldMaterials, RetryCount]()
		{
			if (USWGEquipmentComponent* StrongThis = WeakThis.Get())
			{
				TArray<UMaterialInterface*> RetryMaterials;
				for (const TStrongObjectPtr<UMaterialInterface>& Material : HeldMaterials)
				{
					RetryMaterials.Add(Material.Get());
				}
				StrongThis->AttachWearableSkeletalMesh(ObjectId, HeldMesh.Get(), RetryMaterials, RetryCount + 1);
			}
		}, BodyMeshPollInterval, false);
		return;
	}

	// Reuse the existing component on re-equip (e.g. a redundant BuildEquipmentVisuals
	// pass for an item that's already attached) instead of spawning a duplicate.
	USkeletalMeshComponent* WearableComponent = WearableComponentsByObjectId.FindRef(ObjectId);
	if (!WearableComponent)
	{
		WearableComponent = NewObject<USkeletalMeshComponent>(GetOwner());
		SWGSetInteriorLightingChannel(*WearableComponent, /*bAlsoWorld*/ true);
		WearableComponent->SetRelativeTransform(FTransform::Identity);
		WearableComponent->RegisterComponent();
		WearableComponent->AttachToComponent(BodyMesh, FAttachmentTransformRules::KeepRelativeTransform);
		WearableComponentsByObjectId.Add(ObjectId, WearableComponent);
	}

	// SetLeaderPoseComponent must come after SetSkeletalMesh on both sides —
	// the engine builds the leader/follower bone-name map at link time, and
	// won't refresh it automatically if either mesh changes later.
	WearableComponent->SetSkeletalMesh(Mesh);
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		WearableComponent->SetMaterial(i, Materials[i]);
	}
	WearableComponent->SetLeaderPoseComponent(BodyMesh);

	ReconcileBodyOcclusion();
	ApplySlotRules();
}

void USWGEquipmentComponent::ReconcileBodyOcclusion()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* BodyMesh = Character ? Character->GetMesh() : nullptr;
	USkeletalMesh* BodyMeshAsset = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	const USWGMeshOcclusionZoneData* BodyZoneData = BodyMeshAsset ? BodyMeshAsset->GetAssetUserData<USWGMeshOcclusionZoneData>() : nullptr;
	if (!BodyZoneData)
	{
		return;
	}

	TSet<FString> CoveredZones;
	for (const TPair<uint64, TObjectPtr<USkeletalMeshComponent>>& Pair : WearableComponentsByObjectId)
	{
		USkeletalMeshComponent* WearableComponent = Pair.Value;
		USkeletalMesh* WearableMeshAsset = WearableComponent ? WearableComponent->GetSkeletalMeshAsset() : nullptr;
		const USWGMeshOcclusionZoneData* WearableZoneData = WearableMeshAsset ? WearableMeshAsset->GetAssetUserData<USWGMeshOcclusionZoneData>() : nullptr;
		if (!WearableZoneData)
		{
			continue;
		}
		for (const FSWGMeshSectionOcclusionZones& Section : WearableZoneData->ZoneNamesBySection)
		{
			CoveredZones.Append(Section.ZoneNames);
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("USWGEquipmentComponent: ReconcileBodyOcclusion — %s covered zones: [%s]"),
		*BodyMeshAsset->GetName(), *FString::Join(CoveredZones.Array(), TEXT(", ")));

	for (int32 SectionIndex = 0; SectionIndex < BodyZoneData->ZoneNamesBySection.Num(); ++SectionIndex)
	{
		const TArray<FString>& SectionZones = BodyZoneData->ZoneNamesBySection[SectionIndex].ZoneNames;
		if (SectionZones.IsEmpty())
		{
			continue;
		}

		const bool bFullyCovered = Algo::AllOf(SectionZones, [&CoveredZones](const FString& ZoneName)
		{
			return CoveredZones.Contains(ZoneName);
		});

		UE_LOG(LogTemp, Verbose, TEXT("USWGEquipmentComponent:   section %d zones=[%s] -> %s"),
			SectionIndex, *FString::Join(SectionZones, TEXT(", ")), bFullyCovered ? TEXT("HIDE") : TEXT("show"));

		BodyMesh->ShowMaterialSection(SectionIndex, SectionIndex, !bFullyCovered, /*LODIndex=*/0);
	}
}

void USWGEquipmentComponent::BuildItemHardpoints(uint64 ObjectId, UStaticMeshComponent& ItemMesh, const TArray<FSWGMeshHardpoint>& Hardpoints)
{
	FSWGItemHardpoints& Set = HardpointsByObjectId.FindOrAdd(ObjectId);
	for (const FSWGMeshHardpoint& Hardpoint : Hardpoints)
	{
		const FName Name(*Hardpoint.Name);
		USceneComponent* Point = Set.ByName.FindRef(Name);
		if (!Point)
		{
			Point = NewObject<USceneComponent>(GetOwner());
			Point->RegisterComponent();
			Point->AttachToComponent(&ItemMesh, FAttachmentTransformRules::KeepRelativeTransform);
			Set.ByName.Add(Name, Point);
		}
		Point->SetRelativeTransform(FTransform(Hardpoint.Rotation, Hardpoint.Translation));
	}
}

USceneComponent* USWGEquipmentComponent::GetItemHardpoint(uint64 ObjectId, FName Name) const
{
	const FSWGItemHardpoints* Set = HardpointsByObjectId.Find(ObjectId);
	return Set ? Set->ByName.FindRef(Name).Get() : nullptr;
}

USceneComponent* USWGEquipmentComponent::GetWeaponHardpoint(FName Name, uint64 WeaponObjectId) const
{
	if (WeaponObjectId != 0)
	{
		if (USceneComponent* Point = GetItemHardpoint(WeaponObjectId, Name))
		{
			return Point;
		}
	}
	for (const TPair<uint64, FSWGItemHardpoints>& Pair : HardpointsByObjectId)
	{
		if (USceneComponent* Point = Pair.Value.ByName.FindRef(Name).Get())
		{
			return Point;
		}
	}
	return nullptr;
}

FString USWGEquipmentComponent::GetItemTemplatePath(uint64 ObjectId) const
{
	if (const FString* DebugPath = DebugTemplatePaths.Find(ObjectId))
	{
		return *DebugPath;
	}

	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	for (const FEquiptmentItem& Item : GatherCurrentEquipment())
	{
		if (Item.ObjectId == ObjectId)
		{
			return Tre ? Tre->ResolveTemplatePath(Item.TemplateCRC) : FString();
		}
	}
	return FString();
}

void USWGEquipmentComponent::EnsureSaber(uint64 ObjectId, UStaticMeshComponent& ItemMesh, const FString& LsbPath)
{
	if (SaberByObjectId.FindRef(ObjectId))
	{
		return;
	}

	// Blade n runs from hardpoint tr<n>s to tr<n>e; a hilt without them gets one blade up its +Z.
	TArray<FSWGSaberBladeAxis> Axes;
	for (const TPair<FName, FName>& Edge : { TPair<FName, FName>(SWGWeaponHardpoint::TrailStart, SWGWeaponHardpoint::TrailEnd),
		TPair<FName, FName>(SWGWeaponHardpoint::SecondTrailStart, SWGWeaponHardpoint::SecondTrailEnd) })
	{
		const USceneComponent* Start = GetItemHardpoint(ObjectId, Edge.Key);
		const USceneComponent* End = GetItemHardpoint(ObjectId, Edge.Value);
		if (Start && End)
		{
			FSWGSaberBladeAxis& Axis = Axes.AddDefaulted_GetRef();
			Axis.Base = Start->GetRelativeLocation();
			Axis.Direction = (End->GetRelativeLocation() - Start->GetRelativeLocation()).GetSafeNormal();
		}
	}

	// The template names the blade colour (its swoosh) and the ignite / retract effects.
	FSWGSaberStyle Style;
	Style.Tint = DefaultBladeColor;
	const FString TemplatePath = GetItemTemplatePath(ObjectId);
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (USWGCombatVisualsSubsystem* Visuals = GameInstance ? GameInstance->GetSubsystem<USWGCombatVisualsSubsystem>() : nullptr)
	{
		if (const FSWGWeaponVisuals* Weapon = Visuals->GetWeaponVisuals(TemplatePath))
		{
			// A saber with its own colour swoosh keeps it; the rest (object-hue sabers, whose hue the item customization would set) default to blue.
			Style.Tint = Weapon->bHasSwoosh && !Weapon->bSwooshUseObjectHue ? Weapon->Swoosh.Color : DefaultBladeColor;
			Style.BladeOnEffect = Weapon->BladeOnEffect;
			Style.BladeOffEffect = Weapon->BladeOffEffect;
		}
	}

	// The blade colour proper is the weapon's index_color_blade palette variable (SWGEmu.exe 0x64e890, 0x7ac280).
	if (USWGMeshGeneratorSubsystem* MeshGen = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr)
	{
		for (const FEquiptmentItem& Item : GatherCurrentEquipment())
		{
			FSWGCustomizationVariables Customization;
			FLinearColor BladeColor;
			if (Item.ObjectId == ObjectId && FSWGCustomizationVariables::Parse(Item.CustomizationBytes, Customization)
				&& MeshGen->ResolveBladeColor(Customization, LsbPath, BladeColor))
			{
				Style.Tint = BladeColor;
				break;
			}
		}
	}

	USWGLightsaberComponent* Saber = NewObject<USWGLightsaberComponent>(GetOwner());
	Saber->RegisterComponent();
	Saber->AttachToComponent(&ItemMesh, FAttachmentTransformRules::KeepRelativeTransform);
	if (!Saber->Setup(LsbPath, Axes, Style))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGEquipmentComponent: could not set up the lightsaber %s"), *LsbPath);
		Saber->DestroyComponent();
		return;
	}
	SaberByObjectId.Add(ObjectId, Saber);
}

void USWGEquipmentComponent::ReleaseItemVisual(uint64 ObjectId, UStaticMeshComponent* ItemMesh)
{
	if (FSWGItemHardpoints* Set = HardpointsByObjectId.Find(ObjectId))
	{
		for (const TPair<FName, TObjectPtr<USceneComponent>>& Pair : Set->ByName)
		{
			if (Pair.Value)
			{
				Pair.Value->DestroyComponent();
			}
		}
		HardpointsByObjectId.Remove(ObjectId);
	}

	// A saber closes its blade before the hilt goes.
	float LingerSeconds = 0.f;
	if (USWGLightsaberComponent* Saber = SaberByObjectId.FindRef(ObjectId))
	{
		LingerSeconds = Saber->GetCloseSeconds();
		Saber->Retract();
		SaberByObjectId.Remove(ObjectId);
	}

	if (!ItemMesh)
	{
		return;
	}
	if (LingerSeconds <= 0.f || !GetWorld())
	{
		ItemMesh->DestroyComponent();
		return;
	}
	TWeakObjectPtr<UStaticMeshComponent> WeakItemMesh(ItemMesh);
	FTimerHandle Unused;
	GetWorld()->GetTimerManager().SetTimer(Unused, FTimerDelegate::CreateLambda([WeakItemMesh]()
	{
		if (UStaticMeshComponent* Live = WeakItemMesh.Get())
		{
			Live->DestroyComponent();
		}
	}), LingerSeconds + 0.1f, false);
}

void USWGEquipmentComponent::AttachDebugItem(const FString& AppearancePath, const FString& TemplatePath)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGen = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	if (!MeshGen)
	{
		return;
	}

	for (const uint64 DebugId : DebugItemIds)
	{
		if (const TObjectPtr<UStaticMeshComponent>* Existing = HardpointComponentsByObjectId.Find(DebugId))
		{
			ReleaseItemVisual(DebugId, *Existing);
		}
		HardpointComponentsByObjectId.Remove(DebugId);
	}
	DebugItemIds.Reset();
	DebugTemplatePaths.Reset();
	if (AppearancePath.IsEmpty())
	{
		return;
	}

	DebugItemIds.Add(DebugItemIdBase);
	DebugTemplatePaths.Add(DebugItemIdBase, TemplatePath);
	TWeakObjectPtr<USWGEquipmentComponent> WeakThis(this);
	MeshGen->RequestAppearanceMeshWithHardpoints(AppearancePath, [WeakThis, AppearancePath](UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials, const TArray<FSWGMeshHardpoint>& Hardpoints)
	{
		if (USWGEquipmentComponent* Equipment = WeakThis.Get())
		{
			FSWGMeshData Data;
			Data.Hardpoints = Hardpoints;
			Data.SourceAppearancePath = AppearancePath;
			Equipment->AttachMeshToHardpoint(DebugItemIdBase, Mesh, Data, Materials);
		}
	});
}
