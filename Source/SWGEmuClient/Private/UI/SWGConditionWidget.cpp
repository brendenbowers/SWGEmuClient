#include "UI/SWGConditionWidget.h"
#include "Components/SWGHealthComponent.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "UI/SWGHamPoolDisplay.h"


void USWGConditionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshCondition();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, this, &USWGConditionWidget::RefreshCondition, RefreshInterval, true);
	}
}

void USWGConditionWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}

	Super::NativeDestruct();
}

USWGHealthComponent* USWGConditionWidget::ResolveHealthComponent() const
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	if (!ObjectGraph)
	{
		return nullptr;
	}

	return ObjectGraph->FindComponent<USWGHealthComponent>(ObjectGraph->GetLocalPlayerObjectId());
}

float USWGConditionWidget::GetPoolFraction(ESWGHamPool Pool) const
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

void USWGConditionWidget::RefreshCondition()
{
	const USWGHealthComponent* HealthComponent = ResolveHealthComponent();
	if (!HealthComponent)
	{
		return;
	}

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

	OnConditionUpdated();
}
