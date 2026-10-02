#include "SWGTargetBoxWidget.h"
#include "Subsystems/SWGTargetSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Components/SWGHealthComponent.h"
#include "Components/SWGTangibleComponent.h"
#include "Objects/Creature/SWGCreature.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "SWGHamPoolDisplay.h"


void USWGTargetBoxWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (USWGTargetSubsystem* TargetSubsystem = GetTargetSubsystem())
	{
		TargetSubsystem->OnTargetChanged.AddDynamic(this, &USWGTargetBoxWidget::HandleTargetChanged);

		// A target may already be set when this widget is built (the HUD is
		// pushed after zone-in, and /assist or a relog can land a target
		// first), so adopt the current one rather than waiting for a change.
		HandleTargetChanged(TargetSubsystem->GetTargetId(), TargetSubsystem->GetTargetActor());
	}
	else
	{
		ClearDisplay();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, this, &USWGTargetBoxWidget::RefreshTarget, RefreshInterval, true);
	}
}

void USWGTargetBoxWidget::NativeDestruct()
{
	if (USWGTargetSubsystem* TargetSubsystem = GetTargetSubsystem())
	{
		TargetSubsystem->OnTargetChanged.RemoveDynamic(this, &USWGTargetBoxWidget::HandleTargetChanged);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}

	Super::NativeDestruct();
}

USWGTargetSubsystem* USWGTargetBoxWidget::GetTargetSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<USWGTargetSubsystem>() : nullptr;
}

void USWGTargetBoxWidget::HandleTargetChanged(int64 NewTargetId, AActor* NewTargetActor)
{
	TargetId = NewTargetId;

	if (TargetId == 0)
	{
		ClearDisplay();
	}
	else
	{
		// Pools from the previous target mustn't suppress this one's first write.
		Health = MaxHealth = Action = MaxAction = Mind = MaxMind = INDEX_NONE;
		// HitTestInvisible, not SelfHitTestInvisible: the cursor is live
		// in-world now, and SelfHitTestInvisible only exempts this widget —
		// its child bars and labels would still swallow clicks aimed at the
		// world behind them. Nothing in here is interactive.
		SetVisibility(ESlateVisibility::HitTestInvisible);
		RefreshIdentity();
		RefreshTarget();
	}

	OnTargetChanged(TargetId);
}

void USWGTargetBoxWidget::ClearDisplay()
{
	// Never a real pool value, so the next target's first refresh always writes its labels.
	Health = MaxHealth = Action = MaxAction = Mind = MaxMind = INDEX_NONE;
	TargetLevel = 0;
	TargetName = FText::GetEmpty();

	SetVisibility(ESlateVisibility::Collapsed);
}

