#include "Misc/AutomationTest.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "SWGAudioTypes.h"
#include "SWGEffectsSubsystem.h"
#include "Engine/GameInstance.h"
#include "TRE/SWGClientEffectReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	USWGTreSubsystem* LoadTreSubsystem(FAutomationTestBase& Test)
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

/** Real combat and survey .cef files parse into the parts the effect playback relies on. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGClientEffectPartsTest,
	"SWGEmu.Fx.ClientEffectParts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGClientEffectPartsTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreSubsystem(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// Pistol fire: at least one PSND and one particle, each particle present in the TRE.
	const FString FireEffect = SWGAudio::NormalizeEffectPath(TEXT("clienteffect/combat_bls_0_wep_fire_dh17.cef"));
	FSWGClientEffect Fire;
	if (TestTrue(TEXT("dh17 fire parses"), FSWGClientEffectReader::Read(Tre->CreateIffReader(FireEffect), Fire)))
	{
		TestTrue(TEXT("dh17 fire has a sound"), Fire.SoundPaths.Num() > 0);
		TestTrue(TEXT("dh17 fire has a particle"), Fire.Particles.Num() > 0);
		for (const FSWGClientEffectParticle& Particle : Fire.Particles)
		{
			TestTrue(*FString::Printf(TEXT("particle %s is in the TRE"), *Particle.Path),
				Tre->FileExists(USWGEffectsSubsystem::NormalizeEffectPath(Particle.Path)));
			TestTrue(*FString::Printf(TEXT("particle %s has a scale"), *Particle.Path), Particle.Scale > 0.f);
		}
	}

	// Lights: the last parameter is the range in metres, which playback uses as the attenuation radius.
	int32 LightCount = 0;
	int32 RangedLightCount = 0;
	for (const FString& EffectPath : Tre->FindVirtualPaths(TEXT("clienteffect/")))
	{
		FSWGClientEffect Effect;
		if (!EffectPath.EndsWith(TEXT(".cef")) || !FSWGClientEffectReader::Read(Tre->CreateIffReader(EffectPath), Effect))
		{
			continue;
		}
		for (const FSWGClientEffectLight& Light : Effect.Lights)
		{
			++LightCount;
			RangedLightCount += Light.Params[4] > 0.f ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("%d of %d .cef lights have a positive range"), RangedLightCount, LightCount));
	TestTrue(TEXT("retail .cef files carry lights"), LightCount > 0);
	TestEqual(TEXT("every light has a positive range"), RangedLightCount, LightCount);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
