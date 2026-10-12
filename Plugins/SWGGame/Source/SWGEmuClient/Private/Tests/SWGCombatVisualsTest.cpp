#include "Misc/AutomationTest.h"

#include "Common/SWGWeaponHardpoints.h"
#include "Common/SWGWorldScale.h"
#include "Components/SWGLightsaberComponent.h"
#include "Components/SWGTrailComponent.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGCombatAudioSubsystem.h"
#include "Subsystems/SWGCombatVisualsSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGCombatManagerReader.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGEffectTables.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGLightsaberReader.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGSwooshReader.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Combat visuals against the real game files: weapon hardpoints, projectile and saber resolution, plus the flight / ribbon / blade maths. */
namespace
{
	USWGTreSubsystem* LoadTreForCombatVisualsTest(FAutomationTestBase& Test)
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

	/** The highest-detail static mesh of an .apt (APT > 0000 > NAME, a .lod whose CHLD names the levels, or the mesh itself). */
	FString FindFirstMeshPath(USWGTreSubsystem& Tre, const FString& AppearancePath)
	{
		FSWGIffReader Reader = Tre.CreateIffReader(AppearancePath);
		FSWGIffChunk AptForm, Form0000, NameChunk;
		if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('A', 'P', 'T', ' '), AptForm)
			|| !Reader.FindChildForm(AptForm, SWG_IFF_TAG('0', '0', '0', '0'), Form0000)
			|| !Reader.FindChildChunk(Form0000, SWGIffTags::Name, NameChunk))
		{
			return FString();
		}

		const FString Target = FString::ConstructFromPtrSize(reinterpret_cast<const ANSICHAR*>(Reader.GetChunkData(NameChunk)), NameChunk.DataSize - 1);
		if (!Target.EndsWith(TEXT(".lod")))
		{
			return Target;
		}

		FSWGIffReader LodReader = Tre.CreateIffReader(Target);
		FSWGIffChunk DtlaForm, Form0007, DataForm;
		if (!LodReader.IsValid() || !LodReader.FindForm(SWG_IFF_TAG('D', 'T', 'L', 'A'), DtlaForm)
			|| !LodReader.FindChildForm(DtlaForm, SWG_IFF_TAG('0', '0', '0', '7'), Form0007)
			|| !LodReader.FindChildForm(Form0007, SWGIffTags::Data, DataForm))
		{
			return FString();
		}
		for (const FSWGIffChunk& Child : LodReader.ReadChildren(DataForm))
		{
			if (Child.Tag == SWG_IFF_TAG('C', 'H', 'L', 'D') && Child.DataSize > 4)
			{
				const uint8* Data = LodReader.GetChunkData(Child);
				const FString MeshPath = TEXT("appearance/") + FString::ConstructFromPtrSize(reinterpret_cast<const ANSICHAR*>(Data + 4), Child.DataSize - 4 - 1);
				if (MeshPath.EndsWith(TEXT("_l0.msh")))
				{
					return MeshPath;
				}
			}
		}
		return FString();
	}

	/** The template's appearance as the mesh generator resolves it: a .lsb stands for its hilt. */
	FString ResolveHiltAppearance(USWGTreSubsystem& Tre, const FString& WeaponTemplate, FString* OutWeaponAppearance = nullptr)
	{
		FString Appearance;
		Tre.FindTemplateFormStringParam(WeaponTemplate, SWG_IFF_TAG('S', 'H', 'O', 'T'), TEXT("appearanceFilename"), Appearance);
		if (OutWeaponAppearance)
		{
			*OutWeaponAppearance = Appearance;
		}
		if (Appearance.EndsWith(TEXT(".lsb")))
		{
			FSWGLightsaber Saber;
			return FSWGLightsaberReader::Read(Tre.CreateIffReader(Appearance), Saber) ? Saber.HiltAppearancePath : FString();
		}
		return Appearance;
	}

	bool ReadHardpoints(USWGTreSubsystem& Tre, const FString& WeaponTemplate, TArray<FSWGMeshHardpoint>& OutHardpoints)
	{
		const FString MeshPath = FindFirstMeshPath(Tre, ResolveHiltAppearance(Tre, WeaponTemplate));
		FSWGMeshData Mesh;
		if (MeshPath.IsEmpty() || !FSWGMeshReader::ReadStaticMesh(Tre.CreateIffReader(MeshPath), Mesh))
		{
			return false;
		}
		OutHardpoints = Mesh.Hardpoints;
		return true;
	}

	const FSWGMeshHardpoint* FindHardpoint(const TArray<FSWGMeshHardpoint>& Hardpoints, FName Name)
	{
		return Hardpoints.FindByPredicate([Name](const FSWGMeshHardpoint& Hardpoint) { return FName(*Hardpoint.Name) == Name; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGWeaponHardpointsTest,
	"SWGEmu.Fx.WeaponHardpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGWeaponHardpointsTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCombatVisualsTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// Guns carry a muzzle that points forward (SWG +z is UE +X) and sits at the barrel's end.
	for (const TCHAR* GunTemplate : {
		TEXT("object/weapon/ranged/pistol/shared_pistol_dl44.iff"),
		TEXT("object/weapon/ranged/rifle/shared_rifle_t21.iff"),
		TEXT("object/weapon/ranged/carbine/shared_carbine_dh17.iff") })
	{
		TArray<FSWGMeshHardpoint> Hardpoints;
		if (TestTrue(FString::Printf(TEXT("%s mesh reads"), GunTemplate), ReadHardpoints(*Tre, GunTemplate, Hardpoints)))
		{
			const FSWGMeshHardpoint* Muzzle = FindHardpoint(Hardpoints, SWGWeaponHardpoint::Muzzle);
			if (TestNotNull(FString::Printf(TEXT("%s has a muzzle"), GunTemplate), Muzzle))
			{
				TestTrue(FString::Printf(TEXT("%s muzzle is out along the barrel"), GunTemplate), Muzzle->Translation.X > 5.0);
			}
		}
	}

	// The DL-44 muzzle is 0.159 m down the barrel in the file (z), which is X here.
	{
		TArray<FSWGMeshHardpoint> Hardpoints;
		if (ReadHardpoints(*Tre, TEXT("object/weapon/ranged/pistol/shared_pistol_dl44.iff"), Hardpoints))
		{
			if (const FSWGMeshHardpoint* Muzzle = FindHardpoint(Hardpoints, SWGWeaponHardpoint::Muzzle))
			{
				TestEqual(TEXT("DL-44 muzzle distance in cm"), static_cast<float>(Muzzle->Translation.X), 0.159f * SWGWorldScale, 1.0f);
			}
		}
	}

	// Melee weapons carry the two ends of the swing ribbon; a sword's run up its blade (SWG +y, UE +Z).
	for (const TCHAR* MeleeTemplate : {
		TEXT("object/weapon/melee/sword/shared_sword_01.iff"),
		TEXT("object/weapon/melee/sword/shared_sword_lightsaber_obi.iff"),
		TEXT("object/weapon/melee/baton/shared_baton_stun.iff") })
	{
		TArray<FSWGMeshHardpoint> Hardpoints;
		if (TestTrue(FString::Printf(TEXT("%s mesh reads"), MeleeTemplate), ReadHardpoints(*Tre, MeleeTemplate, Hardpoints)))
		{
			const FSWGMeshHardpoint* TrailStart = FindHardpoint(Hardpoints, SWGWeaponHardpoint::TrailStart);
			const FSWGMeshHardpoint* TrailEnd = FindHardpoint(Hardpoints, SWGWeaponHardpoint::TrailEnd);
			if (TestNotNull(FString::Printf(TEXT("%s trail start"), MeleeTemplate), TrailStart) && TestNotNull(FString::Printf(TEXT("%s trail end"), MeleeTemplate), TrailEnd))
			{
				TestTrue(FString::Printf(TEXT("%s trail runs along the blade"), MeleeTemplate), TrailEnd->Translation.Z > TrailStart->Translation.Z);
			}
		}
	}

	// A double-bladed lance has a second ribbon.
	{
		TArray<FSWGMeshHardpoint> Hardpoints;
		if (TestTrue(TEXT("lance mesh reads"), ReadHardpoints(*Tre, TEXT("object/weapon/melee/polearm/shared_lance_lightsaber_02.iff"), Hardpoints)))
		{
			TestNotNull(TEXT("lance second trail start"), FindHardpoint(Hardpoints, SWGWeaponHardpoint::SecondTrailStart));
			TestNotNull(TEXT("lance second trail end"), FindHardpoint(Hardpoints, SWGWeaponHardpoint::SecondTrailEnd));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGWeaponProjectileTest,
	"SWGEmu.Fx.WeaponProjectile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGWeaponProjectileTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCombatVisualsTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	FSWGDataTableData WeaponTable;
	if (!TestTrue(TEXT("weapon.iff reads"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/weapon/weapon.iff")), WeaponTable)))
	{
		return false;
	}

	for (const TCHAR* GunTemplate : {
		TEXT("object/weapon/ranged/pistol/shared_pistol_dl44.iff"),
		TEXT("object/weapon/ranged/rifle/shared_rifle_t21.iff"),
		TEXT("object/weapon/ranged/carbine/shared_carbine_dh17.iff") })
	{
		FSWGWeaponSoundSet Sounds;
		if (TestTrue(FString::Printf(TEXT("%s resolves"), GunTemplate), USWGCombatAudioSubsystem::ResolveWeaponSounds(*Tre, WeaponTable, GunTemplate, Sounds)))
		{
			TestTrue(FString::Printf(TEXT("%s names a projectile .prt ('%s')"), GunTemplate, *Sounds.ProjectileEffect), Sounds.ProjectileEffect.EndsWith(TEXT(".prt")));
			TestTrue(FString::Printf(TEXT("%s projectile exists"), GunTemplate), Tre->FileExists(Sounds.ProjectileEffect));
			TestEqual(TEXT("the table helper agrees"), Sounds.ProjectileEffect, FSWGEffectTables::FindProjectileAppearance(WeaponTable, Sounds.EffectId, Sounds.EffectIndex));
		}
	}

	// A melee weapon has a swing, not a projectile.
	FSWGWeaponSoundSet Sword;
	if (TestTrue(TEXT("sword resolves"), USWGCombatAudioSubsystem::ResolveWeaponSounds(*Tre, WeaponTable, TEXT("object/weapon/melee/sword/shared_sword_01.iff"), Sword)))
	{
		TestTrue(TEXT("a sword has a swing effect"), !Sword.SwingEffect.IsEmpty());
	}

	// Playback entries: a plain shot sets no speed (the engine default stands), an explicit LEVP wins.
	FSWGCombatActionAnimation Plain;
	TestEqual(TEXT("default projectile speed"), USWGCombatVisualsSubsystem::ResolveProjectileSpeed(Plain), 55.f);
	FSWGCombatActionAnimation Fast;
	Fast.Variables.Add(TEXT("LEVP"), TEXT("80"));
	TestEqual(TEXT("LEVP overrides the speed"), USWGCombatVisualsSubsystem::ResolveProjectileSpeed(Fast), 80.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGLightsaberResolutionTest,
	"SWGEmu.Fx.LightsaberResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGLightsaberResolutionTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCombatVisualsTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// A saber template's appearance is a .lsb, whose BASE hilt is the mesh that gets built.
	FString Appearance;
	const FString SaberTemplate = TEXT("object/weapon/melee/sword/shared_sword_lightsaber_obi.iff");
	const FString Hilt = ResolveHiltAppearance(*Tre, SaberTemplate, &Appearance);
	TestTrue(TEXT("saber appearance is a .lsb"), Appearance.EndsWith(TEXT(".lsb")));
	TestTrue(TEXT("the hilt is an .apt"), Hilt.EndsWith(TEXT(".apt")));
	TestTrue(TEXT("the hilt resolves to a mesh"), FindFirstMeshPath(*Tre, Hilt).EndsWith(TEXT("_l0.msh")));

	FSWGLightsaber Saber;
	if (TestTrue(TEXT(".lsb reads"), FSWGLightsaberReader::Read(Tre->CreateIffReader(Appearance), Saber)))
	{
		TestEqual(TEXT("one blade"), Saber.Blades.Num(), 1);
		TestTrue(TEXT("blade has a length"), Saber.Blades.Num() > 0 && Saber.Blades[0].Length > 0.5f);
		TestTrue(TEXT("hum sound exists"), Tre->FileExists(Saber.HumSoundPath));
		TestTrue(TEXT("blade shader exists"), Saber.Blades.Num() > 0 && Tre->FileExists(Saber.Blades[0].ShaderPath));
	}

	// The double-bladed lance has two.
	FString LanceAppearance;
	ResolveHiltAppearance(*Tre, TEXT("object/weapon/melee/polearm/shared_lance_lightsaber_02.iff"), &LanceAppearance);
	FSWGLightsaber Lance;
	if (TestTrue(TEXT("lance .lsb reads"), FSWGLightsaberReader::Read(Tre->CreateIffReader(LanceAppearance), Lance)))
	{
		TestEqual(TEXT("two blades"), Lance.Blades.Num(), 2);
	}

	// Crafted sabers take their swing trail from the swoosh table (a white .swh, tinted by the object's hue) and every saber its ignite sounds from the weapon cdf.
	FSWGDataTableData SwooshTable;
	if (TestTrue(TEXT("swoosh.iff reads"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/weapon/swoosh.iff")), SwooshTable)))
	{
		FString SwooshPath;
		bool bUseObjectHue = false;
		if (TestTrue(TEXT("a crafted saber has a swoosh"), FSWGEffectTables::FindSwoosh(SwooshTable, TEXT("object/weapon/melee/sword/crafted_saber/shared_sword_lightsaber_one_handed_gen1.iff"), SwooshPath, bUseObjectHue)))
		{
			FSWGSwoosh Swoosh;
			TestTrue(TEXT("swoosh reads"), FSWGSwooshReader::Read(Tre->CreateIffReader(SwooshPath), Swoosh));
			TestTrue(TEXT("a crafted saber takes the object hue"), bUseObjectHue);
		}
	}

	FString ClientDataPath;
	FSWGClientDataFile ClientData;
	if (TestTrue(TEXT("saber cdf is named"), Tre->FindTemplateFormStringParam(SaberTemplate, SWG_IFF_TAG('S', 'H', 'O', 'T'), TEXT("clientDataFile"), ClientDataPath))
		&& TestTrue(TEXT("saber cdf reads"), FSWGClientDataFileReader::Read(Tre->CreateIffReader(ClientDataPath), ClientData)))
	{
		bool bBladeOn = false;
		bool bBladeOff = false;
		for (const FSWGClientDataEvent& Event : ClientData.EffectEvents)
		{
			bBladeOn |= Event.EventName == TEXT("bladeon");
			bBladeOff |= Event.EventName == TEXT("bladeoff");
		}
		TestTrue(TEXT("bladeon effect"), bBladeOn);
		TestTrue(TEXT("bladeoff effect"), bBladeOff);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGCombatVisualsMathTest,
	"SWGEmu.Fx.CombatVisualsMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGCombatVisualsMathTest::RunTest(const FString& Parameters)
{
	// 55 m/s is 5500 cm/s: 55 m takes a second, and a stopped projectile never lands (the caller clamps).
	TestEqual(TEXT("55 m at 55 m/s"), USWGCombatVisualsSubsystem::ProjectileFlightSeconds(55.f * SWGWorldScale, 55.f), 1.f, 0.001f);
	TestEqual(TEXT("11 m at 55 m/s"), USWGCombatVisualsSubsystem::ProjectileFlightSeconds(11.f * SWGWorldScale, 55.f), 0.2f, 0.001f);
	TestEqual(TEXT("a faster bolt is quicker"), USWGCombatVisualsSubsystem::ProjectileFlightSeconds(55.f * SWGWorldScale, 110.f), 0.5f, 0.001f);
	TestEqual(TEXT("no speed, no flight time"), USWGCombatVisualsSubsystem::ProjectileFlightSeconds(1000.f, 0.f), 0.f);

	// CombatAction Trails: 0xFF (default) and the weapon bit draw a ribbon; none or hands and feet alone do not.
	TestTrue(TEXT("default trail includes the weapon"), USWGCombatVisualsSubsystem::WantsWeaponTrail(0xFF));
	TestTrue(TEXT("weapon bit"), USWGCombatVisualsSubsystem::WantsWeaponTrail(SWGCombatTrail::Weapon));
	TestFalse(TEXT("no trail"), USWGCombatVisualsSubsystem::WantsWeaponTrail(0x00));
	TestFalse(TEXT("limb trails alone"), USWGCombatVisualsSubsystem::WantsWeaponTrail(SWGCombatTrail::LeftHand | SWGCombatTrail::RightFoot));

	// LTTA spellings in the data.
	TestEqual(TEXT("3.0 seconds"), USWGCombatVisualsSubsystem::ParseEffectSeconds(TEXT("3.0"), 9.f), 3.f);
	TestEqual(TEXT("the .0.5 typo is half a second"), USWGCombatVisualsSubsystem::ParseEffectSeconds(TEXT(".0.5"), 9.f), 0.5f);
	TestEqual(TEXT(".5 is half a second"), USWGCombatVisualsSubsystem::ParseEffectSeconds(TEXT(".5"), 9.f), 0.5f);
	TestEqual(TEXT("empty falls back"), USWGCombatVisualsSubsystem::ParseEffectSeconds(FString(), 9.f), 9.f);

	// The ribbon fades linearly over the swoosh lifetime.
	TestEqual(TEXT("fresh sample"), USWGTrailComponent::ComputeFade(0.f, 0.5f), 1.f);
	TestEqual(TEXT("half-life sample"), USWGTrailComponent::ComputeFade(0.25f, 0.5f), 0.5f, 0.001f);
	TestEqual(TEXT("expired sample"), USWGTrailComponent::ComputeFade(0.6f, 0.5f), 0.f);

	// The blade extends over OPEN seconds and retracts over CLOS.
	TestEqual(TEXT("blade starts shut"), USWGLightsaberComponent::ComputeBladeFraction(0.f, 1.5f, false), 0.f);
	TestEqual(TEXT("blade half open"), USWGLightsaberComponent::ComputeBladeFraction(0.75f, 1.5f, false), 0.5f, 0.001f);
	TestEqual(TEXT("blade fully open"), USWGLightsaberComponent::ComputeBladeFraction(3.f, 1.5f, false), 1.f);
	TestEqual(TEXT("blade half closed"), USWGLightsaberComponent::ComputeBladeFraction(0.75f, 1.5f, true), 0.5f, 0.001f);
	TestEqual(TEXT("blade shut again"), USWGLightsaberComponent::ComputeBladeFraction(3.f, 1.5f, true), 0.f);
	TestEqual(TEXT("an instant ignite"), USWGLightsaberComponent::ComputeBladeFraction(0.f, 0.f, false), 1.f);
	TestEqual(TEXT("flicker stays within its floor"), USWGLightsaberComponent::ComputeFlickerScale(0.f, 0.65f), 0.65f, 0.001f);
	TestEqual(TEXT("flicker peaks at full"), USWGLightsaberComponent::ComputeFlickerScale(1.f, 0.65f), 1.f, 0.001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
