#include "Components/SWGTangibleComponent.h"
#include "Network/SWGPacket.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Subsystems/SWGTreSubsystem.h"

USWGTangibleComponent::USWGTangibleComponent()
{
	// Only to keep the name label facing the camera; enabled when one is made.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void USWGTangibleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!NameLabel || !PlayerController || !PlayerController->PlayerCameraManager || !NameLabel->IsVisible())
	{
		return;
	}
	// Text reads from its +X side; turn that toward the camera, yaw only.
	const FVector ToCamera = PlayerController->PlayerCameraManager->GetCameraLocation() - NameLabel->GetComponentLocation();
	NameLabel->SetWorldRotation(FRotator(0.f, ToCamera.Rotation().Yaw, 0.f));
}

void USWGTangibleComponent::SetNameLabelHidden(bool bHidden)
{
	bNameLabelHidden = bHidden;
	if (NameLabel)
	{
		NameLabel->SetVisibility(!bHidden);
	}
}

FString USWGTangibleComponent::StripColorCodes(const FString& Text)
{
	FString Result;
	Result.Reserve(Text.Len());
	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		if (Text[Index] == TEXT('\\') && Index + 1 < Text.Len() && Text[Index + 1] == TEXT('#'))
		{
			// "\#." resets; "\#rrggbb" sets. Anything else after "\#" is kept as typed.
			if (Index + 2 < Text.Len() && Text[Index + 2] == TEXT('.'))
			{
				Index += 2;
				continue;
			}
			int32 HexDigits = 0;
			while (HexDigits < 6 && Index + 2 + HexDigits < Text.Len() && FChar::IsHexDigit(Text[Index + 2 + HexDigits]))
			{
				++HexDigits;
			}
			if (HexDigits == 6)
			{
				Index += 7;
				continue;
			}
		}
		Result.AppendChar(Text[Index]);
	}
	return Result.TrimStartAndEnd();
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
		NameLabel->SetVisibility(!bNameLabelHidden);
		NameLabel->RegisterComponent();
		SetComponentTickEnabled(true);
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
		// Player names carry inline colour codes (a guild tag, say) that nothing here renders.
		return StripColorCodes(CustomName);
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
