#include "UI/SWGGameLayout.h"
#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "NativeGameplayTags.h"

TWeakObjectPtr<USWGGameLayout> USWGGameLayout::ActiveLayout;

// FGameplayTag::RequestGameplayTag() at file-scope static-init time races
// UGameplayTagsManager's own initialization — depending on module load order
// it can resolve to an empty tag, silently breaking every "LayerTag == TAG_*"
// comparison below (this is what produced "Unknown layer tag UI.Layer.Loading").
// FNativeGameplayTag sidesteps the race: its constructor builds InternalTag
// directly from the FName (no manager lookup needed) and separately registers
// with the manager whenever it becomes available, so GetTag() below is always
// valid regardless of init order.
namespace SWGGameLayoutTags
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Layer_Menu, "UI.Layer.Menu");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Layer_Loading, "UI.Layer.Loading");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Layer_Modal, "UI.Layer.Modal");
}

const FGameplayTag USWGGameLayout::TAG_Layer_Menu    = SWGGameLayoutTags::TAG_Layer_Menu.GetTag();
const FGameplayTag USWGGameLayout::TAG_Layer_Loading = SWGGameLayoutTags::TAG_Layer_Loading.GetTag();
const FGameplayTag USWGGameLayout::TAG_Layer_Modal   = SWGGameLayoutTags::TAG_Layer_Modal.GetTag();

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
