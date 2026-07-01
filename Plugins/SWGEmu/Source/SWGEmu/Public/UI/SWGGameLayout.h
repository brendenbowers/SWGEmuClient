#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayTagContainer.h"
#include "SWGGameLayout.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetStack;

/**
 * Root UI widget for WBP_ClientShell.
 *
 * Designer layout (three stacks inside a root Overlay, each Fill/Fill):
 *   MenuLayer    — CommonActivatableWidgetStack  (Login / GalaxySelect / CharacterSelect)
 *   LoadingLayer — CommonActivatableWidgetStack  (ZoneLoading panel)
 *   ModalLayer   — CommonActivatableWidgetStack  (Error panel)
 */
UCLASS(Abstract)
class SWGEMU_API USWGGameLayout : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	static const FGameplayTag TAG_Layer_Menu;
	static const FGameplayTag TAG_Layer_Loading;
	static const FGameplayTag TAG_Layer_Modal;

	/**
	 * Gets the active layout, or creates one from LayoutClass, adds it to the
	 * player's viewport, and registers it. Equivalent to PrimaryGameLayout::GetOrCreateInstance.
	 */
	static USWGGameLayout* GetOrCreate(APlayerController* PlayerController, TSubclassOf<USWGGameLayout> LayoutClass);

	static USWGGameLayout* GetLayout(const UObject* WorldContext);
	static void SetLayout(USWGGameLayout* Layout);

	/** Push a widget onto the menu stack (supports back-navigation). */
	UFUNCTION(BlueprintCallable, Category = "UI")
	UCommonActivatableWidget* PushMenuWidget(TSubclassOf<UCommonActivatableWidget> WidgetClass);

	/** Replace the loading overlay with WidgetClass (pass nullptr to clear). */
	UFUNCTION(BlueprintCallable, Category = "UI")
	UCommonActivatableWidget* SetLoadingWidget(TSubclassOf<UCommonActivatableWidget> WidgetClass);

	/** Replace the modal overlay with WidgetClass (pass nullptr to clear). */
	UFUNCTION(BlueprintCallable, Category = "UI")
	UCommonActivatableWidget* SetModalWidget(TSubclassOf<UCommonActivatableWidget> WidgetClass);

	/** Tag-driven entry point used by the flow subsystem. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	UCommonActivatableWidget* PushWidgetToLayerStack(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass);

	UFUNCTION(BlueprintCallable, Category = "UI")
	void ClearLayer(FGameplayTag LayerTag);

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCommonActivatableWidgetStack> MenuLayer;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCommonActivatableWidgetStack> LoadingLayer;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCommonActivatableWidgetStack> ModalLayer;

private:
	static TWeakObjectPtr<USWGGameLayout> ActiveLayout;
};
