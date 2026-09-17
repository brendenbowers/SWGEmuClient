#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWGItemIconSubsystem.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class UPointLightComponent;
class UMeshComponent;
class UMaterialInterface;

/** A built item mesh and the materials its sections wear, kept alive for the widgets drawing it. */
USTRUCT()
struct FSWGCachedItemModel
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UObject> Mesh;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> Materials;
};

/**
 * Renders an item's own 3D model into a small texture for the inventory,
 * the way the retail client drew its icons. A hidden stage high above the
 * world holds one scene capture; each item's mesh is built through the
 * normal item pipeline, posed on the stage, captured once, and discarded.
 *
 * The capture is read back and finished on the CPU so the icon can have a
 * transparent background: the final-colour capture carries no alpha, but the
 * HDR scene colour carries inverse opacity, so the pixels are exposed,
 * tonemapped and alpha-flipped here into a plain UTexture2D.
 *
 * Icons are cached per template, so every copy of an item shares one.
 */
UCLASS()
class SWGEMUCLIENT_API USWGItemIconSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(USWGItemIconSubsystem, STATGROUP_Tickables); }

	/**
	 * The icon for ObjectId's template — fully transparent until its capture
	 * lands, then filled in place, so a widget can hold it from the first
	 * call. Null if the object is unknown.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Icons")
	UTexture2D* GetIcon(int64 ObjectId);

	/**
	 * The item's mesh for drawing live in the UI (USWGModelWidget): a
	 * UStaticMesh or USkeletalMesh plus its per-section materials, built
	 * through the normal item pipeline and cached per template. OnReady runs
	 * on the game thread, at once if already cached, and never if the mesh
	 * can't be built.
	 */
	void RequestItemModel(int64 ObjectId, TFunction<void(UObject* Mesh, const TArray<UMaterialInterface*>& Materials)> OnReady);

	/** Drops every cached icon and model; the next request builds afresh. */
	void ClearCache();

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Icons")
	int32 IconSize = 128;

	/** Illuminance at the item, in lux. Tuned by eye against IconExposure. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Icons")
	float StageLightBrightness = 24.f;

	/** Scale applied to the HDR scene colour before tonemapping — the project's fixed exposure, since the capture skips the tonemapper. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Icons")
	float IconExposure = 0.83f;

private:
	/** Spawns the stage on first use. False if the world can't host it. */
	bool EnsureStage();

	struct FPendingCapture
	{
		TObjectPtr<UMeshComponent> Mesh;
		TObjectPtr<UTexture2D> Icon;
		uint32 TemplateCrc = 0;
	};

	/**
	 * One capture per frame: the single capture component can only frame one
	 * item at a time, and a deferred capture renders with the frame, so the
	 * mesh has to outlive the tick that queued it.
	 */
	TArray<FPendingCapture> Queue;

	UPROPERTY()
	TObjectPtr<UMeshComponent> CapturingMesh;

	UPROPERTY()
	TObjectPtr<UTexture2D> CapturingIcon;

	uint32 CapturingCrc = 0;

	/** Attaches Mesh to the stage and queues it for capture into Icon. */
	void CaptureMesh(UMeshComponent* Mesh, UTexture2D* Icon, uint32 TemplateCrc);

	/** Reads the capture target back, finishes the pixels, and writes them into Icon. */
	void FinishCapture(UTexture2D* Icon, uint32 TemplateCrc);

	/** A transparent IconSize² texture, ready to be filled. */
	UTexture2D* MakeBlankIcon();

	UPROPERTY()
	TMap<uint32, TObjectPtr<UTexture2D>> IconsByTemplateCrc;

	UPROPERTY()
	TMap<uint32, FSWGCachedItemModel> ModelsByTemplateCrc;

	/** Callers waiting on a template whose mesh is still building; drained when it lands. */
	TMap<uint32, TArray<TFunction<void(UObject*, const TArray<UMaterialInterface*>&)>>> PendingModelRequests;

	UPROPERTY()
	TObjectPtr<AActor> Stage;

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> Capture;

	/** Shared HDR target every capture renders into before readback. */
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> CaptureTarget;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> StageLight;

	/** Far above anything the planets place, so nothing but the stage is ever in frame. */
	static constexpr double StageHeight = 400000.0;
};
