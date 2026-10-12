#include "Subsystems/SWGAnimatedMaterialSubsystem.h"

#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShaderAnimation(
		TEXT("swg.Fx.ShaderAnimation"), 1,
		TEXT("1 animates SWTS/SWSH flipbooks and TSNS scrolls on object materials, 0 freezes them."));

	TAutoConsoleVariable<float> CVarShaderAnimationHz(
		TEXT("swg.Fx.ShaderAnimationHz"), 0.f,
		TEXT("Maximum material updates per second; 0 updates every frame."));
}

TStatId USWGAnimatedMaterialSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGAnimatedMaterialSubsystem, STATGROUP_Tickables);
}

int32 USWGAnimatedMaterialSubsystem::SelectFrame(const FSWGShaderAnimation& Animation, double TimeSeconds, uint64 FrameCounter, FRandomStream& Random, FSWGShaderFrameState& State)
{
	const int32 FrameCount = Animation.GetDeclaredFrames();
	if (FrameCount <= 0)
	{
		return 0;
	}

	switch (Animation.Timing)
	{
		case ESWGShaderTiming::DeltaTime:
			return Animation.SecondsPerFrame > 0.f ? static_cast<int32>(FMath::FloorToInt64(TimeSeconds / Animation.SecondsPerFrame) % FrameCount) : 0;
		case ESWGShaderTiming::DeltaFrame:
			return static_cast<int32>(FrameCounter % static_cast<uint64>(FrameCount));
		case ESWGShaderTiming::RandomFrame:
			return Random.RandRange(0, FrameCount - 1);
		case ESWGShaderTiming::RandomTime:
			if (TimeSeconds >= State.NextChangeTime)
			{
				State.Frame = Random.RandRange(0, FrameCount - 1);
				// Retail (SWGEmu.exe 0xb5ada0, 0xb5aeb0): hold time uniform between the two DRTS floats, whichever order.
				const float HoldMin = FMath::Min(Animation.RandomTimeA, Animation.RandomTimeB);
				const float HoldMax = FMath::Max(Animation.RandomTimeA, Animation.RandomTimeB);
				State.NextChangeTime = TimeSeconds + FMath::Max(0.001f, Random.FRandRange(HoldMin, HoldMax));
			}
			return State.Frame;
		default:
			return 0;
	}
}

FVector2D USWGAnimatedMaterialSubsystem::ComputeScrollOffset(const FVector2D& Rate, double TimeSeconds)
{
	auto Wrap = [](double Value) { return Value - FMath::FloorToDouble(Value); };
	return FVector2D(Wrap(Rate.X * TimeSeconds), Wrap(Rate.Y * TimeSeconds));
}

void USWGAnimatedMaterialSubsystem::RegisterFlipbook(UMaterialInstanceDynamic* Material, const FSWGShaderAnimation& Animation, const TArray<UTexture2D*>& Frames, const FString& Name)
{
	if (!Material || Frames.Num() < 2)
	{
		return;
	}
	FSWGFlipbookEntry& Entry = Flipbooks.AddDefaulted_GetRef();
	Entry.Material = Material;
	Entry.Animation = Animation;
	Entry.Name = Name;
	for (UTexture2D* Frame : Frames)
	{
		Entry.Frames.Add(Frame);
	}
}

void USWGAnimatedMaterialSubsystem::RegisterScroll(UMaterialInstanceDynamic* Material, const FSWGShaderScroll& Scroll, const FString& Name)
{
	if (!Material)
	{
		return;
	}
	FSWGScrollEntry& Entry = Scrolls.AddDefaulted_GetRef();
	Entry.Material = Material;
	Entry.RateLayer1 = FVector2D(Scroll.U1, Scroll.V1);
	Entry.RateLayer2 = FVector2D(Scroll.U2, Scroll.V2);
	Entry.Name = Name;
}

void USWGAnimatedMaterialSubsystem::Tick(float DeltaTime)
{
	ClockSeconds += DeltaTime;
	++TickCounter;
	if (CVarShaderAnimation.GetValueOnGameThread() == 0)
	{
		return;
	}

	SecondsSinceUpdate += DeltaTime;
	const float MaxHz = CVarShaderAnimationHz.GetValueOnGameThread();
	if (MaxHz > 0.f && SecondsSinceUpdate < 1.0 / MaxHz)
	{
		return;
	}
	SecondsSinceUpdate = 0.0;

	Flipbooks.RemoveAllSwap([](const FSWGFlipbookEntry& Entry) { return !Entry.Material.IsValid(); });
	for (FSWGFlipbookEntry& Entry : Flipbooks)
	{
		const int32 Frame = SelectFrame(Entry.Animation, ClockSeconds, TickCounter, Random, Entry.State);
		if (Frame != Entry.AppliedFrame && Entry.Frames.IsValidIndex(Frame))
		{
			Entry.Material->SetTextureParameterValue(TEXT("Diffuse"), Entry.Frames[Frame]);
			Entry.AppliedFrame = Frame;
		}
	}

	Scrolls.RemoveAllSwap([](const FSWGScrollEntry& Entry) { return !Entry.Material.IsValid(); });
	for (const FSWGScrollEntry& Entry : Scrolls)
	{
		const FVector2D Layer1 = ComputeScrollOffset(Entry.RateLayer1, ClockSeconds);
		const FVector2D Layer2 = ComputeScrollOffset(Entry.RateLayer2, ClockSeconds);
		Entry.Material->SetVectorParameterValue(TEXT("ScrollUV1"), FLinearColor(Layer1.X, Layer1.Y, 0.f, 0.f));
		Entry.Material->SetVectorParameterValue(TEXT("ScrollUV2"), FLinearColor(Layer2.X, Layer2.Y, 0.f, 0.f));
	}
}

TArray<FString> USWGAnimatedMaterialSubsystem::DescribeEntries() const
{
	static const TCHAR* const TimingNames[] = { TEXT("none"), TEXT("DTST"), TEXT("DFST"), TEXT("DRFS"), TEXT("DRTS") };
	TArray<FString> Lines;
	for (const FSWGFlipbookEntry& Entry : Flipbooks)
	{
		Lines.Add(FString::Printf(TEXT("%s %s %s: %d frames, showing %d%s"), Entry.Animation.bSwitchesShaders ? TEXT("SWSH") : TEXT("SWTS"),
			TimingNames[static_cast<int32>(Entry.Animation.Timing)], *Entry.Name, Entry.Frames.Num(), Entry.AppliedFrame, Entry.Material.IsValid() ? TEXT("") : TEXT(" (MID gone)")));
	}
	for (const FSWGScrollEntry& Entry : Scrolls)
	{
		const FVector2D Layer1 = ComputeScrollOffset(Entry.RateLayer1, ClockSeconds);
		Lines.Add(FString::Printf(TEXT("TSNS %s: rate1 (%.3f, %.3f) rate2 (%.3f, %.3f), layer1 offset (%.3f, %.3f)%s"), *Entry.Name,
			Entry.RateLayer1.X, Entry.RateLayer1.Y, Entry.RateLayer2.X, Entry.RateLayer2.Y, Layer1.X, Layer1.Y, Entry.Material.IsValid() ? TEXT("") : TEXT(" (MID gone)")));
	}
	return Lines;
}
