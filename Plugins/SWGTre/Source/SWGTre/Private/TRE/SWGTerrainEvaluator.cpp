#include "TRE/SWGTerrainEvaluator.h"

namespace
{
	bool GTraceEnabled = false;
	float GTraceX = 0.0f;
	float GTraceY = 0.0f;

	bool IsTraceTarget(float X, float Y)
	{
		return GTraceEnabled && FMath::IsNearlyEqual(X, GTraceX, 0.01f) && FMath::IsNearlyEqual(Y, GTraceY, 0.01f);
	}
}

void FSWGTerrainEvaluator::SetDebugTraceTarget(float X, float Y, bool bEnable)
{
	GTraceEnabled = bEnable;
	GTraceX = X;
	GTraceY = Y;
}

float FSWGTerrainEvaluator::CalculateFeathering(float Value, int32 FeatheringType)
{
	switch (FeatheringType)
	{
		case 1: return Value * Value;
		case 2: return FMath::Sqrt(Value);
		case 3: return Value * Value * (3.0f - 2.0f * Value);
		case 0: return Value;
		default: return 0.0f;
	}
}

float FSWGTerrainEvaluator::EvaluateBoundaryCircle(const FSWGTerrainBoundary& Boundary, float X, float Y)
{
	const float Dx = Boundary.CenterX - X;
	const float Dy = Boundary.CenterY - Y;
	const float DistSq = Dy * Dy + Dx * Dx;
	const float RadiusSq = Boundary.Radius * Boundary.Radius;

	if (DistSq <= RadiusSq)
	{
		const float InnerRadius = (1.0f - Boundary.FeatheringAmount) * Boundary.Radius;
		const float InnerSq = InnerRadius * InnerRadius;

		if (DistSq > InnerSq)
		{
			return 1.0f - (DistSq - InnerSq) / (RadiusSq - InnerSq);
		}
		return 1.0f;
	}

	return 0.0f;
}

float FSWGTerrainEvaluator::EvaluateBoundaryRectangle(const FSWGTerrainBoundary& Boundary, float X, float Y)
{
	// Our reader doesn't sort X0/X1, Y0/Y1 the way Core3's initialize() does — sort locally.
	const float MinX = FMath::Min(Boundary.X0, Boundary.X1);
	const float MaxX = FMath::Max(Boundary.X0, Boundary.X1);
	const float MinY = FMath::Min(Boundary.Y0, Boundary.Y1);
	const float MaxY = FMath::Max(Boundary.Y0, Boundary.Y1);

	if (X < MinX) return 0.0f;
	if (X > MaxX || Y < MinY) return 0.0f;
	if (Y > MaxY) return 0.0f;

	if (Boundary.FeatheringAmount == 0.0f) return 1.0f;

	// initialize()'s derived inset box, recomputed per call rather than cached —
	// cheap relative to the boundary test itself, and avoids mutating the reader's data.
	const float Width = MaxX - MinX;
	const float Height = MaxY - MinY;
	const float MinDim = (Height >= Width) ? Width : Height;
	const float Inset = Boundary.FeatheringAmount * MinDim * 0.5f;
	const float NewX0 = MinX + Inset;
	const float NewY0 = MinY + Inset;
	const float NewX1 = MaxX - Inset;
	const float NewY1 = MaxY - Inset;

	if (!(X < NewX1 || X > NewX0 || Y < NewY1 || Y > NewY0))
		return 1.0f;

	const float V36 = X - MinX;
	const float V35 = MaxX - X;
	const float V34 = Y - MinY;
	const float VY = MaxY - Y;

	const float V30 = Boundary.FeatheringAmount * MinDim * 0.5f;
	float V29 = V30;
	V29 = FMath::Min(V29, V36);
	V29 = FMath::Min(V29, V35);
	V29 = FMath::Min(V29, V34);
	V29 = FMath::Min(V29, VY);

	return V29 / V30;
}

