#include "Misc/AutomationTest.h"

#include "Components/SWGCreatureSoundComponent.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGAnimationReader.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGTerrainReader.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Reads the real game files: .ans MSGS events, cdf event resolution and the terrain surface references. */
namespace
{
	USWGTreSubsystem* LoadTreForCreatureSoundTest(FAutomationTestBase& Test)
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

	const FSWGAnimationEvent* FindEvent(const FSWGAnimationData& Animation, const TCHAR* Name)
	{
		return Animation.Events.FindByPredicate([Name](const FSWGAnimationEvent& Event) { return Event.Name == Name; });
	}

	bool ReadClientData(const USWGTreSubsystem& Tre, const TCHAR* Path, FSWGClientDataFile& OutData)
	{
		return FSWGClientDataFileReader::Read(Tre.CreateIffReader(Path), OutData);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGAnimationEventsTest,
	"SWGEmu.Animation.Events",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGAnimationEventsTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCreatureSoundTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// Rancor sniff (CKAT): two messages, sound_rancor_walk at frames 75 and 225.
	FSWGAnimationData Sniff;
	if (TestTrue(TEXT("rancor sniff parses"), FSWGAnimationReader::ReadAnimation(Tre->CreateIffReader(TEXT("appearance/animation/rancor_emt_stand_sniff.ans")), Sniff)))
	{
		TestEqual(TEXT("sniff message count"), Sniff.Events.Num(), 2);
		const FSWGAnimationEvent* Walk = FindEvent(Sniff, TEXT("sound_rancor_walk"));
		if (TestNotNull(TEXT("sound_rancor_walk"), Walk))
		{
			TestTrue(TEXT("walk frames are 75, 225"), Walk->Frames == TArray<int32>({ 75, 225 }));
		}
		const FSWGAnimationEvent* Emote = FindEvent(Sniff, TEXT("sound_rancor_emote_sniff"));
		if (TestNotNull(TEXT("sound_rancor_emote_sniff"), Emote))
		{
			TestTrue(TEXT("emote frame is 1"), Emote->Frames == TArray<int32>({ 1 }));
		}
	}

	// Humanoid run: footsteps on frames 6 and 17 of a 23-frame cycle.
	FSWGAnimationData Run;
	if (TestTrue(TEXT("all_b_loc_run parses"), FSWGAnimationReader::ReadAnimation(Tre->CreateIffReader(TEXT("appearance/animation/all_b_loc_run.ans")), Run)))
	{
		const FSWGAnimationEvent* Footstep = FindEvent(Run, TEXT("event_footstep"));
		if (TestNotNull(TEXT("event_footstep"), Footstep))
		{
			TestTrue(TEXT("run footsteps are frames 6, 17"), Footstep->Frames == TArray<int32>({ 6, 17 }));
		}
	}

	// Eleven messages, one with 141 frames (a chunk over 255 bytes).
	FSWGAnimationData Fire;
	if (TestTrue(TEXT("jawa prone fire clip parses"), FSWGAnimationReader::ReadAnimation(Tre->CreateIffReader(TEXT("appearance/animation/jawa_cbt_rifle_prone_ready_fire_11.ans")), Fire)))
	{
		TestEqual(TEXT("fire message count"), Fire.Events.Num(), 11);
		const FSWGAnimationEvent* Fire11 = FindEvent(Fire, TEXT("fire11"));
		if (TestNotNull(TEXT("fire11"), Fire11))
		{
			TestEqual(TEXT("fire11 frame count"), Fire11->Frames.Num(), 141);
		}
	}

	// Whole corpus: every clip's events decode with ascending frames inside the clip.
	int32 ClipsWithEvents = 0, EventCount = 0, OutOfRange = 0, Unsorted = 0;
	for (const FString& ClipPath : Tre->FindVirtualPaths(TEXT(".ans")))
	{
		if (!ClipPath.EndsWith(TEXT(".ans")))
		{
			continue;
		}
		FSWGAnimationData Clip;
		if (!FSWGAnimationReader::ReadAnimation(Tre->CreateIffReader(ClipPath), Clip) || Clip.Events.IsEmpty())
		{
			continue;
		}
		++ClipsWithEvents;
		for (const FSWGAnimationEvent& Event : Clip.Events)
		{
			++EventCount;
			for (int32 FrameIndex = 0; FrameIndex < Event.Frames.Num(); ++FrameIndex)
			{
				OutOfRange += Event.Frames[FrameIndex] > Clip.FrameCount ? 1 : 0;
				Unsorted += FrameIndex > 0 && Event.Frames[FrameIndex] <= Event.Frames[FrameIndex - 1] ? 1 : 0;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("clips with events=%d messages=%d framesPastClipEnd=%d notAscending=%d"), ClipsWithEvents, EventCount, OutOfRange, Unsorted));
	TestTrue(TEXT("most event clips decode"), ClipsWithEvents > 2000);
	TestEqual(TEXT("frames never decrease"), Unsorted, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGCreatureEventResolutionTest,
	"SWGEmu.Audio.CreatureEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGCreatureEventResolutionTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCreatureSoundTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// Creature: CSND entries, no surface variants.
	FSWGClientDataFile Rancor;
	if (TestTrue(TEXT("rancor_youth cdf parses"), ReadClientData(*Tre, TEXT("clientdata/creature/client_shared_cr_rancor_youth.cdf"), Rancor)))
	{
		TArray<FString> Targets;
		USWGCreatureSoundComponent::CollectEventTargets(Rancor, TEXT("event_vocalize"), FString(), Targets);
		TestEqual(TEXT("vocalize target count"), Targets.Num(), 1);
		if (!Targets.IsEmpty())
		{
			TestEqual(TEXT("vocalize target"), Targets[0], FString(TEXT("sound/cr_rancor_youth_vocalize.snd")));
		}

		Targets.Reset();
		USWGCreatureSoundComponent::CollectEventTargets(Rancor, TEXT("event_footstep"), TEXT("grass"), Targets);
		TestTrue(TEXT("footstep falls back to the creature walk sound"), Targets == TArray<FString>({ TEXT("sound/cr_rancor_youth_walk.snd") }));

		Targets.Reset();
		USWGCreatureSoundComponent::CollectEventTargets(Rancor, TEXT("fire3"), FString(), Targets);
		TestTrue(TEXT("fireN is not a cdf lookup"), Targets.IsEmpty());
		TestTrue(TEXT("fire3 is a fire event"), USWGCreatureSoundComponent::IsFireEvent(TEXT("fire3")));
		TestTrue(TEXT("fire_9 is a fire event"), USWGCreatureSoundComponent::IsFireEvent(TEXT("fire_9")));
		TestFalse(TEXT("firefly is not"), USWGCreatureSoundComponent::IsFireEvent(TEXT("firefly")));

		TestEqual(TEXT("one ASND loop"), Rancor.AttachedSounds.Num(), 1);
	}

	// sound_<name> goes straight to a .snd; retail files carry a cr_ prefix the animation name drops.
	TArray<FString> DirectPaths;
	USWGCreatureSoundComponent::CollectDirectSoundPaths(TEXT("sound_rancor_walk"), DirectPaths);
	TestEqual(TEXT("direct candidates"), DirectPaths.Num(), 2);
	TestTrue(TEXT("sound/cr_rancor_walk.snd exists"), Tre->FileExists(TEXT("sound/cr_rancor_walk.snd")));
	TestTrue(TEXT("a candidate is the cr_ form"), DirectPaths.Contains(TEXT("sound/cr_rancor_walk.snd")));

	// Player species: EVNT footsteps per surface.
	FSWGClientDataFile Human;
	if (TestTrue(TEXT("human_m cdf parses"), ReadClientData(*Tre, TEXT("clientdata/player/client_shared_player_human_m.cdf"), Human)))
	{
		const TPair<const TCHAR*, const TCHAR*> Expected[] = {
			{ TEXT("stone"), TEXT("sound/fs_in_stone_floor.snd") },
			{ TEXT("grass"), TEXT("sound/fs_out_grass_bare.snd") },
			{ TEXT("sand"), TEXT("sound/fs_out_sand_crunch.snd") },
			{ TEXT("wood"), TEXT("sound/fs_in_wood_floor.snd") },
			{ TEXT("carpet"), TEXT("sound/fs_in_carpet_crunch.snd") },
		};
		for (const TPair<const TCHAR*, const TCHAR*>& Case : Expected)
		{
			TArray<FString> Targets;
			USWGCreatureSoundComponent::CollectEventTargets(Human, TEXT("event_footstep"), Case.Key, Targets);
			TestTrue(*FString::Printf(TEXT("footstep on %s"), Case.Key), Targets == TArray<FString>({ Case.Value }));
		}

		TArray<FString> Targets;
		USWGCreatureSoundComponent::CollectEventTargets(Human, TEXT("event_footstep"), FString(), Targets);
		TestTrue(TEXT("footstep with no surface is the generic effect"), Targets == TArray<FString>({ TEXT("clienteffect/e3_player_footstep.cef") }));
		TestTrue(TEXT("and it is a clienteffect"), USWGCreatureSoundComponent::IsEffectPath(Targets[0]));

		Targets.Reset();
		USWGCreatureSoundComponent::CollectEventTargets(Human, TEXT("event_footstep"), TEXT("lava"), Targets);
		TestEqual(TEXT("unknown surface falls back to the generic effect"), Targets.Num(), 1);

		Targets.Reset();
		USWGCreatureSoundComponent::CollectEventTargets(Human, TEXT("event_cough"), FString(), Targets);
		TestTrue(TEXT("emote"), Targets == TArray<FString>({ TEXT("voice/sound/voice_pl_hum_m_cough.snd") }));
	}

	// Every playable species covers all nine terrain surfaces.
	static const TCHAR* const TerrainSurfaces[] = { TEXT("grass"), TEXT("harddirt"), TEXT("mud"), TEXT("rock"), TEXT("sand"), TEXT("snow"), TEXT("softdirt"), TEXT("stone"), TEXT("surf") };
	int32 SpeciesChecked = 0;
	for (const FString& Path : Tre->FindVirtualPaths(TEXT("clientdata/player/client_shared_player_")))
	{
		FSWGClientDataFile Species;
		if (!Path.EndsWith(TEXT(".cdf")) || !ReadClientData(*Tre, *Path, Species) || Species.PlayerEvents.IsEmpty())
		{
			continue;
		}
		++SpeciesChecked;
		for (const TCHAR* Surface : TerrainSurfaces)
		{
			TArray<FString> Targets;
			USWGCreatureSoundComponent::CollectEventTargets(Species, TEXT("event_footstep"), Surface, Targets);
			if (Targets.IsEmpty())
			{
				AddWarning(FString::Printf(TEXT("%s has no footstep for %s"), *Path, Surface));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d player species cdf(s) checked"), SpeciesChecked));
	TestTrue(TEXT("species cdfs found"), SpeciesChecked > 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGTerrainSurfaceFamiliesTest,
	"SWGEmu.Audio.TerrainSurfaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGTerrainSurfaceFamiliesTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCreatureSoundTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	FSWGTerrainData Tatooine;
	if (!TestTrue(TEXT("tatooine.trn parses"), FSWGTerrainReader::ReadTerrain(Tre->CreateIffReader(TEXT("terrain/tatooine.trn")), Tatooine)))
	{
		return false;
	}

	int32 WithSurface = 0;
	for (const FSWGShaderFamily& Family : Tatooine.ShaderFamilies)
	{
		if (Family.SurfaceFileName.IsEmpty())
		{
			continue;
		}
		++WithSurface;
		TestTrue(*FString::Printf(TEXT("%s -> %s exists"), *Family.Name, *Family.SurfaceFileName), Tre->FileExists(Family.SurfaceFileName));
		TestTrue(TEXT("surface file lives in abstract/terrain_surface"), Family.SurfaceFileName.StartsWith(TEXT("abstract/terrain_surface/")));
	}
	AddInfo(FString::Printf(TEXT("%d of %d tatooine shader families name a surface"), WithSurface, Tatooine.ShaderFamilies.Num()));
	TestTrue(TEXT("families carry surfaces"), WithSurface > 0);
	return true;
}

#endif
