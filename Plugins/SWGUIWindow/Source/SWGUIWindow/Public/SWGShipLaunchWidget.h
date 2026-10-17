#pragma once
#include "SWGWindowWidget.h"
#include "Components/ComboBoxString.h"
#include "Subsystems/SWGShipLaunchSubsystem.h"
#include "SWGShipLaunchWidget.generated.h"

UCLASS()
class SWGUIWINDOW_API USWGShipLaunchWidget : public USWGWindowWidget
{
	GENERATED_BODY()
protected:
	void NativeOnInitialized() override;
	void NativeConstruct() override;
	void NativeDestruct() override;
private:
	UFUNCTION() void Refresh();
	UFUNCTION() void Selected(FString Value, ESelectInfo::Type Type);
	UFUNCTION() void LaunchClicked();
	UPROPERTY() TObjectPtr<UComboBoxString> Selector;
	UPROPERTY() TObjectPtr<UButton> LaunchButton;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<USWGShipLaunchSubsystem> LaunchSystem;
	TArray<FSWGLaunchShip> Ships;
	bool bRefreshing = false;
};