void USWGTargetBoxWidget::RefreshIdentity()
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	if (!ObjectGraph || TargetId == 0)
	{
		return;
	}

	if (const USWGTangibleComponent* Tangible = ObjectGraph->FindComponent<USWGTangibleComponent>(TargetId))
	{
		TargetName = FText::FromString(Tangible->GetDisplayName());
	}

	TargetLevel = 0;
	if (const ASWGCreature* Creature = Cast<ASWGCreature>(ObjectGraph->FindActor(TargetId)))
	{
		TargetLevel = Creature->Level;
	}

	if (NameText)  { NameText->SetText(TargetName); }
	if (LevelText)
	{
		// Only creatures have a level; a terminal or building would read "0".
		LevelText->SetText(TargetLevel > 0 ? FText::AsNumber(TargetLevel) : FText::GetEmpty());
		LevelText->SetVisibility(TargetLevel > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void USWGTargetBoxWidget::SetPoolsVisible(bool bVisible)
{
	const ESlateVisibility RowVisibility = bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	for (UProgressBar* Bar : { HealthBar.Get(), ActionBar.Get(), MindBar.Get() })
	{
		// Each bar sits in an overlay with its label; hide the pair.
		if (UPanelWidget* Row = Bar ? Bar->GetParent() : nullptr)
		{
			Row->SetVisibility(RowVisibility);
		}
	}
}

float USWGTargetBoxWidget::GetPoolFraction(ESWGHamPool Pool) const
{
	int32 Current = 0;
	int32 Max = 0;

	switch (Pool)
	{
		case ESWGHamPool::Health: Current = Health; Max = MaxHealth; break;
		case ESWGHamPool::Action: Current = Action; Max = MaxAction; break;
		case ESWGHamPool::Mind:   Current = Mind;   Max = MaxMind;   break;
		default: break;
	}

	return Max > 0 ? FMath::Clamp(static_cast<float>(Current) / static_cast<float>(Max), 0.f, 1.f) : 0.f;
}

FLinearColor USWGTargetBoxWidget::GetDifficultyColor() const
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	const ASWGCreature* Player = ObjectGraph
		? Cast<ASWGCreature>(ObjectGraph->FindActor(ObjectGraph->GetLocalPlayerObjectId()))
		: nullptr;

	if (!Player || Player->Level == 0 || TargetLevel == 0)
	{
		return FLinearColor::White;
	}

	// SWG's con bands, by level difference against the player.
	const int32 Delta = TargetLevel - static_cast<int32>(Player->Level);

	if (Delta <= -6) { return FLinearColor(0.35f, 0.35f, 0.35f); } // trivial — grey
	if (Delta <= -3) { return FLinearColor(0.20f, 0.80f, 0.20f); } // easy — green
	if (Delta <=  2) { return FLinearColor(0.90f, 0.90f, 0.20f); } // even — yellow
	if (Delta <=  5) { return FLinearColor(0.95f, 0.55f, 0.10f); } // hard — orange
	return FLinearColor(0.90f, 0.15f, 0.15f);                      // deadly — red
}

void USWGTargetBoxWidget::RefreshTarget()
{
	if (TargetId == 0)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	const USWGHealthComponent* HealthComponent = ObjectGraph ? ObjectGraph->FindComponent<USWGHealthComponent>(TargetId) : nullptr;

	if (!HealthComponent)
	{
		// The id is live but its actor hasn't spawned (or isn't a creature —
		// a targeted item has no HAM). Keep the box up with just its name; the
		// next poll picks the pools up if they arrive.
		SetPoolsVisible(false);
		return;
	}
	SetPoolsVisible(true);

	const int32 NewHealth = SWGHamPoolDisplay::PoolValue(HealthComponent->HAM, ESWGHamPool::Health);
	const int32 NewMaxHealth = SWGHamPoolDisplay::PoolValue(HealthComponent->MaxHAM, ESWGHamPool::Health);
	const int32 NewAction = SWGHamPoolDisplay::PoolValue(HealthComponent->HAM, ESWGHamPool::Action);
	const int32 NewMaxAction = SWGHamPoolDisplay::PoolValue(HealthComponent->MaxHAM, ESWGHamPool::Action);
	const int32 NewMind = SWGHamPoolDisplay::PoolValue(HealthComponent->HAM, ESWGHamPool::Mind);
	const int32 NewMaxMind = SWGHamPoolDisplay::PoolValue(HealthComponent->MaxHAM, ESWGHamPool::Mind);

	const bool bChanged =
		NewHealth != Health || NewMaxHealth != MaxHealth ||
		NewAction != Action || NewMaxAction != MaxAction ||
		NewMind != Mind || NewMaxMind != MaxMind;

	if (!bChanged)
	{
		return;
	}

	Health = NewHealth;
	MaxHealth = NewMaxHealth;
	Action = NewAction;
	MaxAction = NewMaxAction;
	Mind = NewMind;
	MaxMind = NewMaxMind;

	if (HealthBar) { HealthBar->SetPercent(GetPoolFraction(ESWGHamPool::Health)); }
	if (ActionBar) { ActionBar->SetPercent(GetPoolFraction(ESWGHamPool::Action)); }
	if (MindBar)   { MindBar->SetPercent(GetPoolFraction(ESWGHamPool::Mind)); }

	if (HealthText) { HealthText->SetText(SWGHamPoolDisplay::FormatPool(Health, MaxHealth)); }
	if (ActionText) { ActionText->SetText(SWGHamPoolDisplay::FormatPool(Action, MaxAction)); }
	if (MindText)   { MindText->SetText(SWGHamPoolDisplay::FormatPool(Mind, MaxMind)); }

	OnTargetUpdated();
}