float FSWGTerrainEvaluator::EvaluateBoundaryPolygon(const FSWGTerrainBoundary& Boundary, float X, float Y)
{
	const TArray<FVector2D>& Verts = Boundary.Vertices;
	if (Verts.Num() <= 0) return 0.0f;

	float MinX = TNumericLimits<float>::Max(), MinY = TNumericLimits<float>::Max();
	float MaxX = -TNumericLimits<float>::Max(), MaxY = -TNumericLimits<float>::Max();
	for (const FVector2D& V : Verts)
	{
		MinX = FMath::Min(MinX, (float)V.X);
		MaxX = FMath::Max(MaxX, (float)V.X);
		MinY = FMath::Min(MinY, (float)V.Y);
		MaxY = FMath::Max(MaxY, (float)V.Y);
	}

	if (X < MinX) return 0.0f;
	if (X > MaxX || Y < MinY) return 0.0f;
	if (Y > MaxY) return 0.0f;

	bool bInside = false;
	FVector2D LastPoint = Verts.Last();
	for (const FVector2D& Point : Verts)
	{
		if ((Point.Y <= Y && Y < LastPoint.Y) || (LastPoint.Y <= Y && Y < Point.Y))
		{
			if ((Y - Point.Y) * (LastPoint.X - Point.X) / (LastPoint.Y - Point.Y) + Point.X > X)
			{
				bInside = !bInside;
			}
		}
		LastPoint = Point;
	}

	if (!bInside) return 0.0f;
	if (Boundary.FeatheringAmount == 0.0f) return 1.0f;

	const double Target = (double)Boundary.FeatheringAmount * Boundary.FeatheringAmount;
	double NearestSq = Target;

	for (const FVector2D& Point : Verts)
	{
		const double Dy = Y - Point.Y;
		const double Dx = X - Point.X;
		const double D = Dy * Dy + Dx * Dx;
		if (D < NearestSq) NearestSq = D;
	}

	LastPoint = Verts.Last();
	for (const FVector2D& Point : Verts)
	{
		const double V35 = LastPoint.X - Point.X;
		const double V36 = LastPoint.Y - Point.Y;
		const double V44 = Point.Y - LastPoint.Y;
		const double V45 = Point.X - LastPoint.X;
		const double Denom = V36 * V36 + V35 * V35;

		if (Denom != 0.0)
		{
			const double V37 = ((X - LastPoint.X) * V45 + (Y - LastPoint.Y) * V44) / Denom;
			if (V37 >= 0.0 && V37 <= 1.0)
			{
				const double V39 = X - (V45 * V37 + LastPoint.X);
				const double V40 = Y - (V44 * V37 + LastPoint.Y);
				const double D = V40 * V40 + V39 * V39;
				if (D < NearestSq) NearestSq = D;
			}
		}
		LastPoint = Point;
	}

	if (NearestSq >= Target - 0.0001 && NearestSq <= Target + 0.0001)
	{
		return 1.0f;
	}

	return (float)(FMath::Sqrt(NearestSq) / Boundary.FeatheringAmount);
}

