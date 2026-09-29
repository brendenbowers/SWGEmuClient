#include "SWGHoloSurveyFieldActor.h"
#include "SWGSurveyStyle.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Common/SWGWorldScale.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "TRE/SWGTerrainReader.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float PillarRadius = 14.f;
	constexpr float LabelSize = 28.f;
	constexpr float BestBeamHeight = 180.f;
	constexpr float PingSeconds = 2.2f;
	/** Softness of the reveal edge, world units. */
	constexpr float RevealEdge = 600.f;
}

ASWGHoloSurveyFieldActor::ASWGHoloSurveyFieldActor()
{
	bHasProjector = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FieldFinder(TEXT("/SWGUI/Materials/M_SWGSurveyField.M_SWGSurveyField"));
	FieldPaintMaterial = FieldFinder.Object;
}

float ASWGHoloSurveyFieldActor::RangeUnits() const
{
	return SWGToUnrealSpace(FMath::Max(Result.Range, 1.f));
}

void ASWGHoloSurveyFieldActor::SetResult(const FSWGSurveyResult& InResult)
{
	Result = InResult;
	const UGameInstance* GameInstance = GetGameInstance();
	const USWGTerrainSubsystem* Terrain = GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	if (!Terrain || !Terrain->GetPlanetData() || Result.GridSize < 2 || Result.Range <= 0.f)
	{
		return;
	}
	const int32 Generation = ++BakeGeneration;
	TArray<FVector2D> SamplePositions;
	for (const FSWGSurveySample& Sample : Result.Samples)
	{
		SamplePositions.Add(Sample.Position);
	}
	const FVector2D Center = Result.Center;
	const FVector2D SouthWest(Result.Samples[0].Position.X, Result.Samples[0].Position.Y - Result.Range);
	const float Range = Result.Range;
	const int32 Resolution = FMath::Max(FieldResolution, 2);
	TWeakObjectPtr<ASWGHoloSurveyFieldActor> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, Generation, Center, SouthWest, Range, Resolution, SamplePositions,
		Planet = Terrain->GetPlanetData(), Edits = Terrain->GetPublishedEditLayers()]()
	{
		const TArrayView<const FSWGTerrainLayer> EditView = Edits ? MakeArrayView(*Edits) : TArrayView<const FSWGTerrainLayer>();
		auto HeightAt = [&](float X, float Y)
		{
			const float Height = FSWGTerrainEvaluator::GetHeight(*Planet, X, Y, EditView);
			// Over the sea the field floats on the water, as the holo map does.
			return Planet->Header.bUseGlobalWaterTable ? FMath::Max(Height, Planet->Header.GlobalWaterTableHeight) : Height;
		};
		TSharedPtr<FFieldBake> Bake = MakeShared<FFieldBake>();
		Bake->FieldHeights.SetNumUninitialized(Resolution * Resolution);
		const float Step = Range / (Resolution - 1);
		ParallelFor(Resolution, [&](int32 Row)
		{
			for (int32 Column = 0; Column < Resolution; ++Column)
			{
				Bake->FieldHeights[Row * Resolution + Column] = HeightAt(SouthWest.X + Column * Step, SouthWest.Y + Row * Step);
			}
		});
		for (const FVector2D& Position : SamplePositions)
		{
			Bake->SampleHeights.Add(HeightAt(Position.X, Position.Y));
		}
		Bake->CenterHeight = HeightAt(Center.X, Center.Y);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Bake]()
		{
			ASWGHoloSurveyFieldActor* Actor = WeakThis.Get();
			if (Actor && Actor->BakeGeneration == Generation)
			{
				Actor->ApplyBake(*Bake);
			}
		});
	});
}

void ASWGHoloSurveyFieldActor::ClearField()
{
	for (UObject* Object : FieldObjects)
	{
		if (UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			Component->DestroyComponent();
		}
	}
	for (UDynamicMeshComponent* Band : BandMeshes)
	{
		Band->DestroyComponent();
	}
	FieldObjects.Reset();
	FieldMaterials.Reset();
	FieldPaintInstance = nullptr;
	BandMeshes.Reset();
	Pillars.Reset();
	BestBeam = nullptr;
	BestPing = nullptr;
	BestPingMaterial = nullptr;
	bBuilt = false;
	bHasBest = false;
}

