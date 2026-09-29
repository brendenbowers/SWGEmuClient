#include "SWGHoloSurveyActor.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** ui_res_survey.inc text1 and header colours, as the hologram's selected and class lines. */
	const FColor SelectedColor(0xE6, 0xFF, 0xFF);
	const FColor RowColor(0x8B, 0xDC, 0xF5);
	const FColor ClassColor(0x97, 0xFF, 0xFF);
	/** The list's middle row in the air, world units. */
	constexpr float ListMiddleHeight = 55.f;
	constexpr float BarLength = 70.f;
}

ASWGHoloSurveyActor::ASWGHoloSurveyActor()
{
	DiscDiameter = 150.f;
	ProjectionLift = 6.f;
}

void ASWGHoloSurveyActor::BeginPlay()
{
	Super::BeginPlay();
	BarMaterial = MakeHoloMaterial(HoloColor, HoloIntensity * 3.f);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UStaticMeshComponent* Bar = NewObject<UStaticMeshComponent>(this);
		Bar->SetStaticMesh(BeamMesh);
		Bar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Bar->SetCastShadow(false);
		Bar->SetMaterial(0, BarMaterial);
		Bar->SetupAttachment(GetRootComponent());
		Bar->RegisterComponent();
		SelectionBars.Add(Bar);
	}
	auto MakeText = [this](float Size, const FColor& Color)
	{
		UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this);
		Text->SetWorldSize(Size);
		Text->SetTextRenderColor(Color);
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		if (UMaterialInterface* TextMaterial = GetHoloTextMaterial())
		{
			Text->SetTextMaterial(TextMaterial);
		}
		Text->SetupAttachment(GetRootComponent());
		Text->RegisterComponent();
		return Text;
	};
	ClassLabel = MakeText(LabelSize * 0.6f, ClassColor);
	HeadingLabel = MakeText(LabelSize * 0.85f, ClassColor);
	EmptyLabel = MakeText(LabelSize * 0.8f, ClassColor);
	EmptyLabel->SetText(NSLOCTEXT("SWGEmu", "HoloSurveyEmpty", "No resources of this type here"));
	RebuildLabels();
	SetProjectionSurfaceVisible(false);
}

void ASWGHoloSurveyActor::SetHeading(const FText& InHeading)
{
	if (HeadingLabel)
	{
		HeadingLabel->SetText(InHeading);
	}
}

void ASWGHoloSurveyActor::SetFieldMode(bool bInFieldMode)
{
	bFieldMode = bInFieldMode;
	RayWidth = bFieldMode ? 3.f : 0.7f;
	DroidRoot->SetRelativeScale3D(bFieldMode ? FVector(2.f) : FVector::OneVector);
	SetProjectionSurfaceVisible(false);
	GlowLight->SetVisibility(!bFieldMode);
}

FVector ASWGHoloSurveyActor::GetDroidHover() const
{
	return bFieldMode ? FVector(0.f, 0.f, 80.f) : Super::GetDroidHover();
}

void ASWGHoloSurveyActor::CollapseField(const FVector& Center, const TArray<FVector>& EdgeTargets, float Seconds)
{
	CollapseCenter = Center;
	CollapseEdges = EdgeTargets;
	CollapseSeconds = FMath::Max(Seconds, 0.01f);
	CollapseRemaining = CollapseSeconds;
	SetLifeSpan(CollapseSeconds + 0.1f);
}

void ASWGHoloSurveyActor::SetEntries(const TArray<FEntry>& InEntries)
{
	Entries = InEntries;
	SelectedIndex = FMath::Clamp(SelectedIndex, 0, FMath::Max(0, Entries.Num() - 1));
	CurrentScroll = SelectedIndex;
	NoteProjectionChange();
	if (HasActorBegunPlay())
	{
		RebuildLabels();
	}
}

void ASWGHoloSurveyActor::SetSelectedIndex(int32 Index)
{
	const int32 Clamped = FMath::Clamp(Index, 0, FMath::Max(0, Entries.Num() - 1));
	if (Clamped != SelectedIndex)
	{
		SelectedIndex = Clamped;
		NoteProjectionChange();
	}
}

void ASWGHoloSurveyActor::RebuildLabels()
{
	for (UTextRenderComponent* Label : Labels)
	{
		Label->DestroyComponent();
	}
	Labels.Reset();
	for (const FEntry& Entry : Entries)
	{
		UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
		Label->SetText(FText::FromString(Entry.Name));
		Label->SetWorldSize(LabelSize);
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextCenter);
		if (UMaterialInterface* TextMaterial = GetHoloTextMaterial())
		{
			Label->SetTextMaterial(TextMaterial);
		}
		Label->SetupAttachment(GetRootComponent());
		Label->RegisterComponent();
		Labels.Add(Label);
	}
}

FVector ASWGHoloSurveyActor::RowLocation(float RowOffset) const
{
	return FVector(0.f, 0.f, ProjectionLift + ListMiddleHeight - RowOffset * RowSpacing);
}

