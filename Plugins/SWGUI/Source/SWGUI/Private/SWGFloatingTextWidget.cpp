#include "SWGFloatingTextWidget.h"
#include "Subsystems/SWGCombatSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Network/Messages/Zone/Object/CombatSpamIn.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Keeps a lag spike from turning into a screen full of stale numbers. */
	constexpr int32 MaxEntries = 64;

	/** Text sits at least this far above an actor whose mesh hasn't built yet. */
	constexpr float MinHeadOffset = 100.f;

	/** The last part of a line's life is spent fading out. */
	constexpr float FadeFraction = 0.4f;
}

TSharedRef<SWidget> USWGFloatingTextWidget::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("FloatingTextCanvas"));
		WidgetTree->RootWidget = Canvas;
	}
	return Super::RebuildWidget();
}

void USWGFloatingTextWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ScatterRandom.Initialize(FPlatformTime::Cycles());

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGCombatSubsystem* Combat = GameInstance->GetSubsystem<USWGCombatSubsystem>())
		{
			Combat->OnCombatSpam.AddUniqueDynamic(this, &USWGFloatingTextWidget::HandleCombatSpam);
		}
	}
}

void USWGFloatingTextWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGCombatSubsystem* Combat = GameInstance->GetSubsystem<USWGCombatSubsystem>())
		{
			Combat->OnCombatSpam.RemoveDynamic(this, &USWGFloatingTextWidget::HandleCombatSpam);
		}
	}
	Entries.Reset();
	Super::NativeDestruct();
}

void USWGFloatingTextWidget::HandleCombatSpam(const FSWGCombatSpamEvent& Event)
{
	FString Text;
	if (!DescribeSpam(Event, Text))
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	AActor* Defender = ObjectGraph ? ObjectGraph->FindActor(Event.DefenderId) : nullptr;
	if (!Defender)
	{
		return;
	}

	ShowText(Defender, Text, ColorFor(Event), Event.bDealtByUs || Event.bLandedOnUs);
}

bool USWGFloatingTextWidget::DescribeSpam(const FSWGCombatSpamEvent& Event, FString& OutText) const
{
	// Only a swing's outcome floats. The rest of cbt_spam (armor_damaged,
	// wounded, out_of_range, the peace notice...) is log-only, and a miss
	// still carries the damage it would have done, so the name decides.
	if (Event.StringName == TEXT("attack_hit"))
	{
		OutText = FString::FromInt(Event.Damage);
		return true;
	}

	static const TMap<FString, FString> Outcomes = {
		{ TEXT("attack_miss"),     TEXT("miss") },
		{ TEXT("attack_block"),    TEXT("block") },
		{ TEXT("attack_dodge"),    TEXT("dodge") },
		{ TEXT("attack_counter"),  TEXT("counter") },
		{ TEXT("attack_ricochet"), TEXT("ricochet") },
	};
	if (const FString* Word = Outcomes.Find(Event.StringName))
	{
		OutText = *Word;
		return true;
	}
	return false;
}

FLinearColor USWGFloatingTextWidget::ColorFor(const FSWGCombatSpamEvent& Event) const
{
	switch (static_cast<ESWGCombatSpamColor>(Event.Color))
	{
		case ESWGCombatSpamColor::Red:    return FLinearColor::Red;
		case ESWGCombatSpamColor::Yellow: return FLinearColor::Yellow;
		default: break;
	}
	if (Event.bLandedOnUs)
	{
		return TakenColor;
	}
	return Event.bDealtByUs ? DealtColor : BystanderColor;
}

void USWGFloatingTextWidget::ShowText(AActor* Anchor, const FString& Text, FLinearColor Color, bool bEmphasis)
{
	if (!Anchor || !Canvas)
	{
		return;
	}

	if (Entries.Num() >= MaxEntries)
	{
		Entries[0].Text->RemoveFromParent();
		Entries.RemoveAt(0);
	}

	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Label->SetText(FText::FromString(Text));
	Label->SetColorAndOpacity(FSlateColor(Color));
	Label->SetShadowOffset(FVector2D(1.f, 1.f));
	Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));

	FSlateFontInfo Font = Label->GetFont();
	Font.Size = bEmphasis ? EmphasisFontSize : FontSize;
	Font.OutlineSettings.OutlineSize = 1;
	Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
	Label->SetFont(Font);

	UCanvasPanelSlot* LabelSlot = Canvas->AddChildToCanvas(Label);
	LabelSlot->SetAutoSize(true);
	LabelSlot->SetAlignment(FVector2D(0.5f, 1.f));

	FVector Origin, Extent;
	Anchor->GetActorBounds(false, Origin, Extent);

	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Anchor     = Anchor;
	Entry.HeadOffset = FMath::Max(MinHeadOffset, Origin.Z + Extent.Z - Anchor->GetActorLocation().Z);
	Entry.Scatter    = FVector2D(ScatterRandom.FRandRange(-24.f, 24.f), ScatterRandom.FRandRange(-8.f, 8.f));
	Entry.Text       = Label;

	// Lines fired in the same instant on one target stack rather than overprint.
	for (int32 Index = 0; Index < Entries.Num() - 1; ++Index)
	{
		if (Entries[Index].Anchor == Entry.Anchor && Entries[Index].Age < 0.25f)
		{
			Entry.Scatter.Y -= 26.f;
		}
	}
}

void USWGFloatingTextWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	APlayerController* PlayerController = GetOwningPlayer();

	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		FEntry& Entry = Entries[Index];
		Entry.Age += InDeltaTime;

		AActor* Anchor = Entry.Anchor.Get();
		if (Entry.Age >= Lifetime || !Anchor || !PlayerController)
		{
			Entry.Text->RemoveFromParent();
			Entries.RemoveAt(Index);
			continue;
		}

		const float Progress = Entry.Age / Lifetime;

		FVector2D ScreenPosition;
		const FVector WorldPosition = Anchor->GetActorLocation() + FVector(0.f, 0.f, Entry.HeadOffset);
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController, WorldPosition, ScreenPosition, false))
		{
			Entry.Text->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		// Rises quickly then eases off, like it was thrown up.
		const float Rise = RiseDistance * (1.f - FMath::Square(1.f - Progress));
		const float Alpha = Progress < 1.f - FadeFraction ? 1.f : (1.f - Progress) / FadeFraction;

		Entry.Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		Entry.Text->SetRenderOpacity(Alpha);
		if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Entry.Text->Slot))
		{
			LabelSlot->SetPosition(ScreenPosition + Entry.Scatter - FVector2D(0.f, Rise));
		}
	}
}
