#include "Components/SWGTangibleComponent.h"
#include "Network/SWGPacket.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"

USWGTangibleComponent::USWGTangibleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGTangibleComponent::ApplyBase3Part1(FSWGPacket& Packet)
{
	Complexity = Packet.ReadFloat();
	ObjectName = FSWGStringId::Read(Packet);
	CustomName = Packet.ReadUnicodeString();
	Volume = Packet.ReadInt32();
	CustomizationString = Packet.ReadAsciiString();

	VisibleComponents = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });

	OptionsBitmask = Packet.ReadInt32();

	//UpdateNameLabel();
}

namespace
{
	// Scale to the owner's actual capsule size (2*HalfHeight = full standing
	// height) plus a small clearance margin above the head, so this scales
	// correctly across SWG's huge creature-size range instead of guessing one
	// flat number that's wildly wrong for most owners (250 above the root —
	// itself the capsule's *center*, not the feet — put the label ~3.4m up
	// for a normal ~1.76m-tall human). Non-Character owners (small items,
	// static props) fall back to a small flat clearance above their root.
	constexpr float HeadClearance = 20.0f;

	float ComputeHeightAboveRoot(const AActor* Owner)
	{
		if (const ACharacter* Character = Cast<ACharacter>(Owner))
		{
			if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				// Root sits at the capsule's center, so the label needs
				// (fullHeight - halfHeight) = halfHeight above the root to
				// clear the top of the head, plus the margin.
				return Capsule->GetScaledCapsuleHalfHeight() + HeadClearance;
			}
		}
		return HeadClearance;
	}
}

void USWGTangibleComponent::UpdateNameLabel()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!NameLabel)
	{
		NameLabel = NewObject<UTextRenderComponent>(Owner, NAME_None, RF_Transactional);
		NameLabel->SetupAttachment(Owner->GetRootComponent());
		NameLabel->SetHorizontalAlignment(EHTA_Center);
		NameLabel->SetVerticalAlignment(EVRTA_TextBottom);
		NameLabel->SetWorldSize(12.0f);
		NameLabel->SetTextRenderColor(FColor::Yellow);
		// No lighting/depth-test dependency for a debug label — always readable.
		NameLabel->SetCastShadow(false);
		NameLabel->SetRelativeLocation(FVector(0.0f, 0.0f, ComputeHeightAboveRoot(Owner)));
		NameLabel->RegisterComponent();
	}

	const FString DisplayName = !CustomName.IsEmpty()
		? CustomName
		: FString::Printf(TEXT("%s/%s"), *ObjectName.File, *ObjectName.StringTableId);

	NameLabel->SetText(FText::FromString(DisplayName));
}

void USWGTangibleComponent::RepositionNameLabel()
{
	if (!NameLabel)
	{
		return;
	}

	NameLabel->SetRelativeLocation(FVector(0.0f, 0.0f, ComputeHeightAboveRoot(GetOwner())));
}

void USWGTangibleComponent::ApplyBase3Part2(FSWGPacket& Packet)
{
	ObjectVisible = Packet.ReadByte();
	bHasBase3 = true;
}
