#include "Structure/SWGPlacementRules.h"

FBox2D FSWGPlacementRules::GetServerFootprintRect(const FSWGStructureFootprint& Footprint, int32 AngleDegrees)
{
	// Literal Core3 StructureManager::getStructureFootprint math: server ignores the SFP chunk sizes.
	const float CenterX = Footprint.CenterCol * 8.f + 4.f;
	const float CenterY = Footprint.CenterRow * 8.f + 4.f;
	const FVector2D Bottom(-CenterX, -CenterY);
	const FVector2D Top(Footprint.Cols * 8.f - CenterX, Footprint.Rows * 8.f - CenterY);
	const float Radians = FMath::DegreesToRadians(static_cast<float>(AngleDegrees));
	const float C = FMath::Cos(Radians);
	const float S = FMath::Sin(Radians);
	const auto Rotate = [C, S](const FVector2D& P) { return FVector2D(P.X * C + P.Y * S, -P.X * S + P.Y * C); };
	const FVector2D A = Rotate(Bottom);
	const FVector2D B = Rotate(Top);
	return FBox2D(A.ComponentMin(B), A.ComponentMax(B));
}

bool FSWGPlacementRules::IsInStructureFootprint(const FBox2D& Rect, const FVector2D& Point)
{
	return Point.X >= Rect.Min.X && Point.X <= Rect.Max.X && Point.Y >= Rect.Min.Y && Point.Y <= Rect.Max.Y;
}

bool FSWGPlacementRules::RectsConflict(const FBox2D& Placing, const FBox2D& Existing)
{
	// Core3 StructureManager::placeStructureFromDeed insets only the existing rect by 0.1 m.
	const FBox2D Inset(Existing.Min + FVector2D(.1, .1), Existing.Max - FVector2D(.1, .1));
	const auto HasCornerInside = [](const FBox2D& Outer, const FBox2D& Inner)
	{
		return FSWGPlacementRules::IsInStructureFootprint(Outer, Inner.Min)
			|| FSWGPlacementRules::IsInStructureFootprint(Outer, Inner.Max)
			|| FSWGPlacementRules::IsInStructureFootprint(Outer, FVector2D(Inner.Min.X, Inner.Max.Y))
			|| FSWGPlacementRules::IsInStructureFootprint(Outer, FVector2D(Inner.Max.X, Inner.Min.Y));
	};
	return HasCornerInside(Inset, Placing) || HasCornerInside(Placing, Inset)
		|| (Placing.Min == Inset.Min && Placing.Max == Inset.Max);
}
