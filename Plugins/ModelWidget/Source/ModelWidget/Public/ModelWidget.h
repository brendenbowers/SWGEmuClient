#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/SLeafWidget.h"
#include "ModelWidget.generated.h"

struct FModelRenderData;
class FModelSlateElement;
class UMaterialInterface;

/** Hands a built mesh and its per-section materials to a waiting widget. */
using FModelReady = TFunction<void(UObject* Mesh, const TArray<UMaterialInterface*>& Materials)>;

/**
 * Resolves an application-defined id to a mesh for UModelWidget::SetObject.
 * This module loads before any game module exists, so the game installs the
 * provider at startup rather than being linked here.
 */
using FModelProvider = TFunction<void(UWorld* World, int64 ObjectId, FModelReady OnReady)>;

/**
 * Slate side of UModelWidget: paints one custom element per frame that
 * draws the mesh live into the back buffer. Ticks to spin the turntable and
 * to pick up the render data once the mesh's GPU buffers exist.
 */
class MODELWIDGET_API SModelWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SModelWidget)
		: _DesiredSize(56.f, 56.f)
		, _ViewRotation(-25.f, 135.f, 0.f)
		, _Fill(0.85f)
		, _RotateSpeed(0.f)
	{}
		SLATE_ARGUMENT(FVector2D, DesiredSize)
		SLATE_ARGUMENT(FRotator, ViewRotation)
		SLATE_ARGUMENT(float, Fill)
		SLATE_ARGUMENT(float, RotateSpeed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Called each tick until it returns data — the buffers land on the render thread some frames after the mesh is built. */
	void SetModelSource(TFunction<TSharedPtr<FModelRenderData>()> InSource);

	void SetDesiredSize(FVector2D InSize);
	void SetViewRotation(FRotator InRotation);
	void SetFill(float InFill);
	/** Degrees per second about the model's up axis. 0 stops on the current angle. */
	void SetRotateSpeed(float InSpeed);
	void SetYaw(float InYaw);
	float GetYaw() const { return Yaw; }

	bool HasModel() const { return Model.IsValid(); }

protected:
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return DesiredSize; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TFunction<TSharedPtr<FModelRenderData>()> Source;
	TSharedPtr<FModelRenderData> Model;

	FVector2D DesiredSize = FVector2D(56.f, 56.f);
	FRotator ViewRotation = FRotator(-25.f, 135.f, 0.f);
	float Fill = 0.85f;
	float RotateSpeed = 0.f;
	float Yaw = 0.f;
};

/**
 * A live 3D model in the UI: a mesh rendered straight into the widget's
 * rectangle every frame through a custom Slate element, rather than captured
 * to a texture. Give it a mesh directly, or an id and it asks the installed
 * provider for one. Surfaces come from each section's material: a diffuse
 * texture and an optional two-tone tint picked by the diffuse alpha, all read
 * from parameters named by the properties below.
 */
UCLASS()
class MODELWIDGET_API UModelWidget : public UWidget
{
	GENERATED_BODY()

public:
	/** Shows the mesh the installed provider returns for this id. Nothing is drawn until it arrives. */
	UFUNCTION(BlueprintCallable, Category = "Model")
	void SetObject(int64 ObjectId);

	/** Installed once by whichever module knows how to turn an id into a mesh. */
	static void SetModelProvider(FModelProvider InProvider);

	/** A UStaticMesh or USkeletalMesh, with the materials its sections wear (falls back to the mesh's own). */
	UFUNCTION(BlueprintCallable, Category = "Model")
	void SetMesh(UObject* InMesh, const TArray<UMaterialInterface*>& InMaterials);

	UFUNCTION(BlueprintCallable, Category = "Model")
	void SetRotateSpeed(float DegreesPerSecond);

	/** Turntable angle, for a viewer that lets the user drag the model round. */
	UFUNCTION(BlueprintCallable, Category = "Model")
	void SetYaw(float InYaw);

	UFUNCTION(BlueprintPure, Category = "Model")
	float GetYaw() const;

	/** Camera pitch and yaw around the model; pitch is clamped so the camera never goes over the top. */
	UFUNCTION(BlueprintCallable, Category = "Model")
	void SetViewRotation(FRotator InRotation);

	UFUNCTION(BlueprintCallable, Category = "Model")
	void ClearModel();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FVector2D DesiredSize = FVector2D(56.f, 56.f);

	/** Pitch and yaw of the camera around the model. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FRotator ViewRotation = FRotator(-25.f, 135.f, 0.f);

	/** How much of the widget the model's widest extent spans, 1 = edge to edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model", meta = (ClampMin = 0.1, ClampMax = 1.5))
	float Fill = 0.85f;

	/** Turntable speed in degrees per second. 0 holds still. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	float RotateSpeed = 0.f;

	/** Texture parameter on each section's material that holds its diffuse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model|Material")
	FName DiffuseParameter = TEXT("Diffuse");

	/** Vector parameters the diffuse is multiplied by: TintParameter where its alpha is 0, SecondTintParameter where it is 1. Absent parameters read as white. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model|Material")
	FName TintParameter = TEXT("TintColor");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model|Material")
	FName SecondTintParameter = TEXT("TintColor2");

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** A callback for the Slate widget that snapshots BuildRenderData once the buffers exist. */
	TFunction<TSharedPtr<FModelRenderData>()> MakeModelSource() const;

	/** Snapshot of the mesh's GPU buffers, or null while they are still being created. */
	TSharedPtr<FModelRenderData> BuildRenderData() const;

	void HandleModelReady(UObject* InMesh, const TArray<UMaterialInterface*>& InMaterials);

	// Held so the buffers and textures stay alive while drawn.
	UPROPERTY()
	TObjectPtr<UObject> Mesh;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	/** The object a pending SetObject is for, so a late arrival for an earlier one is ignored. */
	int64 PendingObjectId = 0;

	TSharedPtr<SModelWidget> SlateWidget;
};
