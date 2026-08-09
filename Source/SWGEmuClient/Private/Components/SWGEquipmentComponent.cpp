#include "Components/SWGEquipmentComponent.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Algo/AllOf.h"

USWGEquipmentComponent::USWGEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGEquipmentComponent::ApplyBase6(FSWGPacket& Packet)
{
	EquipmentList = ReadBaselineVector<FEquiptmentItem>(Packet, [](FSWGPacket& P)
	{
		FEquiptmentItem Item;
		Item.Deserialize(P);
		return Item;
	});

	AlternateAppearance = Packet.ReadAsciiString();
	bHasBase6 = true;

	BuildEquipmentVisuals(EquipmentList.Items);
}

void USWGEquipmentComponent::ApplyDelta6(FSWGPacket& Packet)
{

}

void USWGEquipmentComponent::BuildEquipmentVisuals(const TConstArrayView<FEquiptmentItem> ChangedEquipment)
{
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

	for (const FEquiptmentItem& Item : ChangedEquipment)
	{
		if (!SWGIsSlottedArrangement(Item.ContainmentType))
		{
			continue;
		}

		const uint64 ObjectId = Item.ObjectId;
		MeshGen->RequestItemMesh(Item.TemplateCRC, Item.ContainmentType,
			[this](UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials)
			{
				AttachMeshToHardpoint(Mesh, MeshData, Materials);
			},
			[this, ObjectId](USkeletalMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials)
			{
				AttachWearableSkeletalMesh(ObjectId, Mesh, Materials);
			});
	}
}


void USWGEquipmentComponent::AttachMeshToHardpoint(UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials, int32 RetryCount)
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
		static constexpr int32 MaxRetries = 120; // ~2s at 60fps
		if (RetryCount >= MaxRetries)
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGEquipmentComponent: gave up waiting for %s's skeletal mesh after %d retries"), *GetOwner()->GetName(), RetryCount);
			return;
		}

		TWeakObjectPtr<USWGEquipmentComponent> WeakThis(this);
		GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis, Mesh, MeshData, Materials, RetryCount]()
		{
			if (USWGEquipmentComponent* StrongThis = WeakThis.Get())
			{
				StrongThis->AttachMeshToHardpoint(Mesh, MeshData, Materials, RetryCount + 1);
			}
		});
		return;
	}

	UStaticMeshComponent* ItemMeshComponent = NewObject<UStaticMeshComponent>(GetOwner());
	ItemMeshComponent->SetRelativeTransform(FTransform::Identity);
	ItemMeshComponent->SetStaticMesh(Mesh);
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		ItemMeshComponent->SetMaterial(i, Materials[i]);
	}
	ItemMeshComponent->RegisterComponent();
	ItemMeshComponent->AttachToComponent(SkeletalMeshComponent, FAttachmentTransformRules::KeepRelativeTransform, FName("hold_r"));
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
		static constexpr int32 MaxRetries = 120; // ~2s at 60fps
		if (RetryCount >= MaxRetries)
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGEquipmentComponent: gave up waiting for %s's body skeletal mesh after %d retries — wearable %llu not attached"), *GetOwner()->GetName(), RetryCount, ObjectId);
			return;
		}

		// TODO: switch this to wait on the body mesh's OnSkeletalMeshChanged delegate instead a timer
		TWeakObjectPtr<USWGEquipmentComponent> WeakThis(this);
		GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis, ObjectId, Mesh, Materials, RetryCount]()
		{
			if (USWGEquipmentComponent* StrongThis = WeakThis.Get())
			{
				StrongThis->AttachWearableSkeletalMesh(ObjectId, Mesh, Materials, RetryCount + 1);
			}
		});
		return;
	}

	// Reuse the existing component on re-equip (e.g. a redundant BuildEquipmentVisuals
	// pass for an item that's already attached) instead of spawning a duplicate.
	USkeletalMeshComponent* WearableComponent = WearableComponentsByObjectId.FindRef(ObjectId);
	if (!WearableComponent)
	{
		WearableComponent = NewObject<USkeletalMeshComponent>(GetOwner());
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

	UE_LOG(LogTemp, Warning, TEXT("USWGEquipmentComponent: ReconcileBodyOcclusion — %s covered zones: [%s]"),
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

		UE_LOG(LogTemp, Warning, TEXT("USWGEquipmentComponent:   section %d zones=[%s] -> %s"),
			SectionIndex, *FString::Join(SectionZones, TEXT(", ")), bFullyCovered ? TEXT("HIDE") : TEXT("show"));

		BodyMesh->ShowMaterialSection(SectionIndex, SectionIndex, !bFullyCovered, /*LODIndex=*/0);
	}
}
