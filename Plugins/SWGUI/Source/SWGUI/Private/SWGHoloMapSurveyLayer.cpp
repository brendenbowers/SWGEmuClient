#include "SWGHoloMapSurveyLayer.h"
#include "SWGHoloMapActor.h"
#include "SWGSurveyStyle.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"

void USWGHoloMapSurveyLayer::OnRegister()
{
	Super::OnRegister();
	if (ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner()))
	{
		Map->OnBakeUpdated.AddUObject(this, &USWGHoloMapSurveyLayer::RebuildSurveyField);
		RebuildSurveyField();
	}
}

void USWGHoloMapSurveyLayer::OnUnregister()
{
	if (ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner())) { Map->OnBakeUpdated.RemoveAll(this); }
	for (UDynamicMeshComponent* Band : SurveyBands) { if (IsValid(Band)) { Band->DestroyComponent(); } }
	SurveyBands.Reset();
	Super::OnUnregister();
}

void USWGHoloMapSurveyLayer::SetSurveyField(const FSWGSurveyResult& Result)
{
	SurveyField = Result;
	RebuildSurveyField();
}

void USWGHoloMapSurveyLayer::RebuildSurveyField()
{
	ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner());
	using namespace UE::Geometry;
	for (UDynamicMeshComponent* Band : SurveyBands)
	{
		Band->DestroyComponent();
	}
	SurveyBands.Reset();
	if ((!Map || !Map->bHasBake) || SurveyField.GridSize < 2 || SurveyField.Range <= 0.f)
	{
		return;
	}
	// The same banding as the survey's own ground hologram, at map scale.
	constexpr int32 Resolution = 25;
	constexpr int32 Bands = 6;
	/** Content units the field floats over the terrain, so it never sinks into it. */
	constexpr float FieldLift = 0.4f;
	const float Step = SurveyField.Range / (Resolution - 1);
	const FVector2D SouthWest(SurveyField.Samples[0].Position.X, SurveyField.Samples[0].Position.Y - SurveyField.Range);
	TArray<FDynamicMesh3> BandData;
	BandData.SetNum(Bands);
	TArray<float> VertexDensity;
	VertexDensity.SetNumUninitialized(Resolution * Resolution);
	for (FDynamicMesh3& Mesh : BandData)
	{
		Mesh.EnableAttributes();
	}
	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			const FVector2D Raw(SouthWest.X + Column * Step, SouthWest.Y + Row * Step);
			VertexDensity[Row * Resolution + Column] = SurveyField.SampleDensity(FVector2D(
				FMath::Min(Raw.X, SouthWest.X + SurveyField.Range - 0.01f), FMath::Min(Raw.Y, SouthWest.Y + SurveyField.Range - 0.01f)));
			const FVector Local = Map->RawToContent(FVector(Raw - Map->BakedCenter, Map->BakedHeightAt(Raw))) + FVector(0.f, 0.f, FieldLift);
			for (FDynamicMesh3& Mesh : BandData)
			{
				Mesh.AppendVertex(FVector3d(Local));
			}
		}
	}
	for (int32 Row = 0; Row + 1 < Resolution; ++Row)
	{
		for (int32 Column = 0; Column + 1 < Resolution; ++Column)
		{
			const int32 SouthWestIndex = Row * Resolution + Column;
			const int32 SouthEastIndex = SouthWestIndex + 1;
			const int32 NorthWestIndex = SouthWestIndex + Resolution;
			const int32 NorthEastIndex = NorthWestIndex + 1;
			const float Mean = 0.25f * (VertexDensity[SouthWestIndex] + VertexDensity[SouthEastIndex] + VertexDensity[NorthWestIndex] + VertexDensity[NorthEastIndex]);
			FDynamicMesh3& Mesh = BandData[FMath::Clamp(FMath::FloorToInt(Mean * Bands), 0, Bands - 1)];
			// Rows step north like the terrain bake, so the same winding faces up.
			Mesh.AppendTriangle(FIndex3i(SouthWestIndex, SouthEastIndex, NorthWestIndex));
			Mesh.AppendTriangle(FIndex3i(SouthEastIndex, NorthEastIndex, NorthWestIndex));
		}
	}
	for (int32 Band = 0; Band < Bands; ++Band)
	{
		if (BandData[Band].TriangleCount() == 0)
		{
			continue;
		}
		FMeshNormals::InitializeOverlayToPerVertexNormals(BandData[Band].Attributes()->PrimaryNormals(), false);
		const float BandDensity = (Band + 0.5f) / Bands;
		UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(this);
		Component->SetupAttachment(Map->ContentRoot);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetMesh(MoveTemp(BandData[Band]));
		Component->SetMaterial(0, Map->MakeHoloMaterial(SWGSurveyStyle::DensityColor(BandDensity), Map->HoloIntensity * FMath::Lerp(0.3f, 2.5f, BandDensity)));
		Component->RegisterComponent();
		SurveyBands.Add(Component);
	}
}