float FSWGTerrainEvaluator::EvaluateBoundaryPolyline(const FSWGTerrainBoundary& Boundary, float X, float Y)
{
	// Confirmed port of BoundaryPolyline::process — a road/path's influence
	// falls off with distance to the nearest vertex or segment, not an
	// inside/outside test like Polygon (this is an open path, not a closed
	// shape). initialize()'s min/max in Core3 are cached once from the
	// original (untranslated) points; recomputed per call here instead,
	// same tradeoff EvaluateBoundaryRectangle already makes.
	const TArray<FVector2D>& Verts = Boundary.Vertices;
	if (Verts.Num() <= 0) return 0.0f;

	float MinX = TNumericLimits<float>::Max(), MinY = TNumericLimits<float>::Max();
	float MaxX = -TNumericLimits<float>::Max(), MaxY = -TNumericLimits<float>::Max();
	for (const FVector2D& V : Verts)
	{
		MinX = FMath::Min(MinX, (float)V.X);
		MaxX = FMath::Max(MaxX, (float)V.X);
		MinY = FMath::Min(MinY, (float)V.Y);
		MaxY = FMath::Max(MaxY, (float)V.Y);
	}
	MinX -= Boundary.LineWidth; MaxX += Boundary.LineWidth;
	MinY -= Boundary.LineWidth; MaxY += Boundary.LineWidth;

	if (X < MinX) return 0.0f;
	if (X > MaxX || Y < MinY) return 0.0f;
	if (Y > MaxY) return 0.0f;

	const double LineWidthSq = (double)Boundary.LineWidth * Boundary.LineWidth;
	double NearestSq = LineWidthSq;

	for (const FVector2D& Point : Verts)
	{
		const double Dy = Y - Point.Y;
		const double Dx = X - Point.X;
		const double D = Dy * Dy + Dx * Dx;
		if (D < NearestSq) NearestSq = D;
	}

	for (int32 i = 0; i + 1 < Verts.Num(); ++i)
	{
		const FVector2D& Point = Verts[i];
		const FVector2D& Point2 = Verts[i + 1];

		const double Ex = Point2.X - Point.X;
		const double Ey = Point2.Y - Point.Y;
		const double Denom = Ey * Ey + Ex * Ex;
		if (Denom == 0.0) continue;

		const double T = ((Y - Point.Y) * Ey + (X - Point.X) * Ex) / Denom;
		if (T >= 0.0 && T <= 1.0)
		{
			const double Px = X - (Ex * T + Point.X);
			const double Py = Y - (Ey * T + Point.Y);
			const double D = Py * Py + Px * Px;
			if (D < NearestSq) NearestSq = D;
		}
	}

	if (NearestSq >= LineWidthSq)
	{
		return 0.0f;
	}

	const double InnerRadius = (1.0 - Boundary.FeatheringAmount) * Boundary.LineWidth;
	if (NearestSq >= InnerRadius * InnerRadius)
	{
		return (float)(1.0 - (FMath::Sqrt(NearestSq) - InnerRadius) / (Boundary.LineWidth - InnerRadius));
	}

	return 1.0f;
}

float FSWGTerrainEvaluator::EvaluateBoundary(const FSWGTerrainBoundary& Boundary, float X, float Y)
{
	switch (Boundary.Type)
	{
		case ESWGTerrainBoundaryType::Circle: return EvaluateBoundaryCircle(Boundary, X, Y);
		case ESWGTerrainBoundaryType::Rectangle: return EvaluateBoundaryRectangle(Boundary, X, Y);
		case ESWGTerrainBoundaryType::Polygon: return EvaluateBoundaryPolygon(Boundary, X, Y);
		case ESWGTerrainBoundaryType::Polyline: return EvaluateBoundaryPolyline(Boundary, X, Y);
		default: return 0.0f;
	}
}

