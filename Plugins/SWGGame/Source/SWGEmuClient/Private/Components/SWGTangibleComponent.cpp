#include "Components/SWGTangibleComponent.h"
#include "Network/SWGPacket.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Subsystems/SWGTreSubsystem.h"

USWGTangibleComponent::USWGTangibleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGTangibleComponent::ApplyBase3(const FTangibleObjectBaseline& Baseline)
{
	Complexity = Baseline.Complexity;
	ObjectName = Baseline.ObjectName;
	CustomName = Baseline.CustomName;
	Volume = Baseline.Volume;
	VisibleComponents = Baseline.VisibleComponents;
	OptionsBitmask = Baseline.OptionsBitmask;
	ObjectVisible = Baseline.ObjectVisible;
	bHasBase3 = true;

	CustomizationBytes = Baseline.CustomizationBytes;
	if (!FSWGCustomizationVariables::Parse(CustomizationBytes, DecodedCustomization))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTangibleComponent: %s failed to decode customization data (%d byte(s))"),
			*GetOwner()->GetName(), CustomizationBytes.Num());
	}
	else if (DecodedCustomization.Values.Num() > 0)
	{
		FString Dump;
		for (const TPair<uint8, int16>& Pair : DecodedCustomization.Values)
		{
			Dump += FString::Printf(TEXT("%d=%d "), Pair.Key, Pair.Value);
		}
		UE_LOG(LogTemp, Warning, TEXT("USWGTangibleComponent: %s customization: %s"), *GetOwner()->GetName(), *Dump);
	}

	UpdateNameLabel();
}

FSWGCustomizationVariables USWGTangibleComponent::GetEffectiveCustomization() const
{
	FSWGCustomizationVariables Result = ClientDataCustomization;
	for (const TPair<uint8, int16>& Pair : DecodedCustomization.Values)
	{
		Result.Values.Add(Pair.Key, Pair.Value);
	}
	return Result;
}

void USWGTangibleComponent::ApplyDelta3(const FTangibleObjectDelta& Delta)
{
	if (Delta.Complexity.IsSet())     { Complexity = *Delta.Complexity; }
	if (Delta.ObjectName.IsSet())     { ObjectName = *Delta.ObjectName; }
	if (Delta.CustomName.IsSet())     { CustomName = *Delta.CustomName; }
	if (Delta.Volume.IsSet())         { Volume = *Delta.Volume; }
	if (Delta.OptionsBitmask.IsSet()) { OptionsBitmask = *Delta.OptionsBitmask; }
	if (Delta.ObjectVisible.IsSet())  { ObjectVisible = *Delta.ObjectVisible; }

	ApplyIndexedListChanges(Delta.VisibleComponents, VisibleComponents);

	if (Delta.CustomizationBytes.IsSet())
	{
		CustomizationBytes = *Delta.CustomizationBytes;
		if (!FSWGCustomizationVariables::Parse(CustomizationBytes, DecodedCustomization))
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGTangibleComponent: %s failed to decode customization data (%d byte(s))"),
				*GetOwner()->GetName(), CustomizationBytes.Num());
		}
	}

	if (Delta.ObjectName.IsSet() || Delta.CustomName.IsSet())
	{
		UpdateNameLabel();
	}
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

	NameLabel->SetText(FText::FromString(GetDisplayName()));
}

void USWGTangibleComponent::RepositionNameLabel()
{
	if (!NameLabel)
	{
		return;
	}

	NameLabel->SetRelativeLocation(FVector(0.0f, 0.0f, ComputeHeightAboveRoot(GetOwner())));
}

FString USWGTangibleComponent::GetDisplayName() const
{
	if (!CustomName.IsEmpty())
	{
		return CustomName;
	}

	const AActor* Owner = GetOwner();
	const UGameInstance* GameInstance = Owner ? Owner->GetGameInstance() : nullptr;
	USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (!Tre)
	{
		return ObjectName.StringTableId;
	}

	if (!ObjectName.File.IsEmpty() && !ObjectName.StringTableId.IsEmpty())
	{
		const FString Resolved = Tre->LookupString(ObjectName.File, ObjectName.StringTableId);
		if (!Resolved.IsEmpty())
		{
			return Resolved;
		}
	}

	if (const ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(Owner))
	{
		const FString FromTemplate = Tre->ResolveTemplateObjectName(NetObject->GetObjectCrc());
		if (!FromTemplate.IsEmpty())
		{
			return FromTemplate;
		}
	}

	return ObjectName.StringTableId;
}
