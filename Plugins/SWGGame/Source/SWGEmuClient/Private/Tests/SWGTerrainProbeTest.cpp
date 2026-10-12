#include "Misc/AutomationTest.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "TRE/SWGTerrainFloraPlacer.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Diagnostic, not a pass/fail test: prints a height grid around a raw
 * position so a terrain artefact seen in game can be compared with the
 * evaluator's output. Parameters: "<terrain> <x> <y> [half-extent] [step]".
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FSWGTerrainProbeTest,
	"SWGEmu.Tre.Terrain.Probe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FSWGTerrainProbeTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	// Retail tree positions read off the radar on creature_test (2026-09-18).
	const TCHAR* RetailTrees[] = { TEXT("212 311"), TEXT("104 365"), TEXT("81 332"), TEXT("37 238"), TEXT("-40 233"), TEXT("-60 64"), TEXT("-56 -67"), TEXT("-114 -85"), TEXT("-178 -55"), TEXT("-229 -74") };
	for (const TCHAR* Tree : RetailTrees)
	{
		OutBeautifiedNames.Add(FString::Printf(TEXT("creature_test retail tree %s"), Tree));
		OutTestCommands.Add(FString::Printf(TEXT("terrain/creature_test.trn %s 12 4"), Tree));
	}
}

bool FSWGTerrainProbeTest::RunTest(const FString& Parameters)
{
	TArray<FString> Args;
	Parameters.ParseIntoArrayWS(Args);
	if (Args.Num() < 3)
	{
		AddError(TEXT("usage: <terrain> <x> <y> [half-extent] [step]"));
		return false;
	}
	const FString TerrainPath = Args[0];
	const float CenterX = FCString::Atof(*Args[1]);
	const float CenterY = FCString::Atof(*Args[2]);
	const float HalfExtent = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 40.0f;
	const float Step = Args.Num() > 4 ? FCString::Atof(*Args[4]) : 2.0f;

	UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
	USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
	if (!Tre)
	{
		AddError(TEXT("could not construct a TRE subsystem"));
		return false;
	}
	Tre->AddToRoot();
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };
	Tre->LoadConfig();
	if (!Tre->LoadArchives())
	{
		AddError(TEXT("no .tre archives loaded"));
		return false;
	}

	FSWGTerrainData Planet;
	if (!FSWGTerrainReader::ReadTerrain(Tre->CreateIffReader(TerrainPath), Planet))
	{
		AddError(FString::Printf(TEXT("%s failed to parse"), *TerrainPath));
		return false;
	}

	// Rows run north to south so the print reads like a map; each cell is the height in metres.
	AddInfo(FString::Printf(TEXT("%s heights around (%.0f, %.0f), %.0f m step, rows north->south, columns west->east"), *TerrainPath, CenterX, CenterY, Step));
	float MinHeight = TNumericLimits<float>::Max(), MaxHeight = TNumericLimits<float>::Lowest();
	for (float Y = CenterY + HalfExtent; Y >= CenterY - HalfExtent; Y -= Step)
	{
		FString Row = FString::Printf(TEXT("y=%6.0f |"), Y);
		for (float X = CenterX - HalfExtent; X <= CenterX + HalfExtent; X += Step)
		{
			const float Height = FSWGTerrainEvaluator::GetHeight(Planet, X, Y);
			MinHeight = FMath::Min(MinHeight, Height);
			MaxHeight = FMath::Max(MaxHeight, Height);
			Row += FString::Printf(TEXT("%6.1f"), Height);
		}
		AddInfo(Row);
	}
	AddInfo(FString::Printf(TEXT("range %.2f .. %.2f"), MinHeight, MaxHeight));

	// Collidable flora our placer puts within the same extent, nearest first.
	const float CellSize = FSWGTerrainFloraPlacer::GetCellSize(Planet, ESWGTerrainFloraTier::Collidable);
	const FIntPoint MinCell = FSWGTerrainFloraPlacer::CellCoordAt(CellSize, FVector2D(CenterX - HalfExtent, CenterY - HalfExtent));
	const FIntPoint MaxCell = FSWGTerrainFloraPlacer::CellCoordAt(CellSize, FVector2D(CenterX + HalfExtent, CenterY + HalfExtent));
	TArray<FSWGTerrainFloraInstance> Instances;
	for (int32 CellY = MinCell.Y; CellY <= MaxCell.Y; ++CellY)
	{
		for (int32 CellX = MinCell.X; CellX <= MaxCell.X; ++CellX)
		{
			FSWGTerrainFloraPlacer::PlaceCell(Planet, {}, ESWGTerrainFloraTier::Collidable, FIntPoint(CellX, CellY), 1.0f, [](const FVector2D&) { return false; }, Instances);
		}
	}
	Instances.Sort([CenterX, CenterY](const FSWGTerrainFloraInstance& Left, const FSWGTerrainFloraInstance& Right)
		{
			return FVector2D::DistSquared(FVector2D(Left.RawPosition), FVector2D(CenterX, CenterY)) < FVector2D::DistSquared(FVector2D(Right.RawPosition), FVector2D(CenterX, CenterY));
		});
	for (int32 Index = 0; Index < FMath::Min(Instances.Num(), 6); ++Index)
	{
		const FSWGTerrainFloraInstance& Instance = Instances[Index];
		AddInfo(FString::Printf(TEXT("tree at (%.1f, %.1f) — offset from centre (%+.1f, %+.1f), %.1f m"),
			Instance.RawPosition.X, Instance.RawPosition.Y, Instance.RawPosition.X - CenterX, Instance.RawPosition.Y - CenterY,
			FVector2D::Distance(FVector2D(Instance.RawPosition), FVector2D(CenterX, CenterY))));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
