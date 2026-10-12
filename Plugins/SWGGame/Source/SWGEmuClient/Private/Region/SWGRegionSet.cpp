#include "Region/SWGRegionSet.h"

bool FSWGClientRegion::Contains(const FVector2D& RawPosition) const
{
	return FVector2D::DistSquared(RawPosition, Center) < FMath::Square(Radius);
}

bool FSWGRegionSet::LoadFromTable(const FSWGDataTableData& Table)
{
	Regions.Reset();
	if (Table.GetColumnIndex(TEXT("Name")) == INDEX_NONE || Table.GetColumnIndex(TEXT("X")) == INDEX_NONE
		|| Table.GetColumnIndex(TEXT("Z")) == INDEX_NONE || Table.GetColumnIndex(TEXT("Radius")) == INDEX_NONE)
	{
		return false;
	}
	for (int32 Row = 0; Row < Table.Rows.Num(); ++Row)
	{
		FSWGClientRegion& Region = Regions.AddDefaulted_GetRef();
		Region.NameReference = Table.GetCell(Row, TEXT("Name"));
		Region.Center = FVector2D(FCString::Atof(*Table.GetCell(Row, TEXT("X"))), FCString::Atof(*Table.GetCell(Row, TEXT("Z"))));
		Region.Radius = FCString::Atof(*Table.GetCell(Row, TEXT("Radius")));
	}
	return true;
}

int32 FSWGRegionSet::FindContaining(const FVector2D& RawPosition) const
{
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Regions.Num(); ++Index)
	{
		if (Regions[Index].Contains(RawPosition) && (Best == INDEX_NONE || Regions[Index].Radius < Regions[Best].Radius))
		{
			Best = Index;
		}
	}
	return Best;
}

int32 FSWGRegionSet::FindNear(const FVector2D& RawPosition, float Margin) const
{
	int32 Best = INDEX_NONE;
	float BestEdge = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Regions.Num(); ++Index)
	{
		const float Edge = static_cast<float>(FVector2D::Distance(RawPosition, Regions[Index].Center)) - Regions[Index].Radius;
		if (Edge < Margin && Edge < BestEdge)
		{
			BestEdge = Edge;
			Best = Index;
		}
	}
	return Best;
}