void ASWGHoloSurveyFieldActor::ApplyBake(const FFieldBake& Bake)
{
	using namespace UE::Geometry;
	ClearField();
	// The actor stands on the ground at the survey's centre; everything is laid out from there.
	SetActorLocation(SWGToUnrealSpace(FVector(Result.Center.X, Result.Center.Y, Bake.CenterHeight)));
	auto ToLocal = [this, &Bake](const FVector2D& Raw, float Height)
	{
		return SWGToUnrealSpace(FVector(Raw.X - Result.Center.X, Raw.Y - Result.Center.Y, Height - Bake.CenterHeight));
	};

	// One terrain mesh with interpolated vertex colours paints a continuous field.
	const int32 Resolution = FMath::Max(FieldResolution, 2);
	const float Step = Result.Range / (Resolution - 1);
	const FVector2D SouthWest(Result.Samples[0].Position.X, Result.Samples[0].Position.Y - Result.Range);
	const FVector2D CornerRaw[] = {
		SouthWest, SouthWest + FVector2D(Result.Range, 0.f),
		SouthWest + FVector2D(Result.Range, Result.Range), SouthWest + FVector2D(0.f, Result.Range) };
	const int32 CornerIndexes[] = { 0, Resolution - 1, Resolution * Resolution - 1, Resolution * (Resolution - 1) };
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		CornerGround[Corner] = ToLocal(CornerRaw[Corner], Bake.FieldHeights[CornerIndexes[Corner]] + FieldLiftMetres);
	}
	FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();
	Mesh.Attributes()->EnablePrimaryColors();
	FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();
	TArray<int32> ColorIds;
	ColorIds.SetNumUninitialized(Resolution * Resolution);
	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			const int32 Index = Row * Resolution + Column;
			const FVector2D Raw(SouthWest.X + Column * Step, SouthWest.Y + Row * Step);
			// A hair inside the square so the far edges don't read as outside the grid.
			const float Density = Result.SampleDensity(FVector2D(FMath::Min(Raw.X, SouthWest.X + Result.Range - 0.01f),
				FMath::Min(Raw.Y, SouthWest.Y + Result.Range - 0.01f)));
			const FVector Local = ToLocal(Raw, Bake.FieldHeights[Index] + FieldLiftMetres);
			Mesh.AppendVertex(FVector3d(Local));
			const FLinearColor Color = SWGSurveyStyle::DensityColor(Density) * FMath::Lerp(0.08f, 0.9f, Density * Density);
			ColorIds[Index] = Colors->AppendElement(FVector4f(Color.R, Color.G, Color.B, 1.f));
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
			// Rows step north (UE +X) as in the holo map's terrain, so the same winding faces up.
			for (const FIndex3i& Triangle : { FIndex3i(SouthWestIndex, SouthEastIndex, NorthWestIndex), FIndex3i(SouthEastIndex, NorthEastIndex, NorthWestIndex) })
			{
				const int32 TriangleIndex = Mesh.AppendTriangle(Triangle);
				Colors->SetTriangle(TriangleIndex, FIndex3i(ColorIds[Triangle.A], ColorIds[Triangle.B], ColorIds[Triangle.C]));
			}
		}
	}
	FMeshNormals::InitializeOverlayToPerVertexNormals(Mesh.Attributes()->PrimaryNormals(), false);
	if (FieldPaintMaterial)
	{
		UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(this);
		Component->SetupAttachment(GetRootComponent());
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetMesh(MoveTemp(Mesh));
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(FieldPaintMaterial, this);
		Material->SetScalarParameterValue(TEXT("Intensity"), HoloIntensity * 0.10f);
		Component->SetMaterial(0, Material);
		Component->RegisterComponent();
		BandMeshes.Add(Component);
		FieldMaterials.Add(Material);
		FieldPaintInstance = Material;
	}

	// A pillar per grid point, its height its concentration, the percentage on top.
	const float RangeWorld = RangeUnits();
	for (int32 Index = 0; Index < Result.Samples.Num(); ++Index)
	{
		const FSWGSurveySample& Sample = Result.Samples[Index];
		FPillar& Pillar = Pillars.AddDefaulted_GetRef();
		Pillar.Ground = ToLocal(Sample.Position, Bake.SampleHeights[Index] + FieldLiftMetres);
		Pillar.Height = FMath::Max(RangeWorld * PillarHeightPerRange * Sample.Density, PillarRadius * 2.f);
		Pillar.DistanceFromCenter = FVector2D(Pillar.Ground).Size();
		const FLinearColor Color = SWGSurveyStyle::DensityColor(FMath::Max(Sample.Density, 0.2f));

		Pillar.Beam = NewObject<UStaticMeshComponent>(this);
		Pillar.Beam->SetStaticMesh(BeamMesh);
		Pillar.Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Pillar.Beam->SetCastShadow(false);
		Pillar.Beam->SetDepthPriorityGroup(SDPG_Foreground);
		UMaterialInstanceDynamic* Material = MakeHoloMaterial(Color, HoloIntensity * 0.7f);
		Pillar.Beam->SetMaterial(0, Material);
		Pillar.Beam->SetupAttachment(GetRootComponent());
		Pillar.Beam->RegisterComponent();
		FieldObjects.Add(Pillar.Beam);
		FieldMaterials.Add(Material);

		Pillar.Label = NewObject<UTextRenderComponent>(this);
		if (UMaterialInterface* TextMaterial = GetHoloTextMaterial())
		{
			Pillar.Label->SetTextMaterial(TextMaterial);
		}
		Pillar.Label->SetText(SWGSurveyStyle::DensityText(Sample.Density));
		FLinearColor LabelColor = Color;
		LabelColor.A = 1.f;
		Pillar.Label->SetTextRenderColor(LabelColor.ToFColor(true));
		Pillar.Label->SetWorldSize(LabelSize * (Index == Result.BestIndex ? 1.25f : 1.f));
		Pillar.Label->SetHorizontalAlignment(EHTA_Center);
		Pillar.Label->SetVerticalAlignment(EVRTA_TextBottom);
		Pillar.Label->SetDepthPriorityGroup(SDPG_Foreground);
		Pillar.Label->SetupAttachment(GetRootComponent());
		Pillar.Label->RegisterComponent();
		FieldObjects.Add(Pillar.Label);
	}

	bHasBest = Result.IsValid() && Result.GetBest().Density >= FSWGSurveyResult::WaypointDensity;
	if (bHasBest)
	{
		BestGround = Pillars[Result.BestIndex].Ground;
		const FLinearColor BestColor = SWGSurveyStyle::DensityColor(1.f);
		BestBeam = NewObject<UStaticMeshComponent>(this);
		BestBeam->SetStaticMesh(BeamMesh);
		BestBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BestBeam->SetCastShadow(false);
		BestBeam->SetDepthPriorityGroup(SDPG_Foreground);
		UMaterialInstanceDynamic* BeamMaterial = MakeHoloMaterial(BestColor, HoloIntensity * 1.2f);
		BestBeam->SetMaterial(0, BeamMaterial);
		BestBeam->SetupAttachment(GetRootComponent());
		BestBeam->RegisterComponent();
		FieldObjects.Add(BestBeam);
		FieldMaterials.Add(BeamMaterial);

		BestPing = NewObject<UStaticMeshComponent>(this);
		BestPing->SetStaticMesh(BeamMesh);
		BestPing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BestPing->SetCastShadow(false);
		BestPingMaterial = MakeHoloMaterial(BestColor, HoloIntensity);
		BestPing->SetMaterial(0, BestPingMaterial);
		BestPing->SetupAttachment(GetRootComponent());
		BestPing->RegisterComponent();
		FieldObjects.Add(BestPing);
		FieldMaterials.Add(BestPingMaterial);
	}
	bBuilt = true;
	RevealTime = 0.f;
}

void ASWGHoloSurveyFieldActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		return;
	}
	RevealTime += DeltaSeconds;
	if (FieldPaintInstance)
	{
		const UGameInstance* GameInstance = GetGameInstance();
		const USWGTerrainSubsystem* Terrain = GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
		const float Daylight = Terrain ? FMath::Max(0.f, FMath::Sin(2.f * PI * Terrain->GetDayFraction())) : 1.f;
		FieldPaintInstance->SetScalarParameterValue(TEXT("Intensity"), HoloIntensity * FMath::Lerp(0.10f, 0.34f, Daylight));
	}
	float Fade = 1.f;
	if (FadeOutRemaining >= 0.f)
	{
		FadeOutRemaining = FMath::Max(0.f, FadeOutRemaining - DeltaSeconds);
		Fade = FadeOutSeconds > 0.f ? FadeOutRemaining / FadeOutSeconds : 0.f;
	}

	// The scan sweeps out from the centre: the material hides what lies past its radius.
	const float MaxReach = RangeUnits() * UE_SQRT_2 * 0.5f + RevealEdge;
	const float Reach = MaxReach * (FadeOutRemaining >= 0.f
		? Fade : FMath::Clamp(RevealTime / RevealSeconds, 0.f, 1.f));
	const FVector Centre = GetActorLocation();
	for (UMaterialInstanceDynamic* Material : FieldMaterials)
	{
		Material->SetScalarParameterValue(TEXT("Radius"), FMath::Max(Reach, 1.f));
		Material->SetScalarParameterValue(TEXT("Fade"), FadeAlpha * Fade);
		Material->SetVectorParameterValue(TEXT("Center"), FLinearColor(Centre.X, Centre.Y, Centre.Z, 0.f));
	}

	FVector CameraLocation = Centre;
	FRotator CameraRotation;
	if (const APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	}
	const FTransform& ActorToWorld = GetActorTransform();
	for (int32 Index = 0; Index < Pillars.Num(); ++Index)
	{
		const FPillar& Pillar = Pillars[Index];
		// Each pillar grows up out of the ground once the sweep reaches it.
		const float Grow = FMath::Clamp((Reach - Pillar.DistanceFromCenter) / RevealEdge, 0.f, 1.f) * Fade;
		const float Height = FMath::Max(Pillar.Height * FMath::InterpEaseOut(0.f, 1.f, Grow, 2.f), 1.f);
		Pillar.Beam->SetVisibility(Grow > 0.f);
		Pillar.Beam->SetRelativeLocation(Pillar.Ground + FVector(0.f, 0.f, Height * 0.5f));
		Pillar.Beam->SetRelativeScale3D(FVector(PillarRadius * 2.f / 100.f, PillarRadius * 2.f / 100.f, Height / 100.f));
		Pillar.Label->SetVisibility(Grow > 0.5f);
		Pillar.Label->SetRelativeLocation(Pillar.Ground + FVector(0.f, 0.f, Height + PillarRadius));
		// Face the viewer; text renders along its component's +X.
		const FVector LabelWorld = ActorToWorld.TransformPosition(Pillar.Label->GetRelativeLocation());
		Pillar.Label->SetWorldRotation((CameraLocation - LabelWorld).GetSafeNormal2D().Rotation());
		Pillar.Label->SetWorldSize(FMath::Clamp(FVector::Dist(CameraLocation, LabelWorld) * 0.018f, LabelSize, 55.f)
			* (Index == Result.BestIndex ? 1.25f : 1.f));
	}

	if (bHasBest && BestBeam && BestPing)
	{
		const float BeamHeight = BestBeamHeight;
		const bool bReached = Reach >= FVector2D(BestGround).Size();
		BestBeam->SetVisibility(bReached);
		BestBeam->SetRelativeLocation(BestGround + FVector(0.f, 0.f, BeamHeight * 0.5f));
		BestBeam->SetRelativeScale3D(FVector(PillarRadius * 0.8f / 100.f, PillarRadius * 0.8f / 100.f, BeamHeight / 100.f));
		// A ring of light spreading from the best point, over and over; engine cylinders are 100 units across.
		const float Phase = FMath::Frac(RevealTime / PingSeconds);
		const float PingRadius = 20.f + 100.f * Phase;
		BestPing->SetVisibility(bReached);
		BestPing->SetRelativeLocation(BestGround + FVector(0.f, 0.f, 20.f));
		BestPing->SetRelativeScale3D(FVector(PingRadius / 50.f, PingRadius / 50.f, 0.004f));
		BestPingMaterial->SetScalarParameterValue(TEXT("Intensity"), HoloIntensity * (1.f - Phase));
	}
}

FVector ASWGHoloSurveyFieldActor::GetCornerLocation(int32 Corner) const
{
	return GetActorTransform().TransformPosition(CornerGround[FMath::Clamp(Corner, 0, 3)]);
}

bool ASWGHoloSurveyFieldActor::GetBestLocation(FVector& OutLocation) const
{
	if (!bBuilt || !bHasBest)
	{
		return false;
	}
	OutLocation = GetActorTransform().TransformPosition(BestGround);
	return true;
}

void ASWGHoloSurveyFieldActor::FadeOutAndDestroy(float Seconds)
{
	FadeOutSeconds = FMath::Max(Seconds, 0.01f);
	FadeOutRemaining = FadeOutSeconds;
	SetLifeSpan(FadeOutSeconds + 0.1f);
}
