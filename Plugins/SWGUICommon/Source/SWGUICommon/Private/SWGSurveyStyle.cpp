#include "SWGSurveyStyle.h"
#include "Subsystems/SWGSurveySubsystem.h"

FLinearColor SWGSurveyStyle::DensityColor(float Density)
{
	static const FLinearColor Stops[] = {
		FLinearColor::FromSRGBColor(FColor(0x03, 0x2A, 0x6B, 0x40)),
		FLinearColor::FromSRGBColor(FColor(0x00, 0xD6, 0xFB, 0x90)),
		FLinearColor::FromSRGBColor(FColor(0xF4, 0xF1, 0x2A, 0xC0)),
		FLinearColor::FromSRGBColor(FColor(0xFF, 0x4A, 0x17, 0xE0)) };
	constexpr int32 LastStop = UE_ARRAY_COUNT(Stops) - 1;
	const float Scaled = FMath::Clamp(Density, 0.f, 1.f) * LastStop;
	const int32 Lower = FMath::Min(FMath::FloorToInt(Scaled), LastStop - 1);
	return FMath::Lerp(Stops[Lower], Stops[Lower + 1], Scaled - Lower);
}

FText SWGSurveyStyle::DensityText(float Density)
{
	return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(FMath::Clamp(Density, 0.f, 1.f) * 100.f)));
}

TArray<FSWGMapMarker> SWGSurveyStyle::MakeMarkers(const FSWGSurveyResult& Result, bool bLabelEveryPoint)
{
	TArray<FSWGMapMarker> Markers;
	for (int32 Index = 0; Index < Result.Samples.Num(); ++Index)
	{
		const FSWGSurveySample& Sample = Result.Samples[Index];
		const bool bBest = Index == Result.BestIndex && Sample.Density >= FSWGSurveyResult::WaypointDensity;
		FSWGMapMarker& Marker = Markers.AddDefaulted_GetRef();
		Marker.Id = FName(TEXT("Sample"), Index + 1);
		Marker.Position = Sample.Position;
		if (bLabelEveryPoint || bBest)
		{
			Marker.Label = DensityText(Sample.Density);
		}
		FLinearColor Color = DensityColor(FMath::Max(Sample.Density, 0.35f));
		Color.A = 1.f;
		Marker.LabelColor = Color;
		Marker.bCustomPinColor = true;
		Marker.PinColor = Color;
		Marker.Style = bBest ? BestStyle : NAME_None;
	}
	return Markers;
}

FSWGMapGroundOverlay SWGSurveyStyle::MakeOverlay(const FSWGSurveyResult& Result, int32 Resolution, float Opacity)
{
	FSWGMapGroundOverlay Overlay;
	if (Result.GridSize < 2 || Result.Range <= 0.f || Resolution < 2)
	{
		return Overlay;
	}
	const FVector2D NorthWest = Result.Samples[0].Position;
	Overlay.Min = FVector2D(NorthWest.X, NorthWest.Y - Result.Range);
	Overlay.Max = FVector2D(NorthWest.X + Result.Range, NorthWest.Y);
	Overlay.Columns = Resolution;
	Overlay.Rows = Resolution;
	Overlay.OutlineColor = FLinearColor::FromSRGBColor(FColor(0x1C, 0xFF, 0xFF, 0xC0));
	const float Step = Result.Range / (Resolution - 1);
	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			// A hair inside the edge so SampleDensity doesn't read the far border as outside.
			const FVector2D Raw(FMath::Min(Overlay.Min.X + Column * Step, Overlay.Max.X - 0.01f),
				FMath::Max(Overlay.Max.Y - Row * Step, Overlay.Min.Y + 0.01f));
			FLinearColor Color = DensityColor(Result.SampleDensity(Raw));
			Color.A *= Opacity;
			Overlay.Colors.Add(Color);
		}
	}
	return Overlay;
}
