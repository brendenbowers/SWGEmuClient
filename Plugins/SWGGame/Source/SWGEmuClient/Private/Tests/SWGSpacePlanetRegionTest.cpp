#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Common/SWGTerrainBake.h"
#include "Common/SWGPlanetGlobeBake.h"
#include "Components/SWGSpacePlanetComponent.h"
#include "ImageUtils.h"

#if WITH_DEV_AUTOMATION_TESTS
// The planets that have a playable ground zone, by STAT appearance, and the ground terrain each must map to.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpacePlanetRegionBakeTest, "SWG.Space.PlanetRegionBake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpacePlanetRegionBakeTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>();
	USWGTreSubsystem* Tre = NewObject<USWGTreSubsystem>(Game);
	Tre->AddToRoot(); ON_SCOPE_EXIT { Tre->RemoveFromRoot(); };
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives available"), Tre->LoadArchives())) return false;

	for (const TCHAR* Name : {TEXT("tatooine"), TEXT("naboo"), TEXT("corellia"), TEXT("dantooine"), TEXT("dathomir"), TEXT("endor"), TEXT("lok"), TEXT("yavin4")})
		TestEqual(*FString::Printf(TEXT("planet_%s maps to its ground terrain"), Name),
			FSWGTerrainBake::FindGroundTerrain(*Tre, FString::Printf(TEXT("appearance/planet_%s.pln"), Name)), FString::Printf(TEXT("terrain/%s.trn"), Name));
	for (const TCHAR* Name : {TEXT("tatooine_moon"), TEXT("tatooine_moon_02"), TEXT("naboo_moon"), TEXT("corl_moon_01"), TEXT("gas_endor"), TEXT("yavin")})
		TestTrue(*FString::Printf(TEXT("planet_%s keeps the generic surface"), Name),
			FSWGTerrainBake::FindGroundTerrain(*Tre, FString::Printf(TEXT("appearance/planet_%s.pln"), Name)).IsEmpty());

	auto SaveOpaque = [](const FString& Path, const FSWGImage& Image)
	{
		TArray<FColor> Pixels = Image.Pixels;
		for (FColor& Pixel : Pixels) Pixel.A = 255;
		FImageView View(Pixels.GetData(), Image.Width, Image.Height, ERawImageFormat::BGRA8);
		FImageUtils::SaveImageByExtension(*Path, View);
	};
	const FString CaptureDirectory = FPaths::ProjectDir() / TEXT(".agent_tmp/terrain-quality-captures");
	IFileManager::Get().MakeDirectory(*CaptureDirectory, true);
	FString CacheDirectory = FPaths::ProjectSavedDir() / TEXT("SpacePlanetCache");
	for (const TCHAR* Name : {TEXT("tatooine"), TEXT("naboo"), TEXT("corellia"), TEXT("dantooine"), TEXT("dathomir"), TEXT("endor"), TEXT("lok"), TEXT("yavin4")})
	{
		const FString Terrain = FString::Printf(TEXT("terrain/%s.trn"), Name);
		FSWGTerrainBakeResult Baked;
		if (!TestTrue(*FString::Printf(TEXT("%s bakes"), Name), FSWGTerrainBake::LoadOrBake(*Tre, Terrain, FSWGTerrainBake::DefaultResolution, CacheDirectory, Baked))) continue;
		TestEqual(*FString::Printf(TEXT("%s colour is 1024 square"), Name), Baked.Colour.Width, FSWGTerrainBake::DefaultResolution);
		TestTrue(*FString::Printf(TEXT("%s colour is valid"), Name), Baked.Colour.IsValid() && Baked.Normal.IsValid());
		// A flat grey bake would mean the colour map or shader palette never applied.
		int32 DarkestLuma = 255, BrightestLuma = 0;
		for (const FColor& Pixel : Baked.Colour.Pixels)
		{
			const int32 Luma = (Pixel.R*3 + Pixel.G*6 + Pixel.B) / 10;
			DarkestLuma = FMath::Min(DarkestLuma, Luma); BrightestLuma = FMath::Max(BrightestLuma, Luma);
		}
		TestTrue(*FString::Printf(TEXT("%s colour has contrast (%d..%d)"), Name, DarkestLuma, BrightestLuma), BrightestLuma - DarkestLuma > 20);
		// Stage 10: the detail families (the land's strongest shader families, with their retail textures) and the weight map that blends them.
		TestEqual(*FString::Printf(TEXT("%s detail weight map is half the ground resolution"), Name), Baked.DetailWeights.Width, FSWGTerrainBake::GetDetailResolution(FSWGTerrainBake::DefaultResolution));
		TestTrue(*FString::Printf(TEXT("%s has 1 to 4 detail families (%d)"), Name, Baked.DetailFamilies.Num()), Baked.DetailFamilies.Num() >= 1 && Baked.DetailFamilies.Num() <= FSWGTerrainBake::DetailFamilyCount);
		for (const FSWGTerrainDetailFamily& Family : Baked.DetailFamilies)
			TestTrue(*FString::Printf(TEXT("%s detail family %d texture %s exists"), Name, Family.FamilyId, *Family.DiffusePath), Tre->FileExists(Family.DiffusePath));
		int32 WeightedTexels = 0;
		for (const FColor& Pixel : Baked.DetailWeights.Pixels) WeightedTexels += Pixel.R + Pixel.G + Pixel.B + Pixel.A > 0;
		TestTrue(*FString::Printf(TEXT("%s detail weights are valid and cover its land (%d of %d texels weighted)"), Name, WeightedTexels, Baked.DetailWeights.Pixels.Num()),
			Baked.DetailWeights.IsValid() && WeightedTexels > Baked.DetailWeights.Pixels.Num()/20);
		SaveOpaque(CaptureDirectory / FString::Printf(TEXT("bake-%s-detail-weights.png"), Name), Baked.DetailWeights);
		AddInfo(FString::Printf(TEXT("%s: %s, bake %.2f s, load %.2f s, map %.0f, heights %.0f..%.0f, average (%.2f %.2f %.2f)"), Name, Baked.bFromCache ? TEXT("cache hit") : TEXT("fresh bake"),
			Baked.BakeSeconds, Baked.LoadSeconds, Baked.MapSize, Baked.MinHeight, Baked.MaxHeight, Baked.AverageColour.R, Baked.AverageColour.G, Baked.AverageColour.B));
		SaveOpaque(CaptureDirectory / FString::Printf(TEXT("bake-%s-colour.png"), Name), Baked.Colour);
		FImageView NormalView(Baked.Normal.Pixels.GetData(), Baked.Normal.Width, Baked.Normal.Height, ERawImageFormat::BGRA8);
		FImageUtils::SaveImageByExtension(*(CaptureDirectory / FString::Printf(TEXT("bake-%s-normal.png"), Name)), NormalView);
	}

	// The cache is keyed by the terrain bytes: the next load of Tatooine must come from it.
	FSWGTerrainBakeResult First, Second;
	TestTrue(TEXT("Tatooine loads"), FSWGTerrainBake::LoadOrBake(*Tre, TEXT("terrain/tatooine.trn"), FSWGTerrainBake::DefaultResolution, CacheDirectory, First));
	TestTrue(TEXT("Tatooine loads again"), FSWGTerrainBake::LoadOrBake(*Tre, TEXT("terrain/tatooine.trn"), FSWGTerrainBake::DefaultResolution, CacheDirectory, Second));
	TestTrue(TEXT("the second Tatooine load hits the cache"), Second.bFromCache);
	TestTrue(TEXT("the cache files exist"), IFileManager::Get().FileExists(*(Second.CacheBasePath + TEXT(".png"))) && IFileManager::Get().FileExists(*(Second.CacheBasePath + TEXT(".bin"))));
	TestTrue(TEXT("cached pixels match the bake"), First.Colour.Pixels.Num() == Second.Colour.Pixels.Num() && First.Colour.Pixels == Second.Colour.Pixels);
	TestTrue(TEXT("cached relief matches the bake"), First.Normal.Pixels == Second.Normal.Pixels);
	TestTrue(TEXT("cached detail weights and families match the bake"), First.DetailWeights.Pixels == Second.DetailWeights.Pixels && First.DetailFamilies.Num() == Second.DetailFamilies.Num()
		&& First.DetailFamilies.Num() > 0 && First.DetailFamilies[0].DiffusePath == Second.DetailFamilies[0].DiffusePath && First.DetailFamilies[0].FamilyId == Second.DetailFamilies[0].FamilyId);


	// A lower tier bakes its own cache entry, so switching quality never serves the wrong resolution.
	FSWGTerrainBakeResult Low, LowAgain;
	TestTrue(TEXT("Tatooine bakes at 512"), FSWGTerrainBake::LoadOrBake(*Tre, TEXT("terrain/tatooine.trn"), 512, CacheDirectory, Low));
	TestEqual(TEXT("the 512 bake is 512 square"), Low.Colour.Width, 512);
	TestNotEqual(TEXT("the 512 cache entry differs from the 1024 one"), Low.CacheBasePath, First.CacheBasePath);
	TestTrue(TEXT("the 512 bake loads again from its own cache"), FSWGTerrainBake::LoadOrBake(*Tre, TEXT("terrain/tatooine.trn"), 512, CacheDirectory, LowAgain) && LowAgain.bFromCache && LowAgain.Colour.Width == 512);
	// Stage 10: the top quality tier. Tatooine's ground at 4096 (4 m a pixel) and its 4096 cap globe: timings, the texture memory they need, and the caches.
	{
		auto TextureMegabytes = [](int32 Width, int32 Height) { return double(Width)*Height*4.0*(4.0/3.0)/(1024.0*1024.0); };
		FSWGTerrainBakeResult Top, TopAgain;
		const double TopStart = FPlatformTime::Seconds();
		if (TestTrue(TEXT("Tatooine bakes at the top tier's 4096"), FSWGTerrainBake::LoadOrBake(*Tre, TEXT("terrain/tatooine.trn"), FSWGTerrainBake::MaxResolution, CacheDirectory, Top)))
		{
			const double TopSeconds = FPlatformTime::Seconds() - TopStart;
			TestTrue(TEXT("the 4096 bake has all its maps at full size"), Top.Colour.Width == 4096 && Top.Normal.Width == 4096 && Top.Heights.Num() == 4096*4096 && Top.DetailWeights.Width == 2048);
			TestTrue(TEXT("the 4096 bake has its detail families"), Top.DetailFamilies.Num() >= 1);
			TestNotEqual(TEXT("the 4096 cache entry differs from the 1024 one"), Top.CacheBasePath, First.CacheBasePath);
			TestTrue(TEXT("the 4096 bake loads again from its own cache with identical pixels"), FSWGTerrainBake::LoadOrBake(*Tre, TEXT("terrain/tatooine.trn"), FSWGTerrainBake::MaxResolution, CacheDirectory, TopAgain)
				&& TopAgain.bFromCache && TopAgain.Colour.Pixels == Top.Colour.Pixels && TopAgain.DetailWeights.Pixels == Top.DetailWeights.Pixels && TopAgain.Normal.Pixels == Top.Normal.Pixels);
			AddInfo(FString::Printf(TEXT("Tatooine ground 4096: %s, bake %.2f s, this load %.2f s (%.2f s with the cache write), cache hit load %.2f s"), Top.bFromCache ? TEXT("cache hit") : TEXT("fresh bake"),
				Top.BakeSeconds, Top.LoadSeconds, TopSeconds, TopAgain.LoadSeconds));
			const FSWGTerrainBakeResult& Reference = Top;
			FSWGPlanetGlobeParams TopParams;
			TopParams.CapRadius = FMath::DegreesToRadians(23.f); TopParams.Fill = 1.f; TopParams.CapResolution = 4096; TopParams.Seed = GetTypeHash(FString(TEXT("terrain/tatooine.trn")));
			FSWGPlanetGlobe TopGlobe, TopGlobeAgain;
			if (TestTrue(TEXT("the 4096 cap globe builds"), FSWGPlanetGlobeBake::LoadOrBuild(Reference, TopParams, TopGlobe)))
			{
				TestTrue(TEXT("the 4096 globe has its 4096 cap, 4096 x 2048 whole-globe and 2048 detail maps"), TopGlobe.CapColour.Width == 4096 && TopGlobe.CapNormal.Width == 4096
					&& TopGlobe.FarColour.Width == 4096 && TopGlobe.FarColour.Height == 2048 && TopGlobe.CapDetail.Width == 2048);
				TestTrue(TEXT("the 4096 globe loads again from its own cache with identical pixels"), FSWGPlanetGlobeBake::LoadOrBuild(Reference, TopParams, TopGlobeAgain) && TopGlobeAgain.bFromCache
					&& TopGlobeAgain.CapColour.Pixels == TopGlobe.CapColour.Pixels && TopGlobeAgain.CapDetail.Pixels == TopGlobe.CapDetail.Pixels);
				AddInfo(FString::Printf(TEXT("Tatooine globe cap 4096: %s, build %.2f s, this load %.2f s, cache hit load %.2f s"), TopGlobe.bFromCache ? TEXT("cache hit") : TEXT("fresh build"),
					TopGlobe.BuildSeconds, TopGlobe.LoadSeconds, TopGlobeAgain.LoadSeconds));
				const double CapMb = TextureMegabytes(4096, 4096), FarMb = TextureMegabytes(4096, 2048), DetailMb = TextureMegabytes(2048, 2048);
				AddInfo(FString::Printf(TEXT("texture memory at tier 3 (BGRA8 with mips): cap colour %.0f MB, cap relief %.0f MB, whole globe %.0f MB, detail weights %.0f MB = %.0f MB"), CapMb, CapMb, FarMb, DetailMb, CapMb*2 + FarMb + DetailMb));
				AddInfo(FString::Printf(TEXT("tier 2 (cap 2048, ground 1024): %.0f MB; tiers 0-1 (cap 512): %.0f MB"), TextureMegabytes(2048, 2048)*2 + TextureMegabytes(4096, 2048) + TextureMegabytes(1024, 1024),
					TextureMegabytes(512, 512)*2 + TextureMegabytes(1024, 512) + TextureMegabytes(256, 256)));
			}
		}
	}
	// Stage 9: the globe composed from each planet's ground bake. The cap radius is a typical visible cap (about 23 degrees); the scene test checks the real geometry.
	const float TestCapRadius = FMath::DegreesToRadians(23.f);
	const FString GlobeDirectory = FPaths::ProjectDir() / TEXT(".agent_tmp/terrain-quality-captures");
	IFileManager::Get().MakeDirectory(*GlobeDirectory, true);
	for (const TCHAR* Name : {TEXT("tatooine"), TEXT("naboo"), TEXT("corellia"), TEXT("dantooine"), TEXT("dathomir"), TEXT("endor"), TEXT("lok"), TEXT("yavin4")})
	{
		const FString Terrain = FString::Printf(TEXT("terrain/%s.trn"), Name);
		FSWGTerrainBakeResult Real;
		if (!TestTrue(*FString::Printf(TEXT("%s ground bakes for the globe"), Name), FSWGTerrainBake::LoadOrBake(*Tre, Terrain, FSWGTerrainBake::DefaultResolution, CacheDirectory, Real))) continue;
		FSWGPlanetGlobeParams Params;
		Params.CapRadius = TestCapRadius; Params.Fill = 1.f; Params.CapResolution = 2048; Params.Seed = GetTypeHash(Terrain);
		FSWGPlanetGlobe Globe, GlobeAgain;
		if (!TestTrue(*FString::Printf(TEXT("%s globe builds"), Name), FSWGPlanetGlobeBake::LoadOrBuild(Real, Params, Globe))) continue;
		TestTrue(*FString::Printf(TEXT("%s globe maps have the layout the design says"), Name), Globe.CapColour.Width == 2048 && Globe.CapColour.Height == 2048 && Globe.CapNormal.IsValid()
			&& Globe.FarColour.Width == 4096 && Globe.FarColour.Height == 2048 && Globe.CapColour.IsValid() && Globe.FarColour.IsValid());
		// Stage 10: the cap's detail-weight map is half the cap resolution, carries the ground bake's families and has weighted texels over the playable zone.
		int32 CapWeightedTexels = 0;
		for (const FColor& Pixel : Globe.CapDetail.Pixels) CapWeightedTexels += Pixel.R + Pixel.G + Pixel.B + Pixel.A > 0;
		TestTrue(*FString::Printf(TEXT("%s cap detail weights are valid, %d square and weighted over the land (%d of %d texels)"), Name, Params.GetCapDetailResolution(), CapWeightedTexels, Globe.CapDetail.Pixels.Num()),
			Globe.CapDetail.IsValid() && Globe.CapDetail.Width == Params.GetCapDetailResolution() && CapWeightedTexels > Globe.CapDetail.Pixels.Num()/20 && Globe.DetailFamilies.Num() == Real.DetailFamilies.Num());

		// The cap map covers the visible cap: at 0.9 of its radius, in every direction, the texel is the retail terrain (real weight 1).
		int32 CoveredSamples = 0, SampleCount = 0;
		uint8 LowestWeightInside = 255;
		for (int32 Direction = 0; Direction < 64; ++Direction, ++SampleCount)
		{
			const float Azimuth = Direction*UE_TWO_PI/64.f;
			const uint8 Weight = FSWGPlanetGlobeBake::SampleNearest(Globe.CapNormal, FSWGPlanetGlobeBake::GetCapUv(Params, TestCapRadius*.9f, Azimuth)).A;
			CoveredSamples += Weight >= 250; LowestWeightInside = FMath::Min(LowestWeightInside, Weight);
		}
		TestEqual(*FString::Printf(TEXT("%s: all 64 directions at 0.9x the cap radius are real terrain (lowest weight %d)"), Name, LowestWeightInside), CoveredSamples, SampleCount);
		// Past the playable map's edge the ground is generated: the real weight falls away along the axes at 1.2x the cap radius.
		const uint8 NorthWeight = FSWGPlanetGlobeBake::SampleNearest(Globe.CapNormal, FSWGPlanetGlobeBake::GetCapUv(Params, TestCapRadius*1.22f, 0.f)).A;
		TestTrue(*FString::Printf(TEXT("%s: the cap map's outer ring beyond the playable edge is generated (weight %d)"), Name, NorthWeight), NorthWeight < 128);

		// No NaN-collapsed or empty texels: colour never reads black, relief always points up, alpha flags are present.
		int32 EmptyColour = 0, EmptyFar = 0, BadRelief = 0, WaterTexels = 0;
		for (const FColor& Pixel : Globe.CapColour.Pixels) { EmptyColour += Pixel.R + Pixel.G + Pixel.B < 3; WaterTexels += Pixel.A > 127; }
		for (const FColor& Pixel : Globe.FarColour.Pixels) EmptyFar += Pixel.R + Pixel.G + Pixel.B < 3;
		for (const FColor& Pixel : Globe.CapNormal.Pixels) BadRelief += Pixel.B < 128;
		TestEqual(*FString::Printf(TEXT("%s: no empty cap colour texels"), Name), EmptyColour, 0);
		TestEqual(*FString::Printf(TEXT("%s: no empty whole-globe texels"), Name), EmptyFar, 0);
		TestEqual(*FString::Printf(TEXT("%s: every relief texel points up"), Name), BadRelief, 0);
		const double WaterFraction = double(WaterTexels)/Globe.CapColour.Pixels.Num();
		if (FString(Name) == TEXT("naboo")) TestTrue(*FString::Printf(TEXT("naboo's cap map classifies its lakes and sea as water (%.1f%%)"), WaterFraction*100), WaterFraction > .03 && WaterFraction < .8);
		if (FString(Name) == TEXT("tatooine")) TestTrue(*FString::Printf(TEXT("tatooine's cap map is dry (%.1f%% water)"), WaterFraction*100), WaterFraction < .02);

		// The cap and whole-globe maps come from one field: where they overlap they agree (a flipped axis or shifted frame would not).
		double ColourDifference = 0; int32 Compared = 0;
		for (int32 Ring = 1; Ring <= 4; ++Ring)
			for (int32 Direction = 0; Direction < 32; ++Direction, ++Compared)
			{
				const float Theta = TestCapRadius*Ring*.25f, Azimuth = Direction*UE_TWO_PI/32.f;
				const float Longitude = FMath::Atan2(FMath::Sin(Theta)*FMath::Sin(Azimuth), FMath::Cos(Theta));
				const float Latitude = FMath::Asin(FMath::Sin(Theta)*FMath::Cos(Azimuth));
				const FColor CapPixel = FSWGPlanetGlobeBake::SampleNearest(Globe.CapColour, FSWGPlanetGlobeBake::GetCapUv(Params, Theta, Azimuth));
				const FColor FarPixel = FSWGPlanetGlobeBake::SampleNearest(Globe.FarColour, FSWGPlanetGlobeBake::GetFarUv(Longitude, Latitude));
				ColourDifference += (FMath::Abs(int32(CapPixel.R) - FarPixel.R) + FMath::Abs(int32(CapPixel.G) - FarPixel.G) + FMath::Abs(int32(CapPixel.B) - FarPixel.B))/3.0;
			}
		ColourDifference /= Compared;
		TestTrue(*FString::Printf(TEXT("%s: cap and whole-globe maps agree where they overlap (mean difference %.1f levels)"), Name, ColourDifference), ColourDifference < 14.0);
		// The whole-globe map is seamless where its two edges meet (the antipode meridian).
		// The seam must be no rougher than the map's own texel-to-texel difference next to it.
		double SeamDifference = 0, NeighbourDifference = 0;
		auto ChannelDifference = [](const FColor& First, const FColor& Second) { return (FMath::Abs(int32(First.R) - Second.R) + FMath::Abs(int32(First.G) - Second.G) + FMath::Abs(int32(First.B) - Second.B))/3.0; };
		for (int32 Row = 0; Row < Globe.FarColour.Height; ++Row)
		{
			const int32 RowStart = Row*Globe.FarColour.Width;
			SeamDifference += ChannelDifference(Globe.FarColour.Pixels[RowStart], Globe.FarColour.Pixels[RowStart + Globe.FarColour.Width - 1]);
			NeighbourDifference += ChannelDifference(Globe.FarColour.Pixels[RowStart], Globe.FarColour.Pixels[RowStart + 1]);
		}
		SeamDifference /= Globe.FarColour.Height; NeighbourDifference /= Globe.FarColour.Height;
		TestTrue(*FString::Printf(TEXT("%s: the whole-globe map has no seam at the antipode meridian (edge difference %.1f, texel-to-texel %.1f levels)"), Name, SeamDifference, NeighbourDifference), SeamDifference < NeighbourDifference*1.5 + 2.0);

		// The second load comes from the cache and is identical.
		TestTrue(*FString::Printf(TEXT("%s globe loads again"), Name), FSWGPlanetGlobeBake::LoadOrBuild(Real, Params, GlobeAgain));
		TestTrue(*FString::Printf(TEXT("%s: the second globe load hits the cache"), Name), GlobeAgain.bFromCache);
		TestTrue(*FString::Printf(TEXT("%s: cached globe pixels match the build"), Name), GlobeAgain.CapColour.Pixels == Globe.CapColour.Pixels && GlobeAgain.FarColour.Pixels == Globe.FarColour.Pixels
			&& GlobeAgain.CapNormal.Pixels == Globe.CapNormal.Pixels && GlobeAgain.CapDetail.Pixels == Globe.CapDetail.Pixels && GlobeAgain.DetailFamilies.Num() == Globe.DetailFamilies.Num());
		TestTrue(*FString::Printf(TEXT("%s: the globe cache files exist"), Name), IFileManager::Get().FileExists(*(Globe.CacheBasePath + TEXT(".png"))) && IFileManager::Get().FileExists(*(Globe.CacheBasePath + TEXT("_far.png")))
			&& IFileManager::Get().FileExists(*(Globe.CacheBasePath + TEXT(".bin"))));
		// A different resolution is its own cache entry.
		FSWGPlanetGlobeParams SmallParams = Params; SmallParams.CapResolution = 512;
		TestNotEqual(*FString::Printf(TEXT("%s: another cap resolution has its own cache path"), Name), FSWGPlanetGlobeBake::MakeCacheBasePath(Real.CacheBasePath, SmallParams), Globe.CacheBasePath);
		AddInfo(FString::Printf(TEXT("%s globe: %s, build %.2f s (this load %.2f s), %d of %d cap texels water, cap/far overlap difference %.1f, seam %.1f"), Name,
			GlobeAgain.bFromCache ? TEXT("cached") : TEXT("uncached"), Globe.BuildSeconds, Globe.LoadSeconds, WaterTexels, Globe.CapColour.Pixels.Num(), ColourDifference, SeamDifference));
		SaveOpaque(GlobeDirectory / FString::Printf(TEXT("globe-%s-cap.png"), Name), Globe.CapColour);
		SaveOpaque(GlobeDirectory / FString::Printf(TEXT("globe-%s-far.png"), Name), Globe.FarColour);
		SaveOpaque(GlobeDirectory / FString::Printf(TEXT("globe-%s-cap-relief.png"), Name), Globe.CapNormal);
	}

	FSWGTerrainBakeResult Fallback;
	if (TestTrue(TEXT("the retail overhead map loads as the fallback"), FSWGTerrainBake::LoadFallbackMap(*Tre, TEXT("terrain/tatooine.trn"), Fallback)))
	{
		TestTrue(TEXT("fallback is a valid colour map with flat relief"), Fallback.Colour.IsValid() && Fallback.Normal.Pixels.Num() == 1 && Fallback.bFallbackMap);
		AddInfo(FString::Printf(TEXT("fallback ui_map_tatooine: %dx%d"), Fallback.Colour.Width, Fallback.Colour.Height));
		// The failure path of the globe: the overhead map has no heights and no water classification, and is built uncached.
		FSWGPlanetGlobeParams FallbackParams; FallbackParams.CapResolution = 256; FallbackParams.CapRadius = .4f;
		FSWGPlanetGlobe FallbackGlobe;
		if (TestTrue(TEXT("a globe builds from the fallback map"), FSWGPlanetGlobeBake::LoadOrBuild(Fallback, FallbackParams, FallbackGlobe)))
		{
			TestTrue(TEXT("the fallback globe is uncached and complete"), !FallbackGlobe.bFromCache && FallbackGlobe.CacheBasePath.IsEmpty() && FallbackGlobe.CapColour.IsValid() && FallbackGlobe.FarColour.IsValid());
			int32 WaterTexels = 0, EmptyTexels = 0;
			for (const FColor& Pixel : FallbackGlobe.CapColour.Pixels) { WaterTexels += Pixel.A > 0; EmptyTexels += Pixel.R + Pixel.G + Pixel.B < 3; }
			TestEqual(TEXT("the fallback globe has no water and no empty texels"), WaterTexels + EmptyTexels, 0);
		}
	}
	return true;
}
#endif
