#include "Misc/AutomationTest.h"

#include "Subsystems/SWGCombatAudioSubsystem.h"
#include "Subsystems/SWGCombatMusicSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "SWGAudioTypes.h"
#include "Engine/GameInstance.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGClientEffectReader.h"
#include "TRE/SWGCombatManagerReader.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGGameMusicReader.h"
#include "Network/Messages/Zone/Object/CombatActionIn.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Reads the real game files: combat_manager sound variables, weapon.iff rows, weapon cdfs and game_music_manager. */
namespace
{
	USWGTreSubsystem* LoadTreForCombatAudioTest(FAutomationTestBase& Test)
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

	bool CombatAudioSoundExists(const USWGTreSubsystem& Tre, const FString& Sound)
	{
		return Tre.FileExists(SWGAudio::NormalizeSoundPath(Sound));
	}

	/** True when the .cef reads and every PSND it names is in the TRE. */
	bool EffectPlays(const USWGTreSubsystem& Tre, const FString& Effect, int32& OutSoundCount)
	{
		FSWGClientEffect Parsed;
		if (!FSWGClientEffectReader::Read(Tre.CreateIffReader(SWGAudio::NormalizeEffectPath(Effect)), Parsed))
		{
			return false;
		}
		OutSoundCount = Parsed.SoundPaths.Num();
		for (const FString& Sound : Parsed.SoundPaths)
		{
			if (!CombatAudioSoundExists(Tre, Sound))
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGCombatSoundVariablesTest,
	"SWGEmu.Audio.CombatSoundVariables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGCombatSoundVariablesTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCombatAudioTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	FSWGCombatManagerData Manager;
	if (!TestTrue(TEXT("combat_manager parses"), FSWGCombatManagerReader::ReadCombatManager(Tre->CreateIffReader(TEXT("combat/combat_manager.iff")), Manager)))
	{
		return false;
	}

	// A plain ranged shot: a projectile script that carries no sound variable.
	const FSWGCombatManagerEntry* FireSingle = Manager.FindByName(TEXT("fire_single"));
	if (TestNotNull(TEXT("fire_single entry"), FireSingle))
	{
		const FSWGCombatActionAnimation* Hit = FireSingle->Resolve(static_cast<uint8>(ESWGCombatHit::Hit), 0);
		if (TestNotNull(TEXT("fire_single hit arm"), Hit))
		{
			TestEqual(TEXT("fire_single script"), Hit->PlaybackScript, FString(TEXT("playback/fire_projectiles.pst")));
			TestEqual(TEXT("fire_single TCAA"), Hit->GetVariable(TEXT("TCAA")), FString(TEXT("fire_single")));
			TestTrue(TEXT("a projectile script has no begin sound"), Hit->GetVariable(TEXT("BDNS")).IsEmpty());
		}
	}

	// Every sound variable in the file must name a real .snd.
	int32 WithSounds = 0;
	int32 Missing = 0;
	const FSWGCombatActionAnimation* Flamethrower = nullptr;
	for (const FSWGCombatManagerEntry& Entry : Manager.Entries)
	{
		for (const TPair<uint16, TMap<uint16, FSWGCombatActionAnimation>>& ByHit : Entry.ByHitThenPosture)
		{
			for (const TPair<uint16, FSWGCombatActionAnimation>& ByPosture : ByHit.Value)
			{
				const FSWGCombatActionAnimation& Animation = ByPosture.Value;
				bool bHasSound = false;
				for (const TCHAR* Tag : { TEXT("BDNS"), TEXT("LDNS"), TEXT("EDNS") })
				{
					const FString& Sound = Animation.GetVariable(Tag);
					if (!Sound.IsEmpty())
					{
						bHasSound = true;
						Missing += CombatAudioSoundExists(*Tre, Sound) ? 0 : 1;
						if (Animation.GetVariable(TEXT("BDNS")).EndsWith(TEXT("wep_flamethrower_on.snd")))
						{
							Flamethrower = &Animation;
						}
					}
				}
				WithSounds += bHasSound ? 1 : 0;
			}
		}
	}

	TestTrue(TEXT("entries carry sound variables"), WithSounds > 0);
	TestEqual(TEXT("every SNDB/SNDL/SNDE names an existing .snd"), Missing, 0);
	if (TestNotNull(TEXT("a flamethrower entry"), Flamethrower))
	{
		TestEqual(TEXT("flamethrower loop"), Flamethrower->GetVariable(TEXT("LDNS")), FString(TEXT("sound/wep_flame_thrower.snd")));
		TestEqual(TEXT("flamethrower end"), Flamethrower->GetVariable(TEXT("EDNS")), FString(TEXT("sound/wep_flamethrower_off.snd")));
		TestTrue(TEXT("flamethrower is a particle script"), Flamethrower->PlaybackScript.Contains(TEXT("particle")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGWeaponSoundsTest,
	"SWGEmu.Audio.WeaponSounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGWeaponSoundsTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCombatAudioTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	FSWGDataTableData WeaponTable;
	if (!TestTrue(TEXT("weapon.iff parses"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/weapon/weapon.iff")), WeaponTable)))
	{
		return false;
	}

	// DL-44: weaponEffect "bolt" (inherited), weaponEffectIndex 8 (own) -> the dh17 fire row.
	FSWGWeaponSoundSet Pistol;
	if (TestTrue(TEXT("DL-44 resolves"), USWGCombatAudioSubsystem::ResolveWeaponSounds(*Tre, WeaponTable, TEXT("object/weapon/ranged/pistol/shared_pistol_dl44.iff"), Pistol)))
	{
		TestEqual(TEXT("effect id"), Pistol.EffectId, FString(TEXT("bolt")));
		TestEqual(TEXT("effect index"), Pistol.EffectIndex, 8);
		TestTrue(TEXT("fire effect"), Pistol.FireEffect.EndsWith(TEXT("combat_bls_0_wep_fire_dh17.cef"), ESearchCase::IgnoreCase));

		int32 SoundCount = 0;
		TestTrue(TEXT("fire effect plays"), EffectPlays(*Tre, Pistol.FireEffect, SoundCount));
		TestTrue(TEXT("fire effect has a sound"), SoundCount > 0);
		TestTrue(TEXT("hit effect plays"), EffectPlays(*Tre, USWGCombatAudioSubsystem::PickImpactEffect(Pistol, static_cast<uint8>(ESWGCombatHit::Hit)), SoundCount));
		TestTrue(TEXT("miss effect plays"), EffectPlays(*Tre, USWGCombatAudioSubsystem::PickImpactEffect(Pistol, static_cast<uint8>(ESWGCombatHit::Dodge)), SoundCount));
		TestTrue(TEXT("ricochet effect plays"), EffectPlays(*Tre, USWGCombatAudioSubsystem::PickImpactEffect(Pistol, static_cast<uint8>(ESWGCombatHit::Ricochet)), SoundCount));
		TestTrue(TEXT("a block has no projectile impact"), USWGCombatAudioSubsystem::PickImpactEffect(Pistol, static_cast<uint8>(ESWGCombatHit::Block)).IsEmpty());
		TestTrue(TEXT("a pistol has no swing"), Pistol.SwingEffect.IsEmpty());
	}

	FSWGWeaponSoundSet Rifle;
	int32 SoundCount = 0;
	if (TestTrue(TEXT("rifle resolves"), USWGCombatAudioSubsystem::ResolveWeaponSounds(*Tre, WeaponTable, TEXT("object/weapon/ranged/rifle/shared_rifle_cdef.iff"), Rifle)))
	{
		TestTrue(TEXT("rifle fire effect plays"), EffectPlays(*Tre, Rifle.FireEffect, SoundCount));
	}

	// Melee: swing and hit come from the weapon's cdf CEFT events.
	FSWGWeaponSoundSet Sword;
	if (TestTrue(TEXT("sword resolves"), USWGCombatAudioSubsystem::ResolveWeaponSounds(*Tre, WeaponTable, TEXT("object/weapon/melee/sword/shared_sword_01.iff"), Sword)))
	{
		TestTrue(TEXT("sword swing"), Sword.SwingEffect.EndsWith(TEXT("combat_melee_lrg_sharp_miss.cef"), ESearchCase::IgnoreCase));
		TestTrue(TEXT("sword hit"), Sword.HitEffect.EndsWith(TEXT("combat_melee_lg_blade_hit_flesh.cef"), ESearchCase::IgnoreCase));
		TestTrue(TEXT("sword swing plays"), EffectPlays(*Tre, Sword.SwingEffect, SoundCount));
		TestTrue(TEXT("sword hit plays"), EffectPlays(*Tre, Sword.HitEffect, SoundCount));
	}

	FSWGWeaponSoundSet Knife;
	if (TestTrue(TEXT("knife resolves"), USWGCombatAudioSubsystem::ResolveWeaponSounds(*Tre, WeaponTable, TEXT("object/weapon/melee/knife/shared_knife_dagger.iff"), Knife)))
	{
		TestTrue(TEXT("knife swing"), Knife.SwingEffect.EndsWith(TEXT("combat_melee_swing_knife.cef"), ESearchCase::IgnoreCase));
		TestTrue(TEXT("knife swing plays"), EffectPlays(*Tre, Knife.SwingEffect, SoundCount));
	}

	// Unarmed: the player species cdf names swing and hit.
	FSWGClientDataFile PlayerData;
	if (TestTrue(TEXT("player cdf parses"), FSWGClientDataFileReader::Read(Tre->CreateIffReader(TEXT("clientdata/player/client_shared_player_human_m.cdf")), PlayerData)))
	{
		TestTrue(TEXT("player cdf has swing and hit"), PlayerData.PlayerEvents.ContainsByPredicate([](const FSWGClientDataEvent& Event) { return Event.EventName == TEXT("swing"); })
			|| PlayerData.EffectEvents.ContainsByPredicate([](const FSWGClientDataEvent& Event) { return Event.EventName == TEXT("swing"); }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGGameMusicTest,
	"SWGEmu.Audio.GameMusic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGGameMusicTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCombatAudioTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	FSWGGameMusicData Music;
	if (!TestTrue(TEXT("game_music_manager parses"), FSWGGameMusicReader::Read(Tre->CreateIffReader(TEXT("scene/game_music_manager.iff")), Music)))
	{
		return false;
	}

	TestEqual(TEXT("three enter entries"), Music.CombatEnter.Num(), 3);
	TestEqual(TEXT("three exit entries"), Music.CombatExit.Num(), 3);
	TestEqual(TEXT("three death entries"), Music.CombatDeath.Num(), 3);
	TestEqual(TEXT("three incapacitated entries"), Music.CombatIncapacitated.Num(), 3);
	TestEqual(TEXT("three space battle loops"), Music.SpaceEnter.Num(), 3);
	TestEqual(TEXT("three space victory stings"), Music.SpaceExit.Num(), 3);
	TestEqual(TEXT("one water entry"), Music.Water.Num(), 1);

	// File order is creature, NPC, player; the file names say so.
	const TCHAR* RoleNames[] = { TEXT("_creat_"), TEXT("_npc_"), TEXT("_player_") };
	for (const TArray<FString>* Entries : { &Music.CombatEnter, &Music.CombatExit, &Music.CombatDeath, &Music.CombatIncapacitated })
	{
		for (int32 RoleIndex = 0; RoleIndex < Entries->Num() && RoleIndex < 3; ++RoleIndex)
		{
			TestTrue(*FString::Printf(TEXT("%s is the role %d entry"), *(*Entries)[RoleIndex], RoleIndex), (*Entries)[RoleIndex].Contains(RoleNames[RoleIndex]));
			TestTrue(*FString::Printf(TEXT("%s exists"), *(*Entries)[RoleIndex]), CombatAudioSoundExists(*Tre, (*Entries)[RoleIndex]));
		}
	}
	TestTrue(TEXT("player enter is a loop"), FSWGGameMusicData::ForRole(Music.CombatEnter, ESWGCombatMusicRole::Player).EndsWith(TEXT("_lp.snd")));

	TestEqual(TEXT("player template"), static_cast<int32>(USWGCombatMusicSubsystem::ClassifyTemplate(TEXT("object/creature/player/shared_human_male.iff"))), static_cast<int32>(ESWGCombatMusicRole::Player));
	TestEqual(TEXT("dressed template"), static_cast<int32>(USWGCombatMusicSubsystem::ClassifyTemplate(TEXT("object/mobile/shared_dressed_stormtrooper_m.iff"))), static_cast<int32>(ESWGCombatMusicRole::Npc));
	TestEqual(TEXT("animal template"), static_cast<int32>(USWGCombatMusicSubsystem::ClassifyTemplate(TEXT("object/mobile/shared_bantha.iff"))), static_cast<int32>(ESWGCombatMusicRole::Creature));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