float FSWGTerrainEvaluator::EvaluateFilter(const FSWGTerrainFilter& Filter, float X, float Y, float Height, const FSWGMapGroup& MapGroup)
{
	switch (Filter.Type)
	{
		case ESWGTerrainFilterType::Height:
		{
			// Confirmed port of FilterHeight::process — note it only ever looks at
			// the running height accumulator (Core3's "baseValue"), not (x,y) or the
			// boundary-derived transformValue it's also passed; feathers Height
			// itself back down to a 0-1 factor near the min/max band's edges.
			if (Height > Filter.MinHeight && Height < Filter.MaxHeight)
			{
				const float HalfBand = (Filter.MaxHeight - Filter.MinHeight) * Filter.FeatheringAmount * 0.5f;
				if (Filter.MinHeight + HalfBand <= Height)
				{
					if (Filter.MaxHeight - HalfBand >= Height)
					{
						return 1.0f;
					}
					return (Filter.MaxHeight - Height) / HalfBand;
				}
				return (Height - Filter.MinHeight) / HalfBand;
			}
			return 0.0f;
		}
		case ESWGTerrainFilterType::Fractal:
		{
			// Confirmed port of FilterFractal::process — unlike FilterHeight, this
			// looks at fractal noise sampled at (x,y) (scaled by Filter.Scale), not
			// the running height accumulator. Missing this was confirmed (via a
			// per-layer height trace) as the cause of Hills/Mesa/Canyon/Erosion/
			// Outcropping-type layers applying at full, unfiltered strength across
			// their whole boundary instead of the patchy sub-area the real noise
			// threshold produces.
			const FSWGMapFractal* Fractal = MapGroup.FindFractal(Filter.FractalId);
			if (!Fractal) return 1.0f;

			const float NoiseResult = Fractal->GetNoise(X, Y) * Filter.Scale;

			if (NoiseResult > Filter.Min && NoiseResult < Filter.Max)
			{
				const float HalfBand = (Filter.Max - Filter.Min) * Filter.FeatheringAmount * 0.5f;
				if (Filter.Min + HalfBand <= NoiseResult)
				{
					if (Filter.Max - HalfBand >= NoiseResult)
					{
						return 1.0f;
					}
					return (Filter.Max - NoiseResult) / HalfBand;
				}
				return (NoiseResult - Filter.Min) / HalfBand;
			}
			return 0.0f;
		}
		default:
			return 0.0f;
	}
}

void FSWGTerrainEvaluator::ApplyAffector(const FSWGTerrainAffector& Affector, float X, float Y, float TransformValue, float& Height, const FSWGMapGroup& MapGroup)
{
	if (TransformValue == 0.0f) return;

	switch (Affector.Type)
	{
		case ESWGTerrainAffectorType::HeightConstant:
		{
			float Result;
			switch (Affector.OperationType)
			{
				case 1: Result = TransformValue * Affector.Height + Height; break;
				case 2: Result = Height - TransformValue * Affector.Height; break;
				case 3: Result = Height + (Height * Affector.Height - Height) * TransformValue; break;
				case 4: Result = 0.0f; break;
				default: Result = (1.0f - TransformValue) * Height + TransformValue * Affector.Height; break;
			}
			Height = Result;
			break;
		}
		case ESWGTerrainAffectorType::HeightFractal:
		{
			const FSWGMapFractal* Fractal = MapGroup.FindFractal(Affector.FractalId);
			const float NoiseResult = Fractal ? (Fractal->GetNoise(X, Y) * Affector.Height) : 0.0f;

			float Result;
			switch (Affector.OperationType)
			{
				case 1: Result = Height + NoiseResult * TransformValue; break;
				case 2: Result = Height - NoiseResult * TransformValue; break;
				case 3: Result = Height + (NoiseResult * Height - Height) * TransformValue; break;
				case 4: Result = Height; break;
				default: Result = Height + (NoiseResult - Height) * TransformValue; break;
			}
			Height = Result;
			break;
		}
		case ESWGTerrainAffectorType::HeightTerrace:
		{
			if (Affector.Height <= 0.0f) return;

			float Var1 = FMath::Fmod(Height, Affector.Height);
			if (Height == 0.0f) Var1 += Affector.Height;

			float Var2 = Height - Var1;
			const float Var3 = Affector.Height * Affector.FlatRatio + Var2;
			const float Var4 = Affector.Height + Var2;

			// FlatRatio == 1.0 makes Var3 == Var4 (an all-flat terrace step, no
			// sloped portion) — dividing by (Var4 - Var3) == 0 there produces
			// Inf/NaN for every sample above the plateau in this affector's
			// region, which is exactly the widespread, regularly-spaced needle
			// spikes seen in a live capture (not sparse/random — a systematic,
			// deterministic blowup, confirmed by ruling out the noise-bias NaN
			// path first: that fix alone didn't change the spikes at all).
			if (Height > Var3 && !FMath::IsNearlyEqual(Var4, Var3))
			{
				Var2 = (Height - Var3) / (Var4 - Var3) * (Var4 - Var2) + Var2;
			}

			Height = (Var2 - Height) * TransformValue + Height;
			break;
		}
		case ESWGTerrainAffectorType::Road:
		{
			// Confirmed port of AffectorRoad::process. The 'ROAD' (shader-only)
			// variant never reaches here with bRoadIsHeightType set, matching
			// Core3's own "type != 'HDTA' -> return" early-out.
			if (!Affector.bRoadIsHeightType) break;

			constexpr float OutOfRange = 500.0f;
			const float DeltaXStart = X - Affector.RoadStartPoint.X;
			const float DeltaYStart = Y - Affector.RoadStartPoint.Y;
			if (DeltaXStart * DeltaXStart + DeltaYStart * DeltaYStart > OutOfRange * OutOfRange)
			{
				break;
			}

			bool bOnRoadway = false;
			FVector2D RoadStart = FVector2D::ZeroVector;
			FVector2D RoadCenter = FVector2D::ZeroVector;
			float RoadDirection = 0.0f;

			for (const FSWGTerrainRoadRectangle& Rect : Affector.RoadRectangles)
			{
				const float RDeltaX = Rect.CenterX - X;
				const float RDeltaY = Rect.CenterY - Y;
				const float RotatedX = RDeltaX * FMath::Cos(-Rect.Direction) + RDeltaY * FMath::Sin(-Rect.Direction);
				const float RotatedYForRect = RDeltaX * FMath::Sin(-Rect.Direction) - RDeltaY * FMath::Cos(-Rect.Direction);

				if (FMath::Abs(RotatedX) > Rect.Width * 0.5f || FMath::Abs(RotatedYForRect) > Rect.Height * 0.5f)
				{
					continue;
				}

				bOnRoadway = true;
				RoadStart = FVector2D(Rect.RoadStartX, Rect.RoadStartY);
				RoadCenter = FVector2D(Rect.CenterX, Rect.CenterY);
				RoadDirection = Rect.Direction;
				break;
			}

			if (!bOnRoadway) break;

			for (const FSWGTerrainRoadSegment& Segment : Affector.RoadSegments)
			{
				if (Segment.Positions.Num() == 0) continue;
				if (Segment.Positions[0].X != RoadStart.X || Segment.Positions[0].Y != RoadStart.Y) continue;

				FindNearestRoadHeight(Segment, Height, X, Y, RoadCenter, RoadDirection);
				break;
			}
			break;
		}
		default:
			break;
	}
}

