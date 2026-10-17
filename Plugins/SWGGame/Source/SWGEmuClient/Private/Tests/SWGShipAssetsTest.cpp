#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Subsystems/SWGBaselineHandlerRegistry.h"
#include "Objects/World/SWGShip.h"
#include "TRE/SWGPobReader.h"
#include "TRE/SWGFloorReader.h"
#include "TRE/SWGSpaceTerrainReader.h"
#include "TRE/SWGShaderReader.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGNebulaReader.h"
#include "TRE/SWGCockpitReader.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGShipAssetsTest, "SWG.Space.YachtAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGShipAssetsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("ship spawn handler registered"), FSWGActorSpawnHandlerRegistry::Get().IsRegistered(ASWGShip::StaticClass()));
	TestTrue(TEXT("SHIP baseline handler registered"), FSWGBaselineHandlerRegistry::Get().IsRegistered(ESWGObjectType::SHIP));
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->AddToRoot(); Game->InitializeStandalone();
	UWorld* World = Game->GetWorldContext()->World();
	if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
	ON_SCOPE_EXIT
	{
		Game->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
		World->RemoveFromRoot(); Game->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	if (!Tre) return false;
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives available"), Tre->LoadArchives())) return false;
	int32 TerminalType = 0;
	TestTrue(TEXT("space terminal template type resolves"), Tre->FindTemplateIntParam(TEXT("object/tangible/terminal/shared_terminal_space.iff"), TEXT("gameObjectType"), TerminalType));
	TestEqual(TEXT("retail space terminal type"), TerminalType, 0x400B);
	FSWGShaderData InvisibleShader;
	TestTrue(TEXT("textureless collision shader is a valid shader"), FSWGShaderReader::ReadShader(Tre->CreateIffReader(TEXT("shader/sorosub3000_collide.sht")), InvisibleShader));
	TestTrue(TEXT("collision shader has no textures"), InvisibleShader.Textures.IsEmpty());
	FSWGEffectData InvisibleEffect;
	TestTrue(TEXT("collision effect parses"), FSWGShaderReader::ReadEffect(Tre->CreateIffReader(InvisibleShader.EffectName), InvisibleEffect));
	TestTrue(TEXT("collision effect has implementations but no draw passes"), !InvisibleEffect.Implementations.IsEmpty()
		&& !InvisibleEffect.Implementations.ContainsByPredicate([](const FSWGEffectImplementation& Impl) { return !Impl.Passes.IsEmpty(); }));
	FSWGShaderData GlassShader;
	TestTrue(TEXT("yacht glass shader parses"), FSWGShaderReader::ReadShader(Tre->CreateIffReader(TEXT("shader/ss3000_counter_glass_caes20.sht")), GlassShader));
	FSWGEffectRenderStates GlassState;
	TestTrue(TEXT("yacht glass render states parse"), FSWGShaderReader::ReadEffectRenderStates(Tre->CreateIffReader(GlassShader.EffectName), GlassState));
	TestTrue(TEXT("yacht glass blends without depth writes"), GlassState.bAlphaBlend && !GlassState.bWritesDepth);
	FSWGIffReader Template = Tre->CreateIffReader(TEXT("object/ship/player/shared_player_sorosuub_space_yacht.iff"));
	TestEqual(TEXT("yacht template mapping"), Template.GetRootFormType(), FName(TEXT("SSHP")));
	FString CockpitPath;
	if (!TestTrue(TEXT("yacht cockpit filename resolves"), Tre->FindTemplateFormStringParam(
		TEXT("object/ship/player/shared_player_sorosuub_space_yacht.iff"), SWG_IFF_TAG('S','S','H','P'), TEXT("cockpitFilename"), CockpitPath))) return false;
	TestEqual(TEXT("yacht cockpit configuration path"), CockpitPath, FString(TEXT("cockpit/sorosuub_space_yacht.iff")));
	TArray<uint8> CockpitBytes = Tre->ExtractFile(CockpitPath);
	FSWGIffReader Cockpit(CockpitBytes);
	FSWGCockpitData CameraData;
	if (!TestTrue(TEXT("actual CPIT configuration parses"), FSWGCockpitReader::Read(Cockpit, CameraData))) return false;
	TestTrue(TEXT("POB yacht has no separate cockpit frame appearance"), CameraData.FrameAppearance.IsEmpty());
	TestTrue(TEXT("yacht authored zoom distances are retained"), CameraData.ZoomDistances == TArray<float>{0,35,40,47,54,61});
	TestEqual(TEXT("yacht first-person threshold"), CameraData.FirstPersonThreshold, 35.f);
	TestTrue(TEXT("yacht third-person offset stays native XYZ"), CameraData.NativeThirdPersonOffset.Equals(FVector3f(0,14,-44)));
	TestTrue(TEXT("yacht first-person offset stays native XYZ"), CameraData.NativeFirstPersonOffset.Equals(FVector3f(0,-.5f,0)));
	TestTrue(TEXT("yacht ISPB position stays native XYZ"), CameraData.NativePobPosition.IsSet()
		&& CameraData.NativePobPosition->Equals(FVector3f(-.2f,1.85f,19)));
	FString SelectedCockpitPath;
	if (!TestTrue(TEXT("live YT1300 cockpit filename resolves"), Tre->FindTemplateFormStringParam(
		TEXT("object/ship/player/shared_player_yt1300.iff"), SWG_IFF_TAG('S','S','H','P'), TEXT("cockpitFilename"), SelectedCockpitPath))) return false;
	TestEqual(TEXT("live chassis cockpit asset"), SelectedCockpitPath, FString(TEXT("cockpit/yt1300.iff")));
	FSWGCockpitData SelectedCamera;
	if (!TestTrue(TEXT("live chassis CPIT parses"), FSWGCockpitReader::Read(Tre->CreateIffReader(SelectedCockpitPath), SelectedCamera))) return false;
	TestTrue(TEXT("YT1300 uses the supported empty-frame POB branch"), SelectedCamera.FrameAppearance.IsEmpty()
		&& SelectedCamera.NativePobPosition.IsSet() && SelectedCamera.NativePobPosition->Equals(FVector3f(13,1.4f,10.5f)));
	TestTrue(TEXT("YT1300 zero 1OFF matches the live 120cm eye height"), SelectedCamera.NativeFirstPersonOffset.IsNearlyZero());
	TestTrue(TEXT("YT1300 exterior offset retained in native axes"), SelectedCamera.NativeThirdPersonOffset.Equals(FVector3f(0,16,-24)));
	ASWGShip* SelectedShip = World->SpawnActor<ASWGShip>();
	if (!TestTrue(TEXT("normal live chassis spawn succeeds"), SelectedShip && FSWGActorSpawnHandlerRegistry::Get().TryHandle(*SelectedShip,
		{0, ASWGShip::StaticClass(), TEXT("object/ship/player/shared_player_yt1300.iff")}))) return false;
	TestTrue(TEXT("normal YT1300 spawn retains authored cockpit branch"), SelectedShip->CockpitData.IsSet()
		&& SelectedShip->CockpitData->FrameAppearance.IsEmpty() && SelectedShip->CockpitData->NativeFirstPersonOffset.IsNearlyZero());
	TestEqual(TEXT("live chassis seven rooms plus exterior"), SelectedShip->PortalData.Cells.Num(), 8);
	FSWGIffChunk Root, Version, Offset, ThirdOffset;
	if (!TestTrue(TEXT("cockpit rejection fixtures located"), Cockpit.FindForm(SWG_IFF_TAG('C','P','I','T'), Root)
		&& Cockpit.FindChildForm(Root, SWG_IFF_TAG('0','0','0','0'), Version)
		&& Cockpit.FindChildChunk(Version, SWG_IFF_TAG('1','O','F','F'), Offset)
		&& Cockpit.FindChildChunk(Version, SWG_IFF_TAG('3','O','F','F'), ThirdOffset))) return false;
	TArray<uint8> InvalidCockpit = CockpitBytes;
	const uint32 CockpitNaN = 0x7fc00000;
	FMemory::Memcpy(InvalidCockpit.GetData()+Offset.DataOffset, &CockpitNaN, sizeof(CockpitNaN));
	TestFalse(TEXT("nonfinite camera offset rejected"), FSWGCockpitReader::Read(FSWGIffReader(InvalidCockpit), CameraData));
	InvalidCockpit = CockpitBytes; InvalidCockpit[Version.DataOffset-1] = '1';
	TestFalse(TEXT("unsupported cockpit version rejected"), FSWGCockpitReader::Read(FSWGIffReader(InvalidCockpit), CameraData));
	InvalidCockpit = CockpitBytes; InvalidCockpit[ThirdOffset.DataOffset-8] = '1';
	TestFalse(TEXT("duplicate cockpit offset rejected"), FSWGCockpitReader::Read(FSWGIffReader(InvalidCockpit), CameraData));
	InvalidCockpit = CockpitBytes; InvalidCockpit.SetNum(InvalidCockpit.Num()-1);
	TestFalse(TEXT("truncated cockpit configuration rejected"), FSWGCockpitReader::Read(FSWGIffReader(InvalidCockpit), CameraData));
	TestTrue(TEXT("rejected configuration preserves previous camera data"), CameraData.NativeFirstPersonOffset.Equals(FVector3f(0,-.5f,0))
		&& CameraData.ZoomDistances == TArray<float>{0,35,40,47,54,61});
	ASWGShip* SpawnedShip = World->SpawnActor<ASWGShip>();
	if (!SpawnedShip || !TestTrue(TEXT("normal yacht spawn handler succeeds"), FSWGActorSpawnHandlerRegistry::Get().TryHandle(*SpawnedShip,
		{0, ASWGShip::StaticClass(), TEXT("object/ship/player/shared_player_sorosuub_space_yacht.iff")}))) return false;
	if (TestTrue(TEXT("normal ship spawn loads actual cockpit data"), SpawnedShip->CockpitData.IsSet()))
	{
		TestTrue(TEXT("spawn retains authored yacht POB position"), SpawnedShip->CockpitData->NativePobPosition.IsSet()
			&& SpawnedShip->CockpitData->NativePobPosition->Equals(FVector3f(-.2f,1.85f,19)));
		TestTrue(TEXT("spawn keeps the empty cockpit frame and authored zooms"), SpawnedShip->CockpitData->FrameAppearance.IsEmpty()
			&& SpawnedShip->CockpitData->ZoomDistances == CameraData.ZoomDistances);
	}
	FSWGIffReader Reader = Tre->CreateIffReader(TEXT("appearance/sorosuub_space_yacht.pob"));
	FSWGPobData Pob;
	if (!TestTrue(TEXT("yacht POB parses"), FSWGPobReader::ReadPob(Reader, Pob))) return false;
	TestTrue(TEXT("yacht has walkable rooms"), Pob.Cells.Num() > 10);
	int32 Windows = 0, Floors = 0;
	for (const FSWGPobCell& Cell : Pob.Cells)
	{
		for (const FSWGPobPortalRef& Portal : Cell.Portals)
			if (Cell.CellIndex != 0 && Portal.ConnectingCellIndex == 0 && !Portal.bIsPassable)
			{
				++Windows;
				TestTrue(TEXT("exterior window has opening geometry"), Portal.OpeningVertices.Num() >= 3);
			}
		if (Cell.CellIndex != 0 && !Cell.CollisionFloorPath.IsEmpty())
		{
			FSWGIffReader FloorReader = Tre->CreateIffReader(Cell.CollisionFloorPath);
			FSWGFloorData Floor;
			TestTrue(FString::Printf(TEXT("floor %s parses"), *Cell.CellName), FSWGFloorReader::ReadFloor(FloorReader, Floor));
			float Steepest = 0; int32 PortalWalls = 0, SharedWalls = 0;
			for (const FSWGFloorTriangle& Triangle : Floor.Triangles)
			{
				Steepest = FMath::Max(Steepest, float(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FMath::Abs(Triangle.Normal.GetSafeNormal().Z), 0., 1.)))));
				const uint8 Types[] = {Triangle.EdgeType1, Triangle.EdgeType2, Triangle.EdgeType3};
				const int32 Portals[] = {Triangle.PortalId1, Triangle.PortalId2, Triangle.PortalId3};
				const int32 Neighbors[] = {Triangle.NeighborIndex1, Triangle.NeighborIndex2, Triangle.NeighborIndex3};
				for (int32 Edge = 0; Edge < 3; ++Edge)
				{
					PortalWalls += Types[Edge] == 0 && Portals[Edge] >= 0;
					SharedWalls += Types[Edge] == 0 && Neighbors[Edge] >= 0;
				}
			}
			AddInfo(FString::Printf(TEXT("Floor %s: steepest=%g portalWalls=%d sharedWalls=%d"), *Cell.CellName, Steepest, PortalWalls, SharedWalls));
			++Floors;
		}
	}
	TestEqual(TEXT("bridge, observation deck and private-room windows"), Windows, 3);
	TestTrue(TEXT("interior floor data available"), Floors > 0);
	FSWGSpaceTerrainData Space;
	FSWGDataTableData Nebulas;
	if (!TestTrue(TEXT("actual Tatooine nebula table decodes"), FSWGDataTableReader::ReadDataTable(
		Tre->CreateIffReader(TEXT("datatables/space/nebula/space_tatooine.iff")),Nebulas))) return false;
	TestTrue(TEXT("nebula table contains authored entries"), !Nebulas.Rows.IsEmpty());
	TArray<FSWGNebulaData> NebulaData;
	if (!TestTrue(TEXT("authored nebula visual fields parse"),FSWGNebulaReader::Read(Nebulas,NebulaData)) || NebulaData.IsEmpty()) return false;
	TestTrue(TEXT("nebula position keeps native XYZ"),NebulaData[0].NativePosition.Equals(FVector(-4000,4000,0)));
	TestEqual(TEXT("actual main nebula radius in metres"),NebulaData[0].Radius,4000.f);
	TestTrue(TEXT("actual main nebula tint retained"),NebulaData[0].FacingColor.Equals(FLinearColor(.8f,.8f,.65f,1)));
	TestEqual(TEXT("actual main nebula lightning rate"),NebulaData[0].LightningFrequency,.1f);
	TestEqual(TEXT("actual main nebula lightning duration"),NebulaData[0].LightningDurationMax,2.f);
	TestTrue(TEXT("actual main nebula lightning colours"),NebulaData[0].LightningColor.Equals(FLinearColor(1,.5f,.2f,.2f)) && NebulaData[0].LightningRampColor.Equals(FLinearColor(1,.8f,.2f,.4f)));
	TestEqual(TEXT("actual main nebula lightning appearance"),NebulaData[0].LightningAppearance,FString(TEXT("appearance/pt_nebulae_white_02.ltn")));
	if (NebulaData.Num() > 7) TestEqual(TEXT("a nebula without lightning authored has a zero rate"),NebulaData[7].LightningFrequency,0.f);
	const int32 LightningRateColumn = Nebulas.GetColumnIndex(TEXT("lightningFrequency"));
	const FString LightningRate = Nebulas.Rows[0].Cells[LightningRateColumn];
	for (const TCHAR* Invalid : {TEXT("nan"),TEXT("-1"),TEXT("1000"),TEXT("bad")})
	{
		Nebulas.Rows[0].Cells[LightningRateColumn] = Invalid;
		TestFalse(TEXT("invalid nebula lightning rate rejected"),FSWGNebulaReader::Read(Nebulas,NebulaData));
		TestEqual(TEXT("failure preserves authored lightning data"),NebulaData[0].LightningFrequency,.1f);
	}
	Nebulas.Rows[0].Cells[LightningRateColumn] = LightningRate;
	const int32 RadiusColumn = Nebulas.GetColumnIndex(TEXT("radius"));
	const FString RadiusValue = Nebulas.Rows[0].Cells[RadiusColumn];
	for (const TCHAR* Invalid : {TEXT("nan"),TEXT("-1"),TEXT("0"),TEXT("1e-20"),TEXT("1e20"),TEXT("bad")})
	{
		Nebulas.Rows[0].Cells[RadiusColumn] = Invalid;
		TestFalse(TEXT("invalid nebula radius rejected"),FSWGNebulaReader::Read(Nebulas,NebulaData));
		TestEqual(TEXT("failure preserves authored nebula data"),NebulaData[0].Radius,4000.f);
	}
	Nebulas.Rows[0].Cells[RadiusColumn] = RadiusValue;
	Nebulas.ColumnTypes[RadiusColumn] = TEXT('s');
	TestFalse(TEXT("nebula numeric field with string wire type rejected"),FSWGNebulaReader::Read(Nebulas,NebulaData));
	if (!TestTrue(TEXT("space Tatooine parses"), FSWGSpaceTerrainReader::Read(Tre->CreateIffReader(TEXT("terrain/space_tatooine.trn")), Space))) return false;
	TestEqual(TEXT("space background asset"), Space.Skybox, FString(TEXT("nebula2")));
	TestEqual(TEXT("space directional light count"), Space.Lights.Num(), 2);
	TestEqual(TEXT("space star count"), Space.StarCount, 24000);
	TestEqual(TEXT("space dust count"), Space.DustCount, 750);
	TestEqual(TEXT("space planet count"), Space.Planets.Num(), 4);
	TestEqual(TEXT("space celestial count"), Space.Celestials.Num(), 16);
	if (Space.Planets.Num() == 4)
	{
		TestEqual(TEXT("Tatooine planet appearance"), Space.Planets[0].Appearance, FString(TEXT("appearance/planet_tatooine.pln")));
		TestTrue(TEXT("planet position remains native XYZ"), Space.Planets[0].NativePosition.Equals(FVector(50,-420,0)));
		TestTrue(TEXT("planet rotation remains native yaw/pitch/roll degrees"), Space.Planets[0].NativeAnglesDegrees.Equals(FVector(0,10,-30)));
		TestTrue(TEXT("halo scalar retained separately from planet radius"), FMath::IsNearlyEqual(Space.Planets[0].HaloScale, 2.4f));
	}
	if (Space.Celestials.Num() == 16)
	{
		TestEqual(TEXT("celestial shader"), Space.Celestials[0].Shader, FString(TEXT("shader/cels_star_back.sht")));
		TestTrue(TEXT("celestial size"), FMath::IsNearlyEqual(Space.Celestials[0].Size, 0.5f));
		TestTrue(TEXT("separate glow entry has empty nested glow"), Space.Celestials[0].GlowShader.IsEmpty());
		TestTrue(TEXT("celestial direction retained without angle assumptions"), Space.Celestials[0].NativeDirection.Equals(FVector(-21,-30,-71)));
	}
	TestTrue(TEXT("space ambient red"), FMath::IsNearlyEqual(Space.Ambient.R, 0.35f));
	TestFalse(TEXT("ground terrain rejected by space reader"), FSWGSpaceTerrainReader::Read(Tre->CreateIffReader(TEXT("terrain/tatooine.trn")), Space));
	TestEqual(TEXT("failed parse preserves scene data"), Space.StarCount, 24000);
	TArray<uint8> InvalidSpace = Tre->ExtractFile(TEXT("terrain/space_tatooine.trn"));
	FSWGIffReader ValidReader(InvalidSpace);
	FSWGIffChunk SpaceVersion, ClearForm, ClearChunk;
	if (!TestTrue(TEXT("clear color fixture located"), ValidReader.FindForm(SWG_IFF_TAG('0','0','0','0'), SpaceVersion)
		&& ValidReader.FindChildForm(SpaceVersion, SWG_IFF_TAG('C','L','E','A'), ClearForm)
		&& ValidReader.FindChildChunk(ClearForm, SWG_IFF_TAG('0','0','0','0'), ClearChunk))) return false;
	const uint32 NaN = 0x7fc00000;
	FMemory::Memcpy(InvalidSpace.GetData() + ClearChunk.DataOffset, &NaN, sizeof(NaN));
	TestFalse(TEXT("nonfinite scene color rejected"), FSWGSpaceTerrainReader::Read(FSWGIffReader(InvalidSpace), Space));
	TestEqual(TEXT("invalid scene color preserves state"), Space.StarCount, 24000);
	return true;
}
#endif
