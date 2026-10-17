#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Playback/SWGSoundHandle.h"
#include "TRE/SWGLightsaberReader.h"
#include "SWGLightsaberComponent.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class UPointLightComponent;

/** One blade's axis in the hilt mesh's own space (UE axes, cm): where it starts and which way it points. */
struct FSWGSaberBladeAxis
{
	FVector Base = FVector::ZeroVector;
	FVector Direction = FVector::UpVector;
};

/** What the weapon template adds to its .lsb: the blade colour and the client effects of igniting and retracting. */
struct FSWGSaberStyle
{
	FLinearColor Tint = FLinearColor::White;
	FString BladeOnEffect;
	FString BladeOffEffect;
};

/**
 * A lightsaber's blades on its hilt (the .lsb: BLAD length/width/ignite/retract seconds, an optional LGHT glow
 * light and the ASND hum). Rides the hilt mesh component; the blade extends over OPEN seconds on creation and
 * Retract() closes it over CLOS seconds, then removes the component.
 */
UCLASS(ClassGroup = "SWGEmu")
class SWGEMUCLIENT_API USWGLightsaberComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USWGLightsaberComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** Reads the .lsb and builds one blade per BLAD along Axes (a missing axis reuses the first). False when it cannot be read. */
	bool Setup(const FString& LsbPath, const TArray<FSWGSaberBladeAxis>& Axes, const FSWGSaberStyle& Style);

	/** Starts closing the blades; the component destroys itself once they are in. */
	void Retract();

	/** Seconds a retract takes (the longest CLOS), for deferring the hilt's own removal. */
	float GetCloseSeconds() const;

	/** 0..1 extension of the blade: Elapsed / Duration while opening, 1 - that while closing; instant when Duration is 0. */
	static float ComputeBladeFraction(float ElapsedSeconds, float DurationSeconds, bool bClosing);

	/** Next flicker intensity: a random multiplier between MinScale and 1, re-rolled every TimeMin..TimeMax seconds by the caller. */
	static float ComputeFlickerScale(float RandomUnit, float MinScale);

	float GetCurrentFraction() const { return Fraction; }
	int32 GetBladeCount() const { return Blades.Num(); }

private:
	void ApplyFraction();
	void StartHum();
	void StopHum();
	void PlayStyleEffect(const FString& Effect);

	FSWGLightsaber Saber;
	FSWGSaberStyle Style;
	TArray<FSWGSaberBladeAxis> BladeAxes;

	UPROPERTY()
	TArray<TObjectPtr<UDynamicMeshComponent>> Blades;

	/** Per blade-shader frame (a shader switch cycles them), already tinted. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> FrameMaterials;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> GlowLight;

	FSWGSoundHandle Hum;
	float FrameSeconds = 0.0625f;
	float Elapsed = 0.f;
	float Fraction = 0.f;
	float FlickerScale = 1.f;
	float NextFlickerSeconds = 0.f;
	int32 ShownFrame = INDEX_NONE;
	bool bClosing = false;
};