void FSWGTerrainEvaluator::FindNearestRoadHeight(const FSWGTerrainRoadSegment& Segment, float& Height, float WorldX, float WorldY, const FVector2D& RoadCenter, float Direction)
{
	const int32 PositionsSize = Segment.Positions.Num();
	if (PositionsSize == 0) return;

	if (PositionsSize < 3 || Segment.bFlatRoad)
	{
		Height = Segment.Positions[0].Z;
		return;
	}

	auto SquaredDistanceTo = [](const FSWGTerrainRoadPoint& P, float X, float Y)
	{
		const float Dx = P.X - X;
		const float Dy = P.Y - Y;
		return Dx * Dx + Dy * Dy;
	};

	const FSWGTerrainRoadPoint& FirstPoint = Segment.Positions[0];
	const FSWGTerrainRoadPoint& LastPoint = Segment.Positions[PositionsSize - 1];

	const float WorldDistToFirstSq = SquaredDistanceTo(FirstPoint, WorldX, WorldY);
	const float WorldDistToLastSq = SquaredDistanceTo(LastPoint, WorldX, WorldY);

	const float DeltaX = RoadCenter.X - WorldX;
	const float DeltaY = RoadCenter.Y - WorldY;

	const float RotatedY = DeltaX * FMath::Sin(-Direction) - DeltaY * FMath::Cos(-Direction);
	const float RotatedYSq = RotatedY * RotatedY;

	float NewHeight = 0.0f;
	int32 HeightSegment = -1;
	const int32 HalfSize = (PositionsSize - 1) / 2;

	if (WorldDistToFirstSq <= WorldDistToLastSq)
	{
		for (int32 i = 1; i < PositionsSize; ++i)
		{
			const FSWGTerrainRoadPoint& Point = Segment.Positions[i];
			const float ThisDistSq = SquaredDistanceTo(Point, RoadCenter.X, RoadCenter.Y);

			if (ThisDistSq > RotatedYSq && i <= HalfSize) continue;

			HeightSegment = i;
			NewHeight = Point.Z;
			break;
		}
	}
	else
	{
		for (int32 i = PositionsSize - 1; i > 0; --i)
		{
			const FSWGTerrainRoadPoint& Point = Segment.Positions[i];
			const float ThisDistSq = SquaredDistanceTo(Point, RoadCenter.X, RoadCenter.Y);

			if (ThisDistSq > RotatedYSq && i > HalfSize) continue;

			HeightSegment = i + 1;
			NewHeight = Point.Z;
			break;
		}
	}

	if (HeightSegment > 0 && HeightSegment <= (PositionsSize - 1))
	{
		const FSWGTerrainRoadPoint& SegmentBefore = Segment.Positions[HeightSegment - 1];
		const FSWGTerrainRoadPoint& SegmentAfter = Segment.Positions[HeightSegment];

		const float TotalDistance = FMath::Sqrt(FMath::Square(SegmentAfter.X - SegmentBefore.X) + FMath::Square(SegmentAfter.Y - SegmentBefore.Y));
		const float AfterSegDistFromCenter = FMath::Sqrt(FMath::Square(SegmentAfter.X - RoadCenter.X) + FMath::Square(SegmentAfter.Y - RoadCenter.Y));
		float PositionDistance = TotalDistance - FMath::Abs(AfterSegDistFromCenter - FMath::Abs(RotatedY));

		if (RotatedY > 0.0f && HalfSize + 1 == HeightSegment)
		{
			PositionDistance = TotalDistance - (AfterSegDistFromCenter + FMath::Abs(RotatedY));
		}

		const float DistanceVar = FMath::Clamp(PositionDistance / TotalDistance, 0.001f, 1.0f);
		const float SegmentDiff = SegmentAfter.Z - SegmentBefore.Z;

		NewHeight = SegmentBefore.Z + (DistanceVar * SegmentDiff);
	}

	if (NewHeight != 0.0f)
	{
		Height = NewHeight;
	}
}

