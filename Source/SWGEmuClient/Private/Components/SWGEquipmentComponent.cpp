#include "Components/SWGEquipmentComponent.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"

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
		
		MeshGen->RequestItemStaticMesh(Item.TemplateCRC, [this](UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials)
		{
			AttachMeshToHardpoint(Mesh, MeshData, Materials);
		});
	}
}


void USWGEquipmentComponent::AttachMeshToHardpoint(UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials, int32 RetryCount)
{
	if (!Mesh || !GetOwner())
	{
		return;
	}

	USkeletalMeshComponent* SkeletalMeshComponent = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
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
