#if WITH_DEV_AUTOMATION_TESTS

#include "SWGUISound.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "SWGAudioTypes.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGSoundTemplateReader.h"
#include "TRE/SWGUIStyleReader.h"

/** Needs the retail data: every UI sound the style sheet and sounds.iff name must be a real .snd with samples. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGUISoundsTest,
	"SWGEmu.Audio.UISounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSWGUISoundsTest::RunTest(const FString& Parameters)
{
	UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
	USWGTreSubsystem* Tre = NewObject<USWGTreSubsystem>(Outer);
	Tre->AddToRoot();
	ON_SCOPE_EXIT { Tre->RemoveFromRoot(); };
	Tre->LoadConfig();
	if (!Tre->LoadArchives())
	{
		AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
		return false;
	}

	auto CheckSound = [this, Tre](const FString& Label, const FString& Reference, bool bRequired = true)
	{
		const FString Path = SWGAudio::NormalizeSoundPath(Reference);
		FSWGSoundTemplate Template;
		const bool bParsed = Tre->FileExists(Path) && FSWGSoundTemplateReader::Read(Tre->CreateIffReader(Path), Template);
		if (!bRequired && !bParsed)
		{
			AddInfo(FString::Printf(TEXT("%s -> %s is missing from the retail data"), *Label, *Path));
			return;
		}
		TestTrue(FString::Printf(TEXT("%s '%s' -> %s is a .snd template"), *Label, *Reference, *Path), bParsed);
		if (bParsed)
		{
			TestTrue(FString::Printf(TEXT("%s has samples"), *Path), !Template.Samples.IsEmpty());
		}
	};

	const FSWGUIStyleSheet* Sheet = Tre->GetUIStyleSheet();
	if (!TestNotNull(TEXT("style sheet"), Sheet))
	{
		return false;
	}
	TestNotNull(TEXT("hud button MouseOverSound is in the data, not the fallback"), Sheet->FindElementSound(TEXT("new.buttons.hud.style"), TEXT("MouseOverSound")));
	TestNotNull(TEXT("hud button ReleaseSound is in the data, not the fallback"), Sheet->FindElementSound(TEXT("new.buttons.hud.style"), TEXT("ReleaseSound")));
	TestTrue(TEXT("style sheet keeps many sound elements"), Sheet->ElementSounds.Num() > 50);

	const FSWGUISoundNames Names = FSWGUISoundNames::Resolve(Tre);
	AddInfo(FString::Printf(TEXT("hover=%s click=%s keyclick=%s named=%d"), *Names.Hover, *Names.Click, *Names.Keyclick, Names.Named.Num()));
	CheckSound(TEXT("hover"), Names.Hover);
	CheckSound(TEXT("click"), Names.Click);
	CheckSound(TEXT("keyclick"), Names.Keyclick);

	TestTrue(TEXT("sounds.iff names radial_create"), Names.Named.Contains(TEXT("radial_create")));
	TestTrue(TEXT("sounds.iff has the whole table"), Names.Named.Num() >= 26);
	for (const TPair<FString, FString>& Named : Names.Named)
	{
		// Retail itself names ui_hover_move.snd, which no archive holds.
		CheckSound(FString::Printf(TEXT("sounds.iff '%s'"), *Named.Key), Named.Value, Named.Key != TEXT("hover_move"));
	}

	for (const TPair<FString, TMap<FString, FString>>& Element : Sheet->ElementSounds)
	{
		for (const TPair<FString, FString>& Attribute : Element.Value)
		{
			CheckSound(Element.Key + TEXT(".") + Attribute.Key, Attribute.Value);
		}
	}
	return true;
}

#endif