float FSWGTerrainEvaluator::ProcessLayer(const FSWGTerrainLayer& Layer, float X, float Y, float& Height, float ParentTransform, const FSWGMapGroup& MapGroup)
{
	const bool bTrace = IsTraceTarget(X, Y);
	const float HeightBeforeLayer = Height;

	float TransformValue = 0.0f;
	bool bHasEnabledBoundary = false;

	for (const FSWGTerrainBoundary& Boundary : Layer.Boundaries)
	{
		if (!Boundary.bEnabled) continue;
		bHasEnabledBoundary = true;

		float Result = EvaluateBoundary(Boundary, X, Y);
		Result = CalculateFeathering(Result, Boundary.FeatheringType);
		TransformValue = FMath::Max(TransformValue, Result);

		if (TransformValue >= 1.0f) break;
	}

	if (!bHasEnabledBoundary)
	{
		TransformValue = 1.0f;
	}

	if (Layer.bInvertBoundaries)
	{
		TransformValue = 1.0f - TransformValue;
	}

	if (bTrace)
	{
		UE_LOG(LogTemp, Warning, TEXT("HEIGHTTRACE ENTER layer='%s' afterBoundaries transformValue=%.4f hasEnabledBoundary=%d numFilters=%d numAffectors=%d numChildren=%d"),
			*Layer.Name, TransformValue, bHasEnabledBoundary ? 1 : 0, Layer.Filters.Num(), Layer.Affectors.Num(), Layer.Children.Num());
	}

	if (TransformValue != 0.0f)
	{
		// Confirmed port of processTerrain's filter step: filters can only
		// shrink TransformValue further (never grow it back up), same
		// short-circuit-on-zero and invertFilters handling as Core3. Only
		// FilterHeight is modeled (see FSWGTerrainFilter's comment) — other
		// filter types are silently absent from Layer.Filters and simply
		// don't contribute, same graceful-skip behavior as unrecognized
		// boundary/affector types already had.
		for (const FSWGTerrainFilter& Filter : Layer.Filters)
		{
			if (!Filter.bEnabled) continue;

			float Result = EvaluateFilter(Filter, X, Y, Height, MapGroup);
			Result = CalculateFeathering(Result, Filter.FeatheringType);
			const float PrevTransformValue = TransformValue;
			TransformValue = FMath::Min(TransformValue, Result);

			if (bTrace)
			{
				UE_LOG(LogTemp, Warning, TEXT("HEIGHTTRACE layer='%s' filterType=%d rawResult=%.4f transformValue %.4f -> %.4f"),
					*Layer.Name, (int32)Filter.Type, Result, PrevTransformValue, TransformValue);
			}

			if (TransformValue == 0.0f) break;
		}

		if (Layer.bInvertFilters)
		{
			TransformValue = 1.0f - TransformValue;
		}
	}

	if (TransformValue != 0.0f)
	{
		for (const FSWGTerrainAffector& Affector : Layer.Affectors)
		{
			if (!Affector.bEnabled) continue;
			const float HeightBeforeAffector = Height;
			ApplyAffector(Affector, X, Y, TransformValue * ParentTransform, Height, MapGroup);

			if (bTrace && Height != HeightBeforeAffector)
			{
				UE_LOG(LogTemp, Warning, TEXT("HEIGHTTRACE layer='%s' affectorType=%d height %.4f -> %.4f (transform=%.4f parentTransform=%.4f)"),
					*Layer.Name, (int32)Affector.Type, HeightBeforeAffector, Height, TransformValue, ParentTransform);
			}
		}

		for (const FSWGTerrainLayer& Child : Layer.Children)
		{
			if (!Child.bEnabled) continue;
			ProcessLayer(Child, X, Y, Height, ParentTransform * TransformValue, MapGroup);
		}
	}

	if (bTrace && Height != HeightBeforeLayer)
	{
		UE_LOG(LogTemp, Warning, TEXT("HEIGHTTRACE layer='%s' TOTAL height %.4f -> %.4f (transformValue=%.4f hasEnabledBoundary=%d)"),
			*Layer.Name, HeightBeforeLayer, Height, TransformValue, bHasEnabledBoundary ? 1 : 0);
	}

	return TransformValue;
}

