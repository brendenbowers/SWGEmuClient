#pragma once

#include "CoreMinimal.h"
#include "SWGWindowWidget.h"
#include "SWGAudioOptionsWidget.generated.h"

class UCheckBox;
class USlider;
class USWGAudioSubsystem;
class USWGTreSubsystem;

/**
 * Volume sliders per audio bus, a mute box and a reset button. Built entirely in C++ (no Blueprint):
 * the window chrome the base class binds is created in NativeOnInitialized. Changes apply live and
 * are saved to GameUserSettings.ini when the window goes away.
 */
UCLASS()
class SWGUIWINDOW_API USWGAudioOptionsWidget : public USWGWindowWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void BuildContent();
	void RefreshFromSettings();
	FString Label(const TCHAR* RetailKey, const TCHAR* Fallback) const;

	UFUNCTION() void HandleSliderChanged(float Value);
	UFUNCTION() void HandleSliderCaptureEnd();
	UFUNCTION() void HandleMuteChanged(bool bChecked);
	UFUNCTION() void HandleResetClicked();
	UFUNCTION() void HandleCloseButtonClicked();

	UPROPERTY() TObjectPtr<USWGAudioSubsystem> Audio;
	UPROPERTY() TObjectPtr<USWGTreSubsystem> Tre;
	UPROPERTY() TArray<TObjectPtr<USlider>> Sliders;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> PercentLabels;
	UPROPERTY() TObjectPtr<UCheckBox> MuteCheck;
	UPROPERTY() TArray<TObjectPtr<UObject>> ButtonTextTints;
};
