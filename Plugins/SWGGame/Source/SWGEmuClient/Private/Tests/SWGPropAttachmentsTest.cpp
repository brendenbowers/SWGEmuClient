#include "Misc/AutomationTest.h"

#include "Common/SWGPropWind.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGAshReader.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGLatReader.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGWorldSnapshotReader.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Stage 5: the wind -> clip rule, and that every cdf flag/banner, particle and light reference resolves in the real data. */
namespace
{
	USWGTreSubsystem* LoadTreForPropAttachmentsTest(FAutomationTestBase& Test)
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

	TArray<FString> FindPropFilesWithExtension(const USWGTreSubsystem& Tre, const TCHAR* Extension)
	{
		TArray<FString> Paths = Tre.FindVirtualPaths(Extension);
		Paths.RemoveAll([Extension](const FString& Path) { return !Path.EndsWith(Extension, ESearchCase::IgnoreCase); });
		Paths.Sort();
		return Paths;
	}

	FString FirstString(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk)
	{
		return FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Reader.GetChunkData(Chunk))));
	}

	/** SMAT/0003: the first MSGN mesh path and LATX's skeleton -> lat pair (the same chain ResolveAppearanceMeshPaths walks). */
	bool ReadSatChain(const USWGTreSubsystem& Tre, const FString& SatPath, FString& OutMeshPath, FString& OutSkeletonPath, FString& OutLatPath)
	{
		const FSWGIffReader Reader = Tre.CreateIffReader(SatPath);
		FSWGIffChunk Smat, Version, Msgn, Latx;
		if (!Reader.FindForm(SWG_IFF_TAG('S', 'M', 'A', 'T'), Smat) || !Reader.FindChildForm(Smat, SWG_IFF_TAG('0', '0', '0', '3'), Version)
			|| !Reader.FindChildChunk(Version, SWG_IFF_TAG('M', 'S', 'G', 'N'), Msgn) || !Reader.FindChildChunk(Version, SWG_IFF_TAG('L', 'A', 'T', 'X'), Latx))
		{
			return false;
		}

		OutMeshPath = FirstString(Reader, Msgn);
		const uint8* Data = Reader.GetChunkData(Latx);
		const int32 Size = Reader.GetChunkSize(Latx);
		if (Size < 4 || (Data[0] | (Data[1] << 8)) < 1)
		{
			return false;
		}
		OutSkeletonPath = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Data + 2)));
		OutLatPath = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Data + 2 + OutSkeletonPath.Len() + 1)));
		return !OutMeshPath.IsEmpty() && !OutSkeletonPath.IsEmpty() && !OutLatPath.IsEmpty();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGPropWindTest,
	"SWGEmu.Fx.PropWind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGPropWindTest::RunTest(const FString& Parameters)
{
	// File order as flag.lat lists them: medium, heavy, idle.
	const TArray<FString> FlagClips = {
		TEXT("appearance/animation/flag_medium_wind_repeat.ans"),
		TEXT("appearance/animation/flag_heavy_wind_repeat.ans"),
		TEXT("appearance/animation/flag_idle.ans") };
	// banner.lat: heavy, medium, light, idle.
	const TArray<FString> BannerClips = {
		TEXT("appearance/animation/banner_heavy.ans"),
		TEXT("appearance/animation/banner_medium.ans"),
		TEXT("appearance/animation/banner_light.ans"),
		TEXT("appearance/animation/banner_idle.ans") };

	TestEqual(TEXT("flag clip tiers"), (int32)SWGPropWind::ClassifyClip(FlagClips[1]), (int32)SWGPropWind::ETier::Heavy);
	TestEqual(TEXT("idle clip tier"), (int32)SWGPropWind::ClassifyClip(FlagClips[2]), (int32)SWGPropWind::ETier::Idle);
	TestEqual(TEXT("unnamed clip"), (int32)SWGPropWind::ClassifyClip(TEXT("appearance/animation/all_b_loc_run.ans")), (int32)SWGPropWind::ETier::Unknown);

	TestEqual(TEXT("flag, no wind"), SWGPropWind::SelectClipIndex(FlagClips, 0.f), 2);
	TestEqual(TEXT("flag, default wind is medium"), SWGPropWind::SelectClipIndex(FlagClips, SWGPropWind::DefaultWindStrength), 0);
	TestEqual(TEXT("flag, gale"), SWGPropWind::SelectClipIndex(FlagClips, 1.f), 1);
	TestEqual(TEXT("flag, just under a third"), SWGPropWind::SelectClipIndex(FlagClips, 0.32f), 2);
	TestEqual(TEXT("flag, a third"), SWGPropWind::SelectClipIndex(FlagClips, 0.34f), 0);
	TestEqual(TEXT("flag, wind below zero clamps"), SWGPropWind::SelectClipIndex(FlagClips, -3.f), 2);
	TestEqual(TEXT("flag, wind above one clamps"), SWGPropWind::SelectClipIndex(FlagClips, 7.f), 1);

	TestEqual(TEXT("banner, no wind"), SWGPropWind::SelectClipIndex(BannerClips, 0.f), 3);
	TestEqual(TEXT("banner, breeze"), SWGPropWind::SelectClipIndex(BannerClips, 0.3f), 2);
	TestEqual(TEXT("banner, default wind is medium"), SWGPropWind::SelectClipIndex(BannerClips, SWGPropWind::DefaultWindStrength), 1);
	TestEqual(TEXT("banner, gale"), SWGPropWind::SelectClipIndex(BannerClips, 1.f), 0);

	TestEqual(TEXT("no clips"), SWGPropWind::SelectClipIndex(TArray<FString>(), 0.5f), -1);
	TestEqual(TEXT("unclassified clips"), SWGPropWind::SelectClipIndex({ TEXT("a/walk.ans"), TEXT("a/run.ans") }, 0.5f), -1);

	// MASP 10 and WSCL 1 leave the strength unchanged; a bigger scale or a lower cap strengthens it.
	TestEqual(TEXT("cdf defaults keep the wind"), SWGPropWind::ResolveFlagWind(0.5f, 1.f, 10.f), 0.5f, 0.001f);
	TestEqual(TEXT("scale 2 doubles it"), SWGPropWind::ResolveFlagWind(0.4f, 2.f, 10.f), 0.8f, 0.001f);
	TestEqual(TEXT("caps at one"), SWGPropWind::ResolveFlagWind(1.f, 3.f, 10.f), 1.f, 0.001f);
	TestEqual(TEXT("a zero cap is still"), SWGPropWind::ResolveFlagWind(1.f, 1.f, 0.f), 0.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGPropFlagsTest,
	"SWGEmu.Fx.PropFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGPropFlagsTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForPropAttachmentsTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	int32 FlagCount = 0, BannerCount = 0;
	TSet<FString> SeenSats;
	for (const FString& CdfPath : FindPropFilesWithExtension(*Tre, TEXT(".cdf")))
	{
		FSWGClientDataFile Data;
		if (!FSWGClientDataFileReader::Read(Tre->CreateIffReader(CdfPath), Data))
		{
			continue;
		}

		for (const FSWGClientDataFlag& Flag : Data.Flags)
		{
			(Flag.bBanner ? BannerCount : FlagCount) += 1;
			const FString Where = FString::Printf(TEXT("%s -> %s"), *CdfPath, *Flag.AppearancePath);
			TestFalse(*(Where + TEXT(" names a hardpoint")), Flag.Hardpoint.IsEmpty());
			TestEqual(*(Where + TEXT(" WSCL")), Flag.WindScale, 1.f);
			TestEqual(*(Where + TEXT(" MASP")), Flag.MaxWindSpeed, 10.f);
			TestEqual(*(Where + TEXT(" LIFT")), Flag.Lift, Flag.bBanner ? 30.f : 0.f);

			if (SeenSats.Contains(Flag.AppearancePath))
			{
				continue;
			}
			SeenSats.Add(Flag.AppearancePath);

			FString MeshPath, SkeletonPath, LatPath;
			if (!TestTrue(*(Where + TEXT(" .sat reads")), ReadSatChain(*Tre, Flag.AppearancePath, MeshPath, SkeletonPath, LatPath)))
			{
				continue;
			}
			TestTrue(*(MeshPath + TEXT(" exists")), Tre->FileExists(MeshPath));
			TestTrue(*(SkeletonPath + TEXT(" exists")), Tre->FileExists(SkeletonPath));
			TestEqual(*(MeshPath + TEXT(" names the skeleton the LATX does")),
				FSWGMeshReader::ReadSkeletalMeshSkeletonPath(Tre->CreateIffReader(MeshPath)), SkeletonPath);

			FSWGLatData Lat;
			FSWGAnimationStateHierarchy Hierarchy;
			if (!TestTrue(*(LatPath + TEXT(" reads")), FSWGLatReader::ReadLat(Tre->CreateIffReader(LatPath), Lat))
				|| !TestTrue(*(Lat.AshPath + TEXT(" reads")), FSWGAshReader::ReadHierarchy(Tre->CreateIffReader(Lat.AshPath), Hierarchy)))
			{
				continue;
			}

			const FSWGAnimationState* Root = Hierarchy.GetRoot();
			if (!TestNotNull(*(Lat.AshPath + TEXT(" root state")), Root))
			{
				continue;
			}
			TestEqual(*(Lat.AshPath + TEXT(" root loop")), Root->LoopAnimationName, FString(TEXT("loop_stand")));

			const FSWGLatEntry* Entry = Lat.Find(Root->LoopAnimationName);
			if (!TestNotNull(*(LatPath + TEXT(" has loop_stand")), Entry))
			{
				continue;
			}

			TArray<FString> Clips;
			for (const FSWGLatClip& Clip : Entry->Clips)
			{
				Clips.Add(Clip.AnsPath);
				TestTrue(*(Clip.AnsPath + TEXT(" exists")), Tre->FileExists(Clip.AnsPath));
				TestNotEqual(*(Clip.AnsPath + TEXT(" announces a wind tier")), (int32)SWGPropWind::ClassifyClip(Clip.AnsPath), (int32)SWGPropWind::ETier::Unknown);
			}
			TestTrue(*(LatPath + TEXT(" has at least idle, medium and heavy")), Clips.Num() >= 3);

			// Calm, default and gale each pick a different clip.
			const int32 Calm = SWGPropWind::SelectClipIndex(Clips, 0.f);
			const int32 Default = SWGPropWind::SelectClipIndex(Clips, SWGPropWind::DefaultWindStrength);
			const int32 Gale = SWGPropWind::SelectClipIndex(Clips, 1.f);
			TestTrue(*(LatPath + TEXT(" selects distinct clips")), Calm != Default && Default != Gale && Calm != Gale && Calm >= 0 && Default >= 0 && Gale >= 0);
			if (Clips.IsValidIndex(Calm) && Clips.IsValidIndex(Default) && Clips.IsValidIndex(Gale))
			{
				TestTrue(*(Clips[Calm] + TEXT(" is idle")), Clips[Calm].Contains(TEXT("idle")));
				TestTrue(*(Clips[Default] + TEXT(" is medium")), Clips[Default].Contains(TEXT("medium")));
				TestTrue(*(Clips[Gale] + TEXT(" is heavy")), Clips[Gale].Contains(TEXT("heavy")));
			}
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.PropFlags] %d flag cdf(s), %d banner cdf(s), %d distinct .sat"), FlagCount, BannerCount, SeenSats.Num());
	TestEqual(TEXT("flag cdfs"), FlagCount, 10);
	TestEqual(TEXT("banner cdfs"), BannerCount, 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGPropObjectsTest,
	"SWGEmu.Fx.PropObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGPropObjectsTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForPropAttachmentsTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	int32 GroundParticles = 0, ShipObjects = 0, Glows = 0, PointLights = 0, NightOnly = 0, MissingParticles = 0, ChildParticles = 0;
	TSet<FString> Missing;
	for (const FString& CdfPath : FindPropFilesWithExtension(*Tre, TEXT(".cdf")))
	{
		FSWGClientDataFile Data;
		if (!FSWGClientDataFileReader::Read(Tre->CreateIffReader(CdfPath), Data))
		{
			continue;
		}

		auto CheckParticle = [&](const FString& Kind, const FString& Path)
		{
			if (!Tre->FileExists(Path) && !Missing.Contains(Path.ToLower()))
			{
				Missing.Add(Path.ToLower());
				++MissingParticles;
				UE_LOG(LogTemp, Warning, TEXT("[SWGEmu.Fx.PropObjects] %s: %s %s does not exist"), *CdfPath, *Kind, *Path);
			}
		};

		for (const FSWGClientDataHardpointObject& Object : Data.HardpointObjects)
		{
			if (Object.AppearancePath.EndsWith(TEXT(".prt")))
			{
				++GroundParticles;
				CheckParticle(TEXT("HOBJ"), Object.AppearancePath);
				TestFalse(*(CdfPath + TEXT(" HOBJ particle names a hardpoint")), Object.Hardpoint.IsEmpty());
			}
			else
			{
				++ShipObjects;
			}
		}

		for (const FSWGClientDataLight& Light : Data.Lights)
		{
			TestTrue(*(CdfPath + TEXT(" LOBJ type is 1 or 2")), Light.Type == 1 || Light.Type == 2);
			TestTrue(*(CdfPath + TEXT(" light offset is above the ground")), Light.Offset.Y >= 0.f && Light.Offset.Y < 10.f);
			TestTrue(*(CdfPath + TEXT(" light range is sane")), Light.Range >= 0.f && Light.Range <= 100.f);
			if (Light.Type == 1)
			{
				Glows += Light.EffectPath.IsEmpty() ? 0 : 1;
			}
			else
			{
				++PointLights;
				TestTrue(*(CdfPath + TEXT(" type 2 has no glow")), Light.EffectPath.IsEmpty());
				TestTrue(*(CdfPath + TEXT(" type 2 has a second range")), Light.SecondRange > Light.Range);
			}
			if (!Light.EffectPath.IsEmpty())
			{
				CheckParticle(TEXT("LOBJ"), Light.EffectPath);
			}
			NightOnly += Light.bNightOnly ? 1 : 0;
		}

		for (const FSWGClientDataChild& Child : Data.Children)
		{
			if (Child.Path.EndsWith(TEXT(".prt")))
			{
				++ChildParticles;
				CheckParticle(TEXT("CHLD"), Child.Path);
			}
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.PropObjects] ground HOBJ particles %d (ship/other HOBJ %d), glows %d, type 2 lights %d, night-only lights %d, CHLD particles %d, %d missing particle files"),
		GroundParticles, ShipObjects, Glows, PointLights, NightOnly, ChildParticles, MissingParticles);
	TestTrue(TEXT("ground particles found"), GroundParticles > 0);
	TestEqual(TEXT("type 2 lights"), PointLights, 7);
	TestTrue(TEXT("streetlamps are night-only"), NightOnly > 20);
	TestEqual(TEXT("every referenced particle exists"), MissingParticles, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGPropChildrenTest,
	"SWGEmu.Fx.PropChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGPropChildrenTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForPropAttachmentsTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// Does a snapshot already place what a template's CHLD/CHL2 would? Counts nodes with cdf children that also carry
	// non-cell snapshot children, and snapshot nodes standing exactly where a CHL2 child would be.
	TMap<FString, TSharedPtr<FSWGClientDataFile>> CdfByTemplate;
	auto FindData = [&](const FString& TemplatePath) -> TSharedPtr<FSWGClientDataFile>
	{
		if (const TSharedPtr<FSWGClientDataFile>* Cached = CdfByTemplate.Find(TemplatePath))
		{
			return *Cached;
		}
		TSharedPtr<FSWGClientDataFile> Result;
		FString CdfPath;
		if (Tre->FindTemplateFormStringParam(TemplatePath, SWG_IFF_TAG('S', 'H', 'O', 'T'), TEXT("clientDataFile"), CdfPath) && !CdfPath.IsEmpty())
		{
			TSharedPtr<FSWGClientDataFile> Data = MakeShared<FSWGClientDataFile>();
			if (FSWGClientDataFileReader::Read(Tre->CreateIffReader(CdfPath), *Data) && !Data->Children.IsEmpty())
			{
				Result = Data;
			}
		}
		CdfByTemplate.Add(TemplatePath, Result);
		return Result;
	};

	int32 NodesWithChildren = 0, WithSnapshotChildren = 0, ExactDuplicates = 0, Snapshots = 0;
	for (const FString& SnapshotPath : FindPropFilesWithExtension(*Tre, TEXT(".ws")))
	{
		if (!SnapshotPath.StartsWith(TEXT("snapshot/")))
		{
			continue;
		}
		FSWGWorldSnapshotData Snapshot;
		if (!FSWGWorldSnapshotReader::ReadWorldSnapshot(Tre->CreateIffReader(SnapshotPath), Snapshot))
		{
			continue;
		}
		++Snapshots;

		TArray<const FSWGWorldSnapshotNode*> AllNodes;
		TFunction<void(const FSWGWorldSnapshotNode&)> Collect = [&](const FSWGWorldSnapshotNode& Node)
		{
			AllNodes.Add(&Node);
			for (const FSWGWorldSnapshotNode& Child : Node.Children) { Collect(Child); }
		};
		for (const FSWGWorldSnapshotNode& Node : Snapshot.Nodes) { Collect(Node); }

		for (const FSWGWorldSnapshotNode* Node : AllNodes)
		{
			if (!Snapshot.ObjectTemplateNames.IsValidIndex((int32)Node->NameID))
			{
				continue;
			}
			const TSharedPtr<FSWGClientDataFile> Data = FindData(Snapshot.ObjectTemplateNames[(int32)Node->NameID]);
			if (!Data.IsValid())
			{
				continue;
			}
			++NodesWithChildren;

			bool bHasNonCellChild = false;
			for (const FSWGWorldSnapshotNode& Child : Node->Children)
			{
				const FString& ChildTemplate = Snapshot.ObjectTemplateNames.IsValidIndex((int32)Child.NameID) ? Snapshot.ObjectTemplateNames[(int32)Child.NameID] : FString();
				bHasNonCellChild |= !ChildTemplate.Contains(TEXT("/cell/"));
			}
			WithSnapshotChildren += bHasNonCellChild ? 1 : 0;

			// Any snapshot object of a CHL2 template within a metre of the parent's origin-relative spot (rotation ignored: a cheap bound).
			for (const FSWGClientDataChild& Child : Data->Children)
			{
				if (!Child.bObjectTemplate)
				{
					continue;
				}
				for (const FSWGWorldSnapshotNode* Other : AllNodes)
				{
					if (Other != Node && Snapshot.ObjectTemplateNames.IsValidIndex((int32)Other->NameID) && Snapshot.ObjectTemplateNames[(int32)Other->NameID] == Child.Path
						&& FVector::Dist(Other->Position, Node->Position) <= FVector(Child.Offset.X, Child.Offset.Z, Child.Offset.Y).Size() + 1.0)
					{
						++ExactDuplicates;
						break;
					}
				}
			}
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.PropChildren] %d snapshot(s): %d placed nodes whose cdf has CHLD/CHL2, %d of them with non-cell snapshot children, %d CHL2 children already present in the snapshot"),
		Snapshots, NodesWithChildren, WithSnapshotChildren, ExactDuplicates);
	TestEqual(TEXT("no snapshot node duplicates a cdf CHL2 child"), ExactDuplicates, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
