#pragma once

#include "CoreMinimal.h"
#include "SWGWindowWidget.h"
#include "SWGWaypointListWidget.generated.h"

class UPanelWidget;

/**
 * The datapad's waypoint list — every waypoint the player currently has,
 * sorted nearest first, each row showing its name, colour, active state,
 * compass direction and distance. A floating USWGWindowWidget like Inventory/
 * Examine/the mission browser. Read-only: there is no client-side waypoint
 * activate/deactivate command yet (see USWGWaypointSubsystem), so a row is
 * informational only, not clickable.
 */
UCLASS(Abstract)
class SWGUI_API USWGWaypointListWidget : public USWGWindowWidget
{
	GENERATED_BODY()

public:
	/** Rebuilds the row list from USWGWaypointSubsystem right now. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Waypoint")
	void RefreshList();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Holds the waypoint rows, built in code — see USWGMissionBrowserWidget::BuildList for the same idiom. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> WaypointListPanel;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Waypoint")
	FSlateFontInfo RowFont;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Waypoint")
	FLinearColor InactiveTextColor = FLinearColor(0.6f, 0.6f, 0.6f);

	/** How often the open window re-reads live distance/direction from the subsystem. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Waypoint")
	float RefreshInterval = 1.0f;

private:
	UFUNCTION()
	void HandleWaypointListChanged();

	FTimerHandle RefreshTimer;
};
