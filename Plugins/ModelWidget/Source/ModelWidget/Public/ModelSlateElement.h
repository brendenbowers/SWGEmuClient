#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"
#include "Rendering/RenderingCommon.h"

class FTexture;

/** One draw call's worth of a mesh: an index range plus the surface it wears. */
struct MODELWIDGET_API FModelSection
{
	uint32 FirstIndex = 0;
	uint32 NumTriangles = 0;

	/** Render resource of the diffuse texture. Its owner must outlive every element that draws it. */
	const FTexture* Diffuse = nullptr;

	FLinearColor TintColor = FLinearColor::White;
	FLinearColor TintColor2 = FLinearColor::White;

	/** Alpha below one half is cut out, as the masked object material does. */
	bool bMasked = false;
};

/**
 * Everything the render thread needs to draw a mesh, holding the GPU buffers
 * by reference so they outlive the UObject that built them. Built once per
 * mesh by the widget module and shared by every element that draws it.
 */
struct MODELWIDGET_API FModelRenderData
{
	FBufferRHIRef IndexBuffer;
	FBufferRHIRef PositionBuffer;
	FBufferRHIRef TangentBuffer;
	FBufferRHIRef TexCoordBuffer;

	uint32 NumVertices = 0;
	uint32 NumTexCoords = 1;
	bool bHighPrecisionTangents = false;
	bool bFullPrecisionUVs = false;

	TArray<FModelSection> Sections;

	FVector3f BoundsOrigin = FVector3f::ZeroVector;
	float BoundsRadius = 1.f;

	/** Half-size of the bounding box; the widest axis sets how the model is fitted to the rect. */
	FVector3f BoundsExtent = FVector3f::OneVector;

	/** Views over the buffers, made on the render thread the first time they are drawn. */
	FShaderResourceViewRHIRef PositionView;
	FShaderResourceViewRHIRef TangentView;
	FShaderResourceViewRHIRef TexCoordView;

	bool IsValid() const { return IndexBuffer && PositionBuffer && TangentBuffer && TexCoordBuffer && !Sections.IsEmpty(); }
};

/**
 * Draws one FModelRenderData into the Slate back buffer where a widget
 * sits, rendered with its own depth buffer and a fixed key light, so the
 * model is a live mesh in the UI rather than a captured picture. Made fresh
 * for every paint; Slate keeps it until the render thread has drawn it.
 */
class MODELWIDGET_API FModelSlateElement : public ICustomSlateElement
{
public:
	/** Window-space pixel rect the model fills, and the rect it is clipped to. */
	FIntRect ViewRect;
	FIntRect ClipRect;

	TSharedPtr<FModelRenderData> Model;

	/** Turns the model about its bounds centre before the fixed view is applied. */
	FRotator ModelRotation = FRotator::ZeroRotator;

	/** The view's own pitch and yaw around the model, like a turntable camera. */
	FRotator ViewRotation = FRotator(-25.f, 135.f, 0.f);

	/** How much of the rect the model's widest extent spans, 1 = edge to edge. */
	float Fill = 0.85f;

	virtual void Draw_RenderThread(FRDGBuilder& GraphBuilder, const FDrawPassInputs& Inputs) override;

	/**
	 * Call from the game thread for every element handed to Slate. The batcher
	 * keeps only a raw pointer to a custom drawer, which the render thread uses
	 * a frame or two later, so the element must outlive the widget that painted
	 * it — a widget torn down mid-frame would otherwise leave a dangling draw.
	 * A fixed ring of recent elements covers that lag for every widget at once.
	 */
	static void Retain(const TSharedPtr<FModelSlateElement>& Element);
};

/** ui.ModelWidget.Debug — logs each stage of the widget's draw path. */
MODELWIDGET_API bool ModelWidgetDebugEnabled();
