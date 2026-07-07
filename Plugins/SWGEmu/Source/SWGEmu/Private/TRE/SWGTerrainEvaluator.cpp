#include "TRE/SWGTerrainEvaluator.h"

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

float FSWGTerrainEvaluator::EvaluateBoundary(const FSWGTerrainBoundary& Boundary, float X, float Y)
{
	switch (Boundary.Type)
	{
		case ESWGTerrainBoundaryType::Circle: return EvaluateBoundaryCircle(Boundary, X, Y);
		case ESWGTerrainBoundaryType::Rectangle: return EvaluateBoundaryRectangle(Boundary, X, Y);
		case ESWGTerrainBoundaryType::Polygon: return EvaluateBoundaryPolygon(Boundary, X, Y);
		default: return 0.0f;
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

			if (Height > Var3)
			{
				Var2 = (Height - Var3) / (Var4 - Var3) * (Var4 - Var2) + Var2;
			}

			Height = (Var2 - Height) * TransformValue + Height;
			break;
		}
		default:
			break;
	}
}

float FSWGTerrainEvaluator::ProcessLayer(const FSWGTerrainLayer& Layer, float X, float Y, float& Height, float ParentTransform, const FSWGMapGroup& MapGroup)
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
		// No filter types are parsed yet (FSWGTerrainReader skips them) — nothing to run here.

		for (const FSWGTerrainAffector& Affector : Layer.Affectors)
		{
			if (!Affector.bEnabled) continue;
			ApplyAffector(Affector, X, Y, TransformValue * ParentTransform, Height, MapGroup);
		}

		for (const FSWGTerrainLayer& Child : Layer.Children)
		{
			if (!Child.bEnabled) continue;
			ProcessLayer(Child, X, Y, Height, ParentTransform * TransformValue, MapGroup);
		}
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
