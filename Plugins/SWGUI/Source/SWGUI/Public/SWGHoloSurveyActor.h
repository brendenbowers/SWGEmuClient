#pragma once

#include "CoreMinimal.h"
#include "SWGHoloProjectorActor.h"
#include "SWGHoloSurveyActor.generated.h"

class UTextRenderComponent;

/**
 * The survey tool's hologram at table height: the resources it can find as a
 * column of names projected by the droid, scrolling so the selected one sits in the
 * middle between two bright bars, its class written under it. The droid
 * sweeps while a survey is out and, once a field is up in the world, can
 * throw rays out to it (SetExtraRays).
 */
UCLASS(NotPlaceable)
class SWGUI_API ASWGHoloSurveyActor : public ASWGHoloProjectorActor
{
	GENERATED_BODY()

public:
	ASWGHoloSurveyActor();

	struct FEntry
	{
		FString Name;
		FString ClassName;
	};

	void SetEntries(const TArray<FEntry>& InEntries);
	void SetHeading(const FText& InHeading);
	void SetFieldMode(bool bInFieldMode);
	void CollapseField(const FVector& Center, const TArray<FVector>& EdgeTargets, float Seconds);
	void SetSelectedIndex(int32 Index);
	int32 GetSelectedIndex() const { return SelectedIndex; }

	/** Keeps the droid sweeping, as while a survey is out. */
	void SetScanning(bool bInScanning) { bScanning = bInScanning; }

	/** World position of an entry's label, if it is showing. */
	bool GetEntryLocation(int32 Index, FVector& OutWorld) const;

	/** Middle of the list, world space, for the camera to look at. */
	FVector GetFocusLocation() const;

	/** Rows shown each side of the selected one. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Survey")
	int32 RowsEachSide = 3;

	/** World units between rows. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Survey")
	float RowSpacing = 9.f;

	UPROPERTY(EditAnywhere, Category = "SWGEmu|Survey")
	float LabelSize = 5.5f;

	/** Rows per second the list slides to catch up. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Survey")
	float ScrollSpeed = 12.f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector GetDroidHover() const override;
	virtual float GetFadeRadius() const override { return DiscDiameter * 1.5f; }

private:
	void RebuildLabels();
	FVector RowLocation(float RowOffset) const;

	UPROPERTY()
	TArray<TObjectPtr<UTextRenderComponent>> Labels;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> ClassLabel;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> HeadingLabel;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> EmptyLabel;

	/** Bright bars above and below the selected row. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SelectionBars;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BarMaterial;

	TArray<FEntry> Entries;
	int32 SelectedIndex = 0;
	float CurrentScroll = 0.f;
	bool bScanning = false;
	bool bFieldMode = false;
	FVector CollapseCenter = FVector::ZeroVector;
	TArray<FVector> CollapseEdges;
	float CollapseRemaining = -1.f;
	float CollapseSeconds = 0.f;
};
