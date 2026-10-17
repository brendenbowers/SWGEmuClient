#include "Misc/AutomationTest.h"
#include "TRE/SWGAnimationReader.h"
#include "Import/SWGAnimationImporter.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Engine/GameInstance.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	template<typename T> void AppendLE(TArray<uint8>& Bytes, T Value)
	{
		Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(Value));
	}
	void AppendChunk(TArray<uint8>& Bytes, const ANSICHAR* Tag, const TArray<uint8>& Payload)
	{
		Bytes.Append(reinterpret_cast<const uint8*>(Tag), 4);
		const uint32 Size = Payload.Num();
		for (int32 Shift : {24, 16, 8, 0}) Bytes.Add((uint8)(Size >> Shift));
		Bytes.Append(Payload);
	}
	TArray<uint8> Form(const ANSICHAR* Tag, const TArray<uint8>& Children)
	{
		TArray<uint8> Payload, Bytes;
		Payload.Append(reinterpret_cast<const uint8*>(Tag), 4);
		Payload.Append(Children);
		AppendChunk(Bytes, "FORM", Payload);
		return Bytes;
	}
	TArray<uint8> TranslationClip(bool bCompressed, bool bBadIndex = false)
	{
		auto Index = [bCompressed](TArray<uint8>& Bytes, uint32 Value)
		{
			if (bCompressed) AppendLE(Bytes, (uint16)Value); else AppendLE(Bytes, Value);
		};
		TArray<uint8> Info, Xfrm, Channel, Atrn, Static, Body;
		AppendLE(Info, 30.f);
		for (uint32 Value : {3u, 2u, 0u, 0u, 1u, 2u}) Index(Info, Value);
		AppendChunk(Body, "INFO", Info);
		for (const ANSICHAR* Name : {"root", "finger"})
		{
			const bool bRoot = FCStringAnsi::Strcmp(Name, "root") == 0;
			TArray<uint8> Bone;
			Bone.Append(reinterpret_cast<const uint8*>(Name), FCStringAnsi::Strlen(Name) + 1);
			Bone.Add(0); // No rotation entry: translation-only bones keep the bind rotation.
			Index(Bone, 0);
			if (bCompressed) Bone.Add(bRoot ? 16 : 8); else AppendLE(Bone, bRoot ? 16u : 8u);
			Index(Bone, bRoot ? 1 : (bBadIndex ? 99 : 0));
			Index(Bone, 0);
			Index(Bone, 1);
			AppendChunk(Xfrm, "XFIN", Bone);
		}
		Body.Append(Form("XFRM", Xfrm));
		Body.Append(Form("AROT", {}));
		Index(Channel, 2);
		Index(Channel, 0); AppendLE(Channel, 1.f);
		Index(Channel, 2); AppendLE(Channel, 5.f);
		AppendChunk(Atrn, "CHNL", Channel);
		Body.Append(Form("ATRN", Atrn));
		AppendLE(Static, 2.f); AppendLE(Static, 3.f);
		AppendChunk(Body, "STRN", Static);
		return Form(bCompressed ? "CKAT" : "KFAT", Form("0001", Body));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGAnimationTranslationTest, "SWGEmu.Animation.Translation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGAnimationTranslationTest::RunTest(const FString& Parameters)
{
	for (bool bCompressed : {true, false})
	{
		FSWGAnimationData Animation;
		if (!TestTrue(TEXT("translation-only clip parses"), FSWGAnimationReader::ReadAnimation(FSWGIffReader(TranslationClip(bCompressed)), Animation))) continue;
		TestEqual(TEXT("both bones retained without rotation entries"), Animation.BoneTracks.Num(), 2);
		if (Animation.BoneTracks.Num() != 2) continue;
		TestEqual(TEXT("non-root axis mapping and scale"), Animation.BoneTracks[1].TranslationDeltas.FindRef(2), FVector(300, 500, 200));
		TestEqual(TEXT("root retains vertical bob only"), Animation.RootTranslationDeltas.FindRef(2), FVector(0, 0, 500));
#if WITH_EDITOR
		FSWGSkeletonData Skeleton;
		FSWGSkeletonJoint& Root = Skeleton.Joints.AddDefaulted_GetRef();
		Root.Name = TEXT("root");
		Root.BindPoseTranslation = FVector(1, 2, 3);
		FSWGSkeletonJoint& Finger = Skeleton.Joints.AddDefaulted_GetRef();
		Finger.Name = TEXT("FINGER");
		Finger.BindPoseTranslation = FVector(10, 20, 30);
		Finger.BindPoseRotation = FQuat(FVector::UpVector, 0.4);
		FSWGAnimSequenceBuildData Built;
		if (TestTrue(TEXT("import translation tracks"), FSWGAnimationImporter::BuildAnimSequenceData(Animation, Skeleton, Built)))
		{
			TestEqual(TEXT("interpolated translation adds bind pose"), Built.BoneTracks[1].PosKeys[1], FVector(310, 320, 230));
			TestTrue(TEXT("translation-only bone keeps its bind rotation"), Built.BoneTracks[1].RotKeys[1].Equals(Finger.ComposeLocalRotation(Finger.BindPoseRotation)));
			TestEqual(TEXT("root horizontal motion stays in place"), Built.BoneTracks[0].PosKeys[2], FVector(1, 2, 503));
		}
#endif
		TestFalse(TEXT("out of range translation channel rejected"), FSWGAnimationReader::ReadAnimation(FSWGIffReader(TranslationClip(bCompressed, true)), Animation));
	}

	UGameInstance* Outer = NewObject<UGameInstance>();
	USWGTreSubsystem* Tre = NewObject<USWGTreSubsystem>(Outer);
	Tre->AddToRoot();
	ON_SCOPE_EXIT { Tre->RemoveFromRoot(); };
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives load"), Tre->LoadArchives())) return false;
	for (const TCHAR* Path : {TEXT("appearance/animation/acklay_emt_stand_stretch.ans"), TEXT("appearance/animation/all_b_entertained.ans"), TEXT("appearance/animation/protocol_droid_emt_greet.ans")})
	{
		FSWGAnimationData Animation;
		TestTrue(Path, FSWGAnimationReader::ReadAnimation(Tre->CreateIffReader(Path), Animation));
		TestFalse(TEXT("retail translation channels decoded"), Animation.BoneTracks.IsEmpty());
	}
	return true;
}
#endif