float FSWGTerrainEvaluator::GetHeight(const FSWGTerrainData& Data, float X, float Y)
{
	float Height = 0.0f;

	for (const FSWGTerrainLayer& Layer : Data.TopLevelLayers)
	{
		if (Layer.bEnabled)
		{
			ProcessLayer(Layer, X, Y, Height, 1.0f, Data.MapGroup);
		}
	}

	return Height;
}

void FSWGTerrainEvaluator::ApplyShaderAffector(const FSWGTerrainAffector& Affector, float TransformValue, TMap<int32, float>& OutWeights)
{
	if (TransformValue == 0.0f) return;

	// Affectors carry their own secondary feathering (ShaderFeatheringType/
	// Amount) on top of whatever the layer's boundaries/filters already
	// produced — same CalculateFeathering curve reused for boundary/filter
	// edges elsewhere in this evaluator.
	const float Strength = CalculateFeathering(FMath::Clamp(TransformValue, 0.0f, 1.0f), Affector.ShaderFeatheringType) * Affector.ShaderFeatheringAmount
		+ TransformValue * (1.0f - Affector.ShaderFeatheringAmount);

	switch (Affector.Type)
	{
		case ESWGTerrainAffectorType::ShaderConstant:
		{
			// Alpha-over paint: existing weights recede proportionally to make
			// room for this layer's contribution, keeping the total <= 1.0.
			for (auto& Pair : OutWeights)
			{
				Pair.Value *= (1.0f - Strength);
			}
			float& Weight = OutWeights.FindOrAdd(Affector.ShaderFamilyId);
			Weight += Strength;
			break;
		}
		case ESWGTerrainAffectorType::ShaderReplace:
		{
			float* OldWeight = OutWeights.Find(Affector.ShaderOldFamilyId);
			const float Move = OldWeight ? (*OldWeight * Strength) : 0.0f;
			if (OldWeight)
			{
				*OldWeight -= Move;
			}
			if (Move > 0.0f)
			{
				OutWeights.FindOrAdd(Affector.ShaderNewFamilyId) += Move;
			}
			break;
		}
		default:
			break;
	}
}

