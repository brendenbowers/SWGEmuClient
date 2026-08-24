#include "TRE/SWGTerrainModifier.h"

namespace
{
	FVector2D LocalToWorld(const FVector2D& Local, const FSWGTerrainPlacement& Placement, float SinYaw, float CosYaw)
	{
		return FVector2D(
			Placement.WorldCenter.X + Local.X * CosYaw - Local.Y * SinYaw,
			Placement.WorldCenter.Y + Local.X * SinYaw + Local.Y * CosYaw);
	}

	void TransformBoundary(FSWGTerrainBoundary& Boundary, const FSWGTerrainPlacement& Placement, float SinYaw, float CosYaw)
	{
		switch (Boundary.Type)
		{
			case ESWGTerrainBoundaryType::Circle:
			{
				const FVector2D Center = LocalToWorld(FVector2D(Boundary.CenterX, Boundary.CenterY), Placement, SinYaw, CosYaw);
				Boundary.CenterX = (float)Center.X;
				Boundary.CenterY = (float)Center.Y;
				break;
			}

			case ESWGTerrainBoundaryType::Rectangle:
			{
				// An axis-aligned rectangle stops being one under rotation, and
				// EvaluateBoundaryRectangle only understands axis-aligned min/max.
				// Rotated placements therefore become an equivalent 4-vertex
				// polygon; unrotated ones stay a rectangle so they keep
				// EvaluateBoundaryRectangle's cheaper path and its own feathering
				// behaviour, which is not identical to the polygon one.
				if (FMath::IsNearlyZero(Placement.YawRadians))
				{
					Boundary.X0 += (float)Placement.WorldCenter.X;
					Boundary.X1 += (float)Placement.WorldCenter.X;
					Boundary.Y0 += (float)Placement.WorldCenter.Y;
					Boundary.Y1 += (float)Placement.WorldCenter.Y;
					break;
				}

				const float MinX = FMath::Min(Boundary.X0, Boundary.X1);
				const float MaxX = FMath::Max(Boundary.X0, Boundary.X1);
				const float MinY = FMath::Min(Boundary.Y0, Boundary.Y1);
				const float MaxY = FMath::Max(Boundary.Y0, Boundary.Y1);

				// FeatheringAmount changes meaning with the boundary type, so it
				// has to be converted along with the shape. EvaluateBoundaryRectangle
				// reads it as a 0-1 fraction of the smaller side
				// ("Inset = FeatheringAmount * MinDim * 0.5"); EvaluateBoundaryPolygon
				// reads it as an absolute distance. Carrying the raw fraction across
				// would turn a normally-feathered pad (e.g. retail's 0.1279) into a
				// ~13 cm ramp — a vertical-walled mesa.
				const float MinDim = FMath::Min(MaxX - MinX, MaxY - MinY);
				Boundary.FeatheringAmount = Boundary.FeatheringAmount * MinDim * 0.5f;

				Boundary.Type = ESWGTerrainBoundaryType::Polygon;
				Boundary.Vertices = {
					LocalToWorld(FVector2D(MinX, MinY), Placement, SinYaw, CosYaw),
					LocalToWorld(FVector2D(MaxX, MinY), Placement, SinYaw, CosYaw),
					LocalToWorld(FVector2D(MaxX, MaxY), Placement, SinYaw, CosYaw),
					LocalToWorld(FVector2D(MinX, MaxY), Placement, SinYaw, CosYaw),
				};
				break;
			}

			case ESWGTerrainBoundaryType::Polygon:
			case ESWGTerrainBoundaryType::Polyline:
			{
				for (FVector2D& Vertex : Boundary.Vertices)
				{
					Vertex = LocalToWorld(Vertex, Placement, SinYaw, CosYaw);
				}
				break;
			}

			default:
				break;
		}
	}

	void TransformAffector(FSWGTerrainAffector& Affector, const FSWGTerrainPlacement& Placement)
	{
		// Only the "lerp" operation (0) treats Height as a target elevation to
		// blend the terrain towards — that is the one that has to be rebased onto
		// the ground the object was actually placed on. Operations 1-3
		// (add/subtract/scale) treat it as a delta or factor, and 4 (zero) ignores
		// it, so offsetting any of those would corrupt them.
		if (Affector.Type == ESWGTerrainAffectorType::HeightConstant && Affector.OperationType == 0)
		{
			Affector.Height += Placement.BaseHeight;
		}
	}

	bool AccumulateBoundaryBounds(const FSWGTerrainBoundary& Boundary, FBox2D& InOutBounds)
	{
		switch (Boundary.Type)
		{
			case ESWGTerrainBoundaryType::Circle:
			{
				const FVector2D Center(Boundary.CenterX, Boundary.CenterY);
				InOutBounds += Center - FVector2D(Boundary.Radius, Boundary.Radius);
				InOutBounds += Center + FVector2D(Boundary.Radius, Boundary.Radius);
				return true;
			}

			case ESWGTerrainBoundaryType::Rectangle:
			{
				InOutBounds += FVector2D(Boundary.X0, Boundary.Y0);
				InOutBounds += FVector2D(Boundary.X1, Boundary.Y1);
				return true;
			}

			case ESWGTerrainBoundaryType::Polygon:
			case ESWGTerrainBoundaryType::Polyline:
			{
				if (Boundary.Vertices.IsEmpty())
				{
					return false;
				}

				// A polyline's influence reaches LineWidth either side of the path itself.
				const float Pad = (Boundary.Type == ESWGTerrainBoundaryType::Polyline) ? Boundary.LineWidth : 0.0f;
				for (const FVector2D& Vertex : Boundary.Vertices)
				{
					InOutBounds += Vertex - FVector2D(Pad, Pad);
					InOutBounds += Vertex + FVector2D(Pad, Pad);
				}
				return true;
			}

			default:
				return false;
		}
	}

	bool AccumulateLayerBounds(const FSWGTerrainLayer& Layer, FBox2D& InOutBounds)
	{
		bool bAny = false;

		for (const FSWGTerrainBoundary& Boundary : Layer.Boundaries)
		{
			bAny |= AccumulateBoundaryBounds(Boundary, InOutBounds);
		}

		for (const FSWGTerrainLayer& Child : Layer.Children)
		{
			bAny |= AccumulateLayerBounds(Child, InOutBounds);
		}

		return bAny;
	}
}

void FSWGTerrainModifier::TransformLayer(FSWGTerrainLayer& Layer, const FSWGTerrainPlacement& Placement)
{
	const float SinYaw = FMath::Sin(Placement.YawRadians);
	const float CosYaw = FMath::Cos(Placement.YawRadians);

	for (FSWGTerrainBoundary& Boundary : Layer.Boundaries)
	{
		TransformBoundary(Boundary, Placement, SinYaw, CosYaw);
	}

	for (FSWGTerrainAffector& Affector : Layer.Affectors)
	{
		TransformAffector(Affector, Placement);
	}

	for (FSWGTerrainLayer& Child : Layer.Children)
	{
		TransformLayer(Child, Placement);
	}
}

bool FSWGTerrainModifier::GetLayerWorldBounds(const FSWGTerrainLayer& Layer, FBox2D& OutBounds)
{
	FBox2D Bounds(ForceInit);
	if (!AccumulateLayerBounds(Layer, Bounds))
	{
		return false;
	}

	OutBounds = Bounds;
	return true;
}
