#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TRE/SWGShaderReader.h"
#include "SWGAnimatedMaterialSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;

/** Per-animation frame state; only RandomTime keeps anything between ticks. */
struct FSWGShaderFrameState
{
	int32 Frame = 0;
	double NextChangeTime = 0.0;
};

/** One flipbook MID: its frame textures and the frame currently applied. */
USTRUCT()
struct FSWGFlipbookEntry
{
	GENERATED_BODY()

	TWeakObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> Frames;
	FSWGShaderAnimation Animation;
	FSWGShaderFrameState State;
	int32 AppliedFrame = INDEX_NONE;
	FString Name;
};

/** One scrolling MID: TSNS rates in UV units per second for the two UV layers. */
USTRUCT()
struct FSWGScrollEntry
{
	GENERATED_BODY()

	TWeakObjectPtr<UMaterialInstanceDynamic> Material;
	FVector2D RateLayer1 = FVector2D::ZeroVector;
	FVector2D RateLayer2 = FVector2D::ZeroVector;
	FString Name;
};

/**
 * Animates the shared object MIDs from GetOrBuildObjectMaterial:
 *  - SWTS flipbooks and SWSH switches swap the Diffuse texture (SWSH frames are reduced to their diffuse textures);
 *  - TSNS scrolls drive ScrollUV1/ScrollUV2 on the object masters.
 * MIDs are cached per shader, so one update reaches every mesh using the shader.
 */
UCLASS()
class SWGEMUCLIENT_API USWGAnimatedMaterialSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !Flipbooks.IsEmpty() || !Scrolls.IsEmpty(); }

	/** Frames holds one texture per animation frame (SWSH included). */
	void RegisterFlipbook(UMaterialInstanceDynamic* Material, const FSWGShaderAnimation& Animation, const TArray<UTexture2D*>& Frames, const FString& Name);
	/** Scroll rates come from the shader's TSNS record for the MAIN texture. */
	void RegisterScroll(UMaterialInstanceDynamic* Material, const FSWGShaderScroll& Scroll, const FString& Name);

	/**
	 * Frame to show at TimeSeconds. FrameCounter is the rendered-frame index (DFST); Random supplies RandomFrame/RandomTime picks.
	 * RandomTime reads the two floats as a hold-time range in seconds (inferred, not confirmed against the retail client).
	 */
	static int32 SelectFrame(const FSWGShaderAnimation& Animation, double TimeSeconds, uint64 FrameCounter, FRandomStream& Random, FSWGShaderFrameState& State);

	/** UV offset for a scroll rate at TimeSeconds, wrapped to [0,1). */
	static FVector2D ComputeScrollOffset(const FVector2D& Rate, double TimeSeconds);

	/** Human-readable state of every registered material, for swg.Fx.ShaderAnim. */
	TArray<FString> DescribeEntries() const;

private:
	UPROPERTY()
	TArray<FSWGFlipbookEntry> Flipbooks;

	UPROPERTY()
	TArray<FSWGScrollEntry> Scrolls;

	double ClockSeconds = 0.0;
	double SecondsSinceUpdate = 0.0;
	uint64 TickCounter = 0;
	FRandomStream Random;
};