float FSWGTerrainEvaluator::ProcessShaderLayer(const FSWGTerrainLayer& Layer, float X, float Y, float& Height, float ParentTransform, const FSWGMapGroup& MapGroup, TMap<int32, float>& OutWeights)
{
	float TransformValue = 0.0f;
	bool bHasEnabledBoundary = false;

	for (const FSWGTerrainBoundary& Boundary : Layer.Boundaries)
	{
		if (!Boundary.bEnabled) continue;
		bHasEnabledBoundary = true;

		float Result = EvaluateBoundary(Boundary, X, Y);
		Result = CalculateFeathering(Result, Boundary.FeatheringType);
		TransformValue = FMath::Max(TransformValue, Result);

		if (TransformValue >= 1.0f) break;
	}

	if (!bHasEnabledBoundary)
	{
		TransformValue = 1.0f;
	}

	if (Layer.bInvertBoundaries)
	{
		TransformValue = 1.0f - TransformValue;
	}

	if (TransformValue != 0.0f)
	{
		for (const FSWGTerrainFilter& Filter : Layer.Filters)
		{
			if (!Filter.bEnabled) continue;

			float Result = EvaluateFilter(Filter, X, Y, Height, MapGroup);
			Result = CalculateFeathering(Result, Filter.FeatheringType);
			TransformValue = FMath::Min(TransformValue, Result);

			if (TransformValue == 0.0f) break;
		}

		if (Layer.bInvertFilters)
		{
			TransformValue = 1.0f - TransformValue;
		}
	}

	if (TransformValue != 0.0f)
	{
		// Only shader-type affectors contribute here — height/road affectors
		// are silently skipped, matching Core3's own requestedType-filtered
		// dispatch (see AffectorProceduralRule::process's affectorType check).
		for (const FSWGTerrainAffector& Affector : Layer.Affectors)
		{
			if (!Affector.bEnabled) continue;
			if (Affector.Type != ESWGTerrainAffectorType::ShaderConstant && Affector.Type != ESWGTerrainAffectorType::ShaderReplace) continue;

			ApplyShaderAffector(Affector, TransformValue * ParentTransform, OutWeights);
		}

		for (const FSWGTerrainLayer& Child : Layer.Children)
		{
			if (!Child.bEnabled) continue;
			ProcessShaderLayer(Child, X, Y, Height, ParentTransform * TransformValue, MapGroup, OutWeights);
		}
	}

	return TransformValue;
}

void FSWGTerrainEvaluator::GetShaderWeights(const FSWGTerrainData& Data, float X, float Y, TMap<int32, float>& OutWeights)
{
	// Local accumulator, starting at 0 like Core3's own per-call baseValue —
	// FilterHeight checks during this walk see only what this same walk's
	// (nonexistent) height-type affectors would have produced, exactly as
	// getEnvironmentID's independent traversal does, not the real terrain height.
	float Height = 0.0f;

	for (const FSWGTerrainLayer& Layer : Data.TopLevelLayers)
	{
		if (Layer.bEnabled)
		{
			ProcessShaderLayer(Layer, X, Y, Height, 1.0f, Data.MapGroup, OutWeights);
		}
	}
}
