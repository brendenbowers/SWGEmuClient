#include "SWGMapGroundOverlayWidget.h"
#include "SWGPlanetMapWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

void USWGMapGroundOverlayWidget::SetMap(USWGPlanetMapWidget* InMap)
{
	Map = InMap;
}

int32 USWGMapGroundOverlayWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const USWGPlanetMapWidget* MapWidget = Map.Get();
	if (!MapWidget || !Overlay.IsValid() || !FSlateApplication::IsInitialized())
	{
		return Layer;
	}

	// Each vertex through the map's own projection, so the field sits on the relief.
	const int32 VertexCount = Overlay.Columns * Overlay.Rows;
	TArray<FVector2D> Local;
	TArray<bool> Visible;
	Local.SetNumUninitialized(VertexCount);
	Visible.SetNumUninitialized(VertexCount);
	const FVector2D Step((Overlay.Max.X - Overlay.Min.X) / (Overlay.Columns - 1), (Overlay.Max.Y - Overlay.Min.Y) / (Overlay.Rows - 1));
	for (int32 Row = 0; Row < Overlay.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Overlay.Columns; ++Column)
		{
			const int32 Index = Row * Overlay.Columns + Column;
			const FVector2D Raw(Overlay.Min.X + Column * Step.X, Overlay.Max.Y - Row * Step.Y);
			Visible[Index] = MapWidget->ProjectToLocalUnclamped(Raw, Local[Index]);
		}
	}

	const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();
	const float Opacity = InWidgetStyle.GetColorAndOpacityTint().A;
	TArray<FSlateVertex> Vertices;
	Vertices.Reserve(VertexCount);
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		FLinearColor Color = Overlay.Colors[Index];
		Color.A *= Opacity;
		Vertices.Add(FSlateVertex::Make(Transform, FVector2f(Local[Index]), FVector2f(0.5f, 0.5f), Color.ToFColor(true)));
	}
	TArray<SlateIndex> Indices;
	for (int32 Row = 0; Row + 1 < Overlay.Rows; ++Row)
	{
		for (int32 Column = 0; Column + 1 < Overlay.Columns; ++Column)
		{
			const int32 TopLeft = Row * Overlay.Columns + Column;
			const int32 TopRight = TopLeft + 1;
			const int32 BottomLeft = TopLeft + Overlay.Columns;
			const int32 BottomRight = BottomLeft + 1;
			if (!Visible[TopLeft] || !Visible[TopRight] || !Visible[BottomLeft] || !Visible[BottomRight])
			{
				continue;
			}
			Indices.Append({ static_cast<SlateIndex>(TopLeft), static_cast<SlateIndex>(BottomLeft), static_cast<SlateIndex>(TopRight),
				static_cast<SlateIndex>(TopRight), static_cast<SlateIndex>(BottomLeft), static_cast<SlateIndex>(BottomRight) });
		}
	}
	if (!Indices.IsEmpty())
	{
		const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, Layer + 1, Handle, Vertices, Indices, nullptr, 0, 0);
	}

	if (Overlay.OutlineColor.A > 0.f)
	{
		// Walks the field's edge through the same projected vertices.
		TArray<FVector2D> Edge;
		auto AddVertex = [&](int32 Row, int32 Column)
		{
			const int32 Index = Row * Overlay.Columns + Column;
			if (Visible[Index])
			{
				Edge.Add(Local[Index]);
			}
		};
		for (int32 Column = 0; Column < Overlay.Columns; ++Column) { AddVertex(0, Column); }
		for (int32 Row = 1; Row < Overlay.Rows; ++Row) { AddVertex(Row, Overlay.Columns - 1); }
		for (int32 Column = Overlay.Columns - 2; Column >= 0; --Column) { AddVertex(Overlay.Rows - 1, Column); }
		for (int32 Row = Overlay.Rows - 2; Row >= 0; --Row) { AddVertex(Row, 0); }
		if (Edge.Num() > 1)
		{
			FLinearColor Outline = Overlay.OutlineColor;
			Outline.A *= Opacity;
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 2, AllottedGeometry.ToPaintGeometry(), Edge,
				ESlateDrawEffect::None, Outline, true, 1.5f);
		}
	}
	return Layer + 2;
}
