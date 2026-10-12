#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Subsystems/SWGAnimatedMaterialSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGShaderReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FSWGShaderAnimation MakeAnimation(ESWGShaderTiming Timing, int32 FrameCount)
	{
		FSWGShaderAnimation Animation;
		Animation.Timing = Timing;
		Animation.FrameCount = FrameCount;
		Animation.Textures.SetNum(FrameCount);
		return Animation;
	}

	void CheckScroll(FAutomationTestBase& Test, USWGTreSubsystem& Tre, const TCHAR* ShaderPath, const FVector4& ExpectedRates)
	{
		FSWGShaderData Shader;
		if (!Test.TestTrue(*FString::Printf(TEXT("%s reads"), ShaderPath), FSWGShaderReader::ReadShader(Tre.CreateIffReader(ShaderPath), Shader)))
		{
			return;
		}
		const FSWGShaderScroll* Main = Shader.Scrolls.FindByPredicate([](const FSWGShaderScroll& Candidate) { return Candidate.Tag == TEXT("MAIN"); });
		if (Test.TestNotNull(*FString::Printf(TEXT("%s has a MAIN scroll"), ShaderPath), Main))
		{
			Test.TestEqual(*FString::Printf(TEXT("%s U1"), ShaderPath), Main->U1, (float)ExpectedRates.X, 0.001f);
			Test.TestEqual(*FString::Printf(TEXT("%s V1"), ShaderPath), Main->V1, (float)ExpectedRates.Y, 0.001f);
			Test.TestEqual(*FString::Printf(TEXT("%s U2"), ShaderPath), Main->U2, (float)ExpectedRates.Z, 0.001f);
			Test.TestEqual(*FString::Printf(TEXT("%s V2"), ShaderPath), Main->V2, (float)ExpectedRates.W, 0.001f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGShaderAnimationTest,
	"SWGEmu.Fx.ShaderAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGShaderAnimationTest::RunTest(const FString& Parameters)
{
	FRandomStream Random(1234);
	FSWGShaderFrameState State;

	// DTST: fixed time per frame, looping.
	FSWGShaderAnimation Timed = MakeAnimation(ESWGShaderTiming::DeltaTime, 8);
	Timed.SecondsPerFrame = 0.1f;
	TestEqual(TEXT("DTST at 0"), USWGAnimatedMaterialSubsystem::SelectFrame(Timed, 0.0, 0, Random, State), 0);
	TestEqual(TEXT("DTST at 0.35"), USWGAnimatedMaterialSubsystem::SelectFrame(Timed, 0.35, 0, Random, State), 3);
	TestEqual(TEXT("DTST wraps at 0.85"), USWGAnimatedMaterialSubsystem::SelectFrame(Timed, 0.85, 0, Random, State), 0);

	// DFST: one frame per rendered frame.
	const FSWGShaderAnimation PerFrame = MakeAnimation(ESWGShaderTiming::DeltaFrame, 4);
	TestEqual(TEXT("DFST frame 5"), USWGAnimatedMaterialSubsystem::SelectFrame(PerFrame, 99.0, 5, Random, State), 1);

	// DRFS: random frame, always in range and not constant.
	const FSWGShaderAnimation RandomFrame = MakeAnimation(ESWGShaderTiming::RandomFrame, 5);
	TSet<int32> Seen;
	for (int32 Sample = 0; Sample < 64; ++Sample)
	{
		const int32 Frame = USWGAnimatedMaterialSubsystem::SelectFrame(RandomFrame, 0.0, Sample, Random, State);
		TestTrue(TEXT("DRFS in range"), Frame >= 0 && Frame < 5);
		Seen.Add(Frame);
	}
	TestTrue(TEXT("DRFS varies"), Seen.Num() > 1);

	// DRTS: the frame holds until the hold time elapses, then changes within range.
	FSWGShaderAnimation RandomTime = MakeAnimation(ESWGShaderTiming::RandomTime, 6);
	RandomTime.RandomTimeA = 1.f;
	RandomTime.RandomTimeB = 2.f;
	FSWGShaderFrameState HoldState;
	const int32 First = USWGAnimatedMaterialSubsystem::SelectFrame(RandomTime, 0.0, 0, Random, HoldState);
	TestTrue(TEXT("DRTS in range"), First >= 0 && First < 6);
	TestTrue(TEXT("DRTS hold is positive and bounded"), HoldState.NextChangeTime >= 0.03 && HoldState.NextChangeTime <= 2.0);
	TestEqual(TEXT("DRTS holds before the change time"), USWGAnimatedMaterialSubsystem::SelectFrame(RandomTime, HoldState.NextChangeTime * 0.5, 1, Random, HoldState), First);

	// SWSH counts its shaders, not textures.
	FSWGShaderAnimation Switch;
	Switch.bSwitchesShaders = true;
	Switch.Timing = ESWGShaderTiming::DeltaTime;
	Switch.SecondsPerFrame = 1.f / 16.f;
	Switch.Shaders.SetNum(4);
	TestEqual(TEXT("SWSH at 0.1 s"), USWGAnimatedMaterialSubsystem::SelectFrame(Switch, 0.1, 0, Random, State), 1);

	// Scroll offsets wrap into [0,1).
	const FVector2D Wrapped = USWGAnimatedMaterialSubsystem::ComputeScrollOffset(FVector2D(0.0, -0.5), 3.0);
	TestEqual(TEXT("scroll U"), (float)Wrapped.X, 0.f, 0.0001f);
	TestEqual(TEXT("scroll V wraps negative"), (float)Wrapped.Y, 0.5f, 0.0001f);

	// Retail data.
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
		AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
		return false;
	}

	FSWGShaderAnimation Terminal;
	if (TestTrue(TEXT("anim_mission_terminal reads"), FSWGShaderReader::ReadAnimation(Tre->CreateIffReader(TEXT("shader/anim_mission_terminal.sht")), Terminal)))
	{
		TestEqual(TEXT("terminal frames"), Terminal.GetDeclaredFrames(), 8);
		TestEqual(TEXT("terminal seconds per frame"), Terminal.SecondsPerFrame, 0.1f, 0.001f);
		FSWGShaderFrameState TerminalState;
		TestEqual(TEXT("terminal frame at 0.35 s"), USWGAnimatedMaterialSubsystem::SelectFrame(Terminal, 0.35, 0, Random, TerminalState), 3);
	}

	CheckScroll(*this, *Tre, TEXT("shader/monitor_text_scroll.sht"), FVector4(0.0, 0.175, 0.0, -0.3));
	CheckScroll(*this, *Tre, TEXT("shader/waterfall_scroll_fast.sht"), FVector4(-0.2, -0.5, 0.0, -0.68));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
