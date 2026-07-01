#include "UI/SWGGameLayout.h"
#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"

TWeakObjectPtr<USWGGameLayout> USWGGameLayout::ActiveLayout;

const FGameplayTag USWGGameLayout::TAG_Layer_Menu    = FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Menu"));
const FGameplayTag USWGGameLayout::TAG_Layer_Loading = FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Loading"));
const FGameplayTag USWGGameLayout::TAG_Layer_Modal   = FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Modal"));

USWGGameLayout* USWGGameLayout::GetOrCreate(APlayerController* PlayerController, TSubclassOf<USWGGameLayout> LayoutClass)
{
	if (ActiveLayout.IsValid())
		return ActiveLayout.Get();

	if (!PlayerController || !LayoutClass)
		return nullptr;

	USWGGameLayout* Layout = CreateWidget<USWGGameLayout>(PlayerController, LayoutClass);
	if (!Layout)
		return nullptr;

	Layout->AddToPlayerScreen(100);
	SetLayout(Layout);
	return Layout;
}

USWGGameLayout* USWGGameLayout::GetLayout(const UObject* WorldContext)
{
	return ActiveLayout.IsValid() ? ActiveLayout.Get() : nullptr;
}

void USWGGameLayout::SetLayout(USWGGameLayout* Layout)
{
	ActiveLayout = Layout;
}

UCommonActivatableWidget* USWGGameLayout::PushMenuWidget(TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	if (!WidgetClass || !MenuLayer)
		return nullptr;

	if (UCommonActivatableWidget* CurrentWidget = MenuLayer->GetRootContent(); CurrentWidget && CurrentWidget->GetClass() == WidgetClass)
	{
		// already active
		return CurrentWidget;
	}

	return MenuLayer->AddWidget(WidgetClass);
}

UCommonActivatableWidget* USWGGameLayout::SetLoadingWidget(TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	if (!LoadingLayer)
		return nullptr;

	LoadingLayer->ClearWidgets();

	if (!WidgetClass)
		return nullptr;

	return LoadingLayer->AddWidget(WidgetClass);
}

UCommonActivatableWidget* USWGGameLayout::SetModalWidget(TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	if (!ModalLayer)
		return nullptr;

	ModalLayer->ClearWidgets();

	if (!WidgetClass)
		return nullptr;

	return ModalLayer->AddWidget(WidgetClass);
}

UCommonActivatableWidget* USWGGameLayout::PushWidgetToLayerStack(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	if (LayerTag == TAG_Layer_Menu)
	{
		return PushMenuWidget(WidgetClass);
	}
	if (LayerTag == TAG_Layer_Loading)
	{
		return SetLoadingWidget(WidgetClass);
	}
	if (LayerTag == TAG_Layer_Modal)
	{
		return SetModalWidget(WidgetClass);
	}

	UE_LOG(LogTemp, Warning, TEXT("USWGGameLayout: Unknown layer tag %s"), *LayerTag.ToString());
	return nullptr;
}

void USWGGameLayout::ClearLayer(FGameplayTag LayerTag)
{
	if (LayerTag == TAG_Layer_Menu && MenuLayer)
	{
		MenuLayer->ClearWidgets();
	}
	if (LayerTag == TAG_Layer_Loading && LoadingLayer)
	{
		LoadingLayer->ClearWidgets();
	}
	if (LayerTag == TAG_Layer_Modal && ModalLayer)
	{
		ModalLayer->ClearWidgets();
	}
}
