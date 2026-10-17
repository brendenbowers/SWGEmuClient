#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Common/SWGPropWind.h"
#include "TRE/SWGClientDataFileReader.h"
#include "SWGPropAttachmentsSubsystem.generated.h"

class USWGMeshGeneratorSubsystem;
class USWGPropAttachmentsComponent;
class USWGTerrainSubsystem;
class UMeshComponent;

struct FSWGPropAttachmentStats
{
	int32 Props = 0;
	int32 FlagsRunning = 0;
	int32 LightsLit = 0;
	int32 LightCandidates = 0;
	int32 ParticlesLive = 0;
	int32 FlagProps = 0;
	int32 FxProps = 0;
};

/**
 * Gives every prop whose template .cdf holds flags, particle or light objects, or children a
 * USWGPropAttachmentsComponent (once its mesh lands) and decides, a few times a second, what runs:
 * flags within swg.Fx.PropFlagRadius, particles and lights within swg.Fx.PropFxRadius (both with hysteresis),
 * and only the nearest swg.Fx.MaxPropLights lights. Lamps follow USWGTerrainSubsystem::IsDaytime.
 * Wind is a single 0..1 strength; SetWindStrength is the hook Stage 7's weather subsystem drives.
 */
UCLASS()
class SWGEMUCLIENT_API USWGPropAttachmentsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Wind strength 0..1 feeding every flag. swg.Fx.Wind sets it until the weather subsystem takes over. */
	void SetWindStrength(float InWindStrength) { WindStrength = FMath::Clamp(InWindStrength, 0.f, 1.f); }
	float GetWindStrength() const { return WindStrength; }

	/** The template's parsed cdf when it holds anything this subsystem plays; null otherwise. Cached per template. */
	TSharedPtr<const FSWGClientDataFile> FindAttachments(const FString& TemplatePath);

	/** Adds the component to Actor if its template has attachments; no-op otherwise or when it already has one. */
	void AttachToActor(AActor& Actor);

	void RegisterProp(USWGPropAttachmentsComponent* Prop);
	void UnregisterProp(USWGPropAttachmentsComponent* Prop);

	float GetLightIntensityScale() const;
	FSWGPropAttachmentStats GetStats() const;

	/** Re-evaluates every prop now instead of waiting for the next update. */
	void RefreshNow();

private:
	void HandleMeshReady(AActor* Actor, UMeshComponent* MeshComponent);
	void Refresh();

	UPROPERTY(Transient)
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGenerator;

	UPROPERTY(Transient)
	TObjectPtr<USWGTerrainSubsystem> Terrain;

	TArray<TWeakObjectPtr<USWGPropAttachmentsComponent>> Props;
	TArray<TWeakObjectPtr<USWGPropAttachmentsComponent>> LitProps;
	TMap<FString, TSharedPtr<const FSWGClientDataFile>> AttachmentsByTemplate;

	FDelegateHandle MeshReadyHandle;
	FSWGPropAttachmentStats LastStats;
	float WindStrength = SWGPropWind::DefaultWindStrength;
	float SecondsSinceRefresh = 0.f;
	bool bNight = false;
};