FVector ASWGHoloSurveyActor::GetFocusLocation() const
{
	return GetActorTransform().TransformPosition(RowLocation(0.f) - FVector(0.f, 0.f, RowSpacing));
}

bool ASWGHoloSurveyActor::GetEntryLocation(int32 Index, FVector& OutWorld) const
{
	if (!Labels.IsValidIndex(Index) || !Labels[Index]->IsVisible())
	{
		return false;
	}
	OutWorld = Labels[Index]->GetComponentLocation();
	return true;
}

void ASWGHoloSurveyActor::Tick(float DeltaSeconds)
{
	if (CollapseRemaining >= 0.f)
	{
		CollapseRemaining = FMath::Max(0.f, CollapseRemaining - DeltaSeconds);
		const float Fraction = CollapseRemaining / CollapseSeconds;
		TArray<FVector> Targets;
		TArray<float> Brightness;
		for (const FVector& Edge : CollapseEdges)
		{
			Targets.Add(FMath::Lerp(CollapseCenter, Edge, Fraction));
			Brightness.Add(0.06f * Fraction);
		}
		SetExtraRays(Targets, Brightness);
	}
	if (bScanning)
	{
		NoteProjectionChange();
	}
	Super::Tick(DeltaSeconds);
	CurrentScroll = FMath::FInterpConstantTo(CurrentScroll, SelectedIndex, DeltaSeconds, ScrollSpeed * FMath::Max(1.f, FMath::Abs(SelectedIndex - CurrentScroll)));

	FVector CameraLocation = GetActorLocation();
	FRotator CameraRotation;
	if (const APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	}
	const FRotator FaceCamera = (CameraLocation - GetActorLocation()).GetSafeNormal2D().Rotation();
	HeadingLabel->SetRelativeLocation(RowLocation(-RowsEachSide - 1.f));
	HeadingLabel->SetWorldRotation(FaceCamera);
	HeadingLabel->SetVisibility(!bFieldMode);
	const float Pulse = bScanning ? 0.5f + 0.5f * FMath::Sin(DroidTime * 8.f) : 1.f;

	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		UTextRenderComponent* Label = Labels[Index];
		const float Offset = Index - CurrentScroll;
		const float Distance = FMath::Abs(Offset);
		const bool bShown = !bFieldMode && Distance <= RowsEachSide + 0.5f && FadeAlpha > 0.05f;
		Label->SetVisibility(bShown);
		if (!bShown)
		{
			continue;
		}
		const bool bSelected = Index == SelectedIndex;
		// Rows further from the middle shrink and dim, like a drum turning away.
		const float Nearness = 1.f - Distance / (RowsEachSide + 1.f);
		Label->SetRelativeLocation(RowLocation(Offset) + FaceCamera.Vector() * (1.f - Nearness) * -6.f);
		Label->SetWorldRotation(FaceCamera);
		Label->SetWorldSize(LabelSize * (bSelected ? 1.25f : FMath::Lerp(0.6f, 0.95f, Nearness)));
		const FLinearColor Base = FLinearColor(bSelected ? SelectedColor : RowColor);
		Label->SetTextRenderColor((Base * (bSelected ? Pulse : Nearness * 0.9f + 0.1f)).ToFColor(true));
	}

	const bool bHasEntries = !Entries.IsEmpty();
	EmptyLabel->SetVisibility(!bFieldMode && !bHasEntries);
	EmptyLabel->SetRelativeLocation(RowLocation(0.f));
	EmptyLabel->SetWorldRotation(FaceCamera);
	ClassLabel->SetVisibility(!bFieldMode && bHasEntries);
	if (bHasEntries)
	{
		ClassLabel->SetText(FText::FromString(Entries[SelectedIndex].ClassName));
		ClassLabel->SetRelativeLocation(RowLocation(CurrentScroll - SelectedIndex) + FVector(0.f, 0.f, -LabelSize * 0.95f));
		ClassLabel->SetWorldRotation(FaceCamera);
	}
	for (int32 Index = 0; Index < SelectionBars.Num(); ++Index)
	{
		UStaticMeshComponent* Bar = SelectionBars[Index];
		Bar->SetVisibility(!bFieldMode && bHasEntries);
		// Engine cylinders stand along Z; lay them across, facing the camera.
		const float Height = Index == 0 ? RowSpacing * 0.62f : -RowSpacing * 1.05f;
		Bar->SetRelativeLocation(RowLocation(0.f) + FVector(0.f, 0.f, Height));
		Bar->SetWorldRotation(FRotator(0.f, FaceCamera.Yaw, 90.f));
		Bar->SetRelativeScale3D(FVector(0.006f, 0.006f, BarLength / 100.f));
	}
	if (BarMaterial)
	{
		BarMaterial->SetScalarParameterValue(TEXT("Intensity"), HoloIntensity * 3.f * Pulse);
	}
}
