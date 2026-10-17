#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Misc/ScopeExit.h"
#include "SWGAppearanceBillboardComponent.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGBeamReader.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGSpriteReader.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Sprite / beam appearances and the .prt/.spr parts of .cmp components (retail SpriteAppearance 0x789af0, BeamAppearance 0x792af0). */
namespace
{
	USWGTreSubsystem* LoadAppearanceTre(FAutomationTestBase& Test)
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
		if (!Tre)
		{
			Test.AddError(TEXT("could not construct a TRE subsystem"));
			return nullptr;
		}
		Tre->AddToRoot();
		Tre->LoadConfig();
		if (!Tre->LoadArchives())
		{
			Test.AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGAppearanceFxMathTest,
	"SWGEmu.Fx.AppearanceMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGAppearanceFxMathTest::RunTest(const FString& Parameters)
{
	// The stored radius bounds the square, so the side is sqrt(2) x radius x scale.
	TestEqual(TEXT("sprite side"), USWGAppearanceBillboardComponent::ComputeSpriteSideMeters(0.3257f, 1.f), (float)(0.3257 * UE_SQRT_2), 1e-4f);
	TestEqual(TEXT("sprite side scaled"), USWGAppearanceBillboardComponent::ComputeSpriteSideMeters(2.f, 3.f), (float)(6.0 * UE_SQRT_2), 1e-4f);

	// A beam along +X seen from above and to the side: the strip normal ignores the along-axis part.
	const FVector Normal = USWGAppearanceBillboardComponent::ComputeBeamNormal(FVector::XAxisVector, FVector(500.f, 30.f, 40.f));
	TestEqual(TEXT("normal along axis"), (float)Normal.X, 0.f, 1e-4f);
	TestEqual(TEXT("normal length"), (float)Normal.Size(), 1.f, 1e-4f);
	TestTrue(TEXT("normal toward viewer"), FVector::DotProduct(Normal, FVector(0.f, 30.f, 40.f)) > 0.f);

	// Looking straight down the beam still yields a unit perpendicular.
	const FVector Degenerate = USWGAppearanceBillboardComponent::ComputeBeamNormal(FVector::XAxisVector, FVector(900.f, 0.f, 0.f));
	TestEqual(TEXT("degenerate length"), (float)Degenerate.Size(), 1.f, 1e-4f);
	TestEqual(TEXT("degenerate perpendicular"), (float)FVector::DotProduct(Degenerate, FVector::XAxisVector), 0.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGAppearanceFxDataTest,
	"SWGEmu.Fx.AppearanceData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGAppearanceFxDataTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadAppearanceTre(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// Every .spr parses, its shader exists and the tint is the 1,1,1,1 every shipped file carries.
	TArray<FString> SpritePaths = Tre->FindVirtualPaths(TEXT(".spr"));
	SpritePaths.RemoveAll([](const FString& Path) { return !Path.EndsWith(TEXT(".spr"), ESearchCase::IgnoreCase); });
	int32 Parsed = 0, NonWhite = 0, MissingShader = 0;
	for (const FString& Path : SpritePaths)
	{
		FSWGSprite Sprite;
		if (!FSWGSpriteReader::Read(Tre->CreateIffReader(Path), Sprite))
		{
			AddError(FString::Printf(TEXT("%s does not parse"), *Path));
			continue;
		}
		++Parsed;
		NonWhite += Sprite.Tint.Equals(FLinearColor::White) ? 0 : 1;
		if (!Tre->FileExists(Sprite.GetShaderPath()))
		{
			++MissingShader;
		}
		TestTrue(*FString::Printf(TEXT("%s radius"), *Path), Sprite.BoundsRadius >= 0.f);
	}
	UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.AppearanceData] .spr: %d files, %d parsed, %d with a non-white tint, %d with a missing shader"), SpritePaths.Num(), Parsed, NonWhite, MissingShader);
	TestEqual(TEXT("every .spr parses"), Parsed, SpritePaths.Num());
	TestTrue(TEXT("sprites ship"), SpritePaths.Num() > 400);

	// The four beam files.
	int32 Beams = 0;
	for (const TCHAR* Name : { TEXT("beam_red"), TEXT("beam_green"), TEXT("beam_yellow"), TEXT("beam_template") })
	{
		const FString Path = FString::Printf(TEXT("appearance/%s.iff"), Name);
		FSWGBeam Beam;
		if (!FSWGBeamReader::Read(Tre->CreateIffReader(Path), Beam))
		{
			AddError(FString::Printf(TEXT("%s does not parse"), *Path));
			continue;
		}
		++Beams;
		TestEqual(*FString::Printf(TEXT("%s length"), Name), Beam.Length, 10.f, 1e-3f);
		TestEqual(*FString::Printf(TEXT("%s width"), Name), Beam.Width, 0.3f, 1e-3f);
	}
	TestEqual(TEXT("beams parse"), Beams, 4);

	// .cmp components: laser bolts carry two sprite parts, lamps a particle part, and the part path is kept whole.
	int32 WithEffects = 0, SpriteParts = 0, ParticleParts = 0, ComponentCount = 0;
	TArray<FString> ComponentPaths = Tre->FindVirtualPaths(TEXT(".cmp"));
	for (const FString& Path : ComponentPaths)
	{
		if (!Path.EndsWith(TEXT(".cmp"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		++ComponentCount;
		FSWGMeshData Mesh;
		FSWGMeshReader::ReadComponentMesh(Tre->CreateIffReader(Path), [](const FString&, FSWGMeshData&) { return false; }, Mesh);
		WithEffects += Mesh.EffectParts.IsEmpty() ? 0 : 1;
		for (const FSWGMeshEffectPart& Part : Mesh.EffectParts)
		{
			SpriteParts += Part.Path.EndsWith(TEXT(".spr")) ? 1 : 0;
			ParticleParts += Part.Path.EndsWith(TEXT(".prt")) ? 1 : 0;
			TestTrue(*FString::Printf(TEXT("%s part %s prefixed"), *Path, *Part.Path), Part.Path.StartsWith(TEXT("appearance/")));
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.AppearanceData] .cmp: %d files, %d with effect parts, %d sprite parts, %d particle parts"), ComponentCount, WithEffects, SpriteParts, ParticleParts);

	FSWGMeshData Bolt;
	FSWGMeshReader::ReadComponentMesh(Tre->CreateIffReader(TEXT("appearance/component/cef_laserbolt_red_l0.cmp")), [](const FString&, FSWGMeshData&) { return false; }, Bolt);
	TestEqual(TEXT("laser bolt sprite parts"), Bolt.EffectParts.Num(), 2);
	FSWGMeshData Lamp;
	FSWGMeshReader::ReadComponentMesh(Tre->CreateIffReader(TEXT("appearance/component/frn_all_lamp_desk_s1_lit_l0.cmp")), [](const FString&, FSWGMeshData&) { return false; }, Lamp);
	TestTrue(TEXT("lamp halo particle part"), Lamp.EffectParts.ContainsByPredicate([](const FSWGMeshEffectPart& Part) { return Part.Path.EndsWith(TEXT("pt_light_indoor_glow.prt")); }));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
