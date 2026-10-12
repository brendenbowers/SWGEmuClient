#include "Misc/AutomationTest.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGAppearanceCollisionReader.h"
#include "TRE/SWGFloorReader.h"
#include "TRE/SWGPobReader.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGAppearanceCollisionTest,
	"SWGEmu.Tre.Appearance.Collision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGAppearanceCollisionTest::RunTest(const FString& Parameters)
{
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
		AddError(TEXT("no .tre archives loaded — check TreDirectory in DefaultGame.ini"));
		return false;
	}

	FSWGFloorData StarportFloor;
	if (!TestTrue(TEXT("starport interior FORM 0005 floor parses"), FSWGFloorReader::ReadFloor(
		Tre->CreateIffReader(TEXT("appearance/collision/mun_all_landing_pad_s01_r1_collision_floor.flr")), StarportFloor))) return false;
	TestEqual(TEXT("starport floor vertices"), StarportFloor.Vertices.Num(), 12);
	TestEqual(TEXT("starport floor triangles"), StarportFloor.Triangles.Num(), 10);

	// Only uncrossable edges become physical walls. Core3 keeps blocking
	// edges separate; walling those off fences parts of ramps and door sills.
	FSWGFloorData EdgeFloor;
	EdgeFloor.Vertices = { FVector(0, 0, 0), FVector(100, 0, 0), FVector(0, 100, 0) };
	FSWGFloorTriangle& EdgeTriangle = EdgeFloor.Triangles.AddDefaulted_GetRef();
	EdgeTriangle.CornerIndex1 = 0;
	EdgeTriangle.CornerIndex2 = 1;
	EdgeTriangle.CornerIndex3 = 2;
	EdgeTriangle.EdgeType1 = (uint8)ESWGFloorEdgeType::Uncrossable;
	EdgeTriangle.EdgeType2 = (uint8)ESWGFloorEdgeType::Blocking;
	EdgeTriangle.EdgeType3 = (uint8)ESWGFloorEdgeType::Crossable;
	TArray<FVector> BarrierVertices;
	TArray<int32> BarrierIndices;
	TestEqual(TEXT("only uncrossable edges have barriers"), FSWGFloorReader::AppendBarrierMesh(EdgeFloor, 300.0f, BarrierVertices, BarrierIndices), 1);
	FSWGFloorData HouseEntryFloor;
	if (TestTrue(TEXT("house entry floor parses"), FSWGFloorReader::ReadFloor(
		Tre->CreateIffReader(TEXT("appearance/collision/ply_tato_house_sm_s01_fp1_r0_mesh_collision_floor0.flr")), HouseEntryFloor)))
	{
		int32 BlockingEdges = 0;
		for (const FSWGFloorTriangle& Triangle : HouseEntryFloor.Triangles)
		{
			BlockingEdges += Triangle.EdgeType1 == (uint8)ESWGFloorEdgeType::Blocking;
			BlockingEdges += Triangle.EdgeType2 == (uint8)ESWGFloorEdgeType::Blocking;
			BlockingEdges += Triangle.EdgeType3 == (uint8)ESWGFloorEdgeType::Blocking;
		}
		TestTrue(TEXT("house entry has blocking edges that stay open"), BlockingEdges > 0);
	}

	// Bridge: bounding box, a 7-box composite, and a walkable floor (hex-dumped 2026-09-13).
	FSWGAppearanceCollision Bridge;
	TestTrue(TEXT("bridge .lod parses"), FSWGAppearanceCollisionReader::Read(Tre->CreateIffReader(TEXT("appearance/lod/thm_tato_imprv_bridge_sml_s01.lod")), Bridge));
	TestTrue(TEXT("bridge bounding extent"), Bridge.BoundingExtent.IsSet() && Bridge.BoundingExtent->Type == ESWGCollisionExtentType::Box);
	TestTrue(TEXT("bridge collision extent is a composite"), Bridge.HasCollisionExtent() && Bridge.CollisionExtent->Type == ESWGCollisionExtentType::Composite);
	if (Bridge.HasCollisionExtent())
	{
		TestEqual(TEXT("bridge composite boxes"), Bridge.CollisionExtent->Children.Num(), 7);
	}
	TestEqual(TEXT("bridge floor"), Bridge.FloorPath, FString(TEXT("appearance/collision/thm_tato_imprv_bridge_sml_s01_collision_floor.flr")));
	if (Bridge.BoundingExtent.IsSet())
	{
		// BOX max (2.749, 1.520, 4.249) / min (-2.749, -4.124, -4.249) in SWG (x, y-up, z)
		// -> UE (z, x, y) x100: X +/-424.9, Y +/-274.9, Z 152.0 / -412.4.
		TestTrue(TEXT("bridge box X"), FMath::IsNearlyEqual(Bridge.BoundingExtent->Box.Max.X, 424.9, 0.5));
		TestTrue(TEXT("bridge box Y"), FMath::IsNearlyEqual(Bridge.BoundingExtent->Box.Max.Y, 274.9, 0.5));
		TestTrue(TEXT("bridge box Z max"), FMath::IsNearlyEqual(Bridge.BoundingExtent->Box.Max.Z, 152.0, 0.5));
		TestTrue(TEXT("bridge box Z min"), FMath::IsNearlyEqual(Bridge.BoundingExtent->Box.Min.Z, -412.4, 0.5));
	}

	// The bridge floor's edge flags: the deck sides must be uncrossable (rails),
	// the two ends crossable (you step on from the terrain).
	FSWGFloorData BridgeFloor;
	if (TestTrue(TEXT("bridge floor parses"), FSWGFloorReader::ReadFloor(Tre->CreateIffReader(Bridge.FloorPath), BridgeFloor)))
	{
		int32 EdgeCounts[3] = { 0, 0, 0 };
		FBox FloorBounds(ForceInit);
		for (const FVector& Vertex : BridgeFloor.Vertices) { FloorBounds += Vertex; }
		int32 UncrossableAtEnds = 0, CrossableAtEnds = 0;
		for (const FSWGFloorTriangle& Triangle : BridgeFloor.Triangles)
		{
			const int32 Corners[3] = { Triangle.CornerIndex1, Triangle.CornerIndex2, Triangle.CornerIndex3 };
			const uint8 Types[3] = { Triangle.EdgeType1, Triangle.EdgeType2, Triangle.EdgeType3 };
			for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
			{
				if (Types[EdgeIndex] < 3) { ++EdgeCounts[Types[EdgeIndex]]; }
				// An edge at the deck's far X extent is an end; the bridge runs along SWG z, UE X.
				const FVector& Start = BridgeFloor.Vertices[Corners[EdgeIndex]];
				const FVector& End = BridgeFloor.Vertices[Corners[(EdgeIndex + 1) % 3]];
				const bool bAtEnd = (FMath::Abs(Start.X - FloorBounds.Min.X) < 5.0 && FMath::Abs(End.X - FloorBounds.Min.X) < 5.0)
					|| (FMath::Abs(Start.X - FloorBounds.Max.X) < 5.0 && FMath::Abs(End.X - FloorBounds.Max.X) < 5.0);
				if (bAtEnd)
				{
					(Types[EdgeIndex] == 1 ? CrossableAtEnds : UncrossableAtEnds)++;
				}
			}
		}
		AddInfo(FString::Printf(TEXT("bridge floor edges: %d uncrossable, %d crossable, %d blocking; ends: %d crossable, %d uncrossable; bounds %s"),
			EdgeCounts[0], EdgeCounts[1], EdgeCounts[2], CrossableAtEnds, UncrossableAtEnds, *FloorBounds.ToString()));
		TestTrue(TEXT("bridge ends are crossable"), CrossableAtEnds > 0 && UncrossableAtEnds == 0);
	}

	// A room: the cantina foyer's floor. Its wall edges must be uncrossable
	// and its two doorways (to the exterior and to foyer2) crossable with a
	// portal id — that is where the room's collision comes from.
	FSWGPobData CantinaPob;
	if (TestTrue(TEXT("cantina pob parses"), FSWGPobReader::ReadPob(Tre->CreateIffReader(TEXT("appearance/thm_tato_cantina.pob")), CantinaPob)) && CantinaPob.Cells.Num() > 1)
	{
		const FSWGPobCell& Foyer = CantinaPob.Cells[1];
		FSWGFloorData FoyerFloor;
		if (TestTrue(TEXT("foyer floor parses"), FSWGFloorReader::ReadFloor(Tre->CreateIffReader(Foyer.CollisionFloorPath), FoyerFloor)))
		{
			int32 EdgeCounts[3] = { 0, 0, 0 };
			int32 PortalEdges = 0, PortalEdgesCrossable = 0, BoundaryEdges = 0;
			for (const FSWGFloorTriangle& Triangle : FoyerFloor.Triangles)
			{
				const uint8 Types[3] = { Triangle.EdgeType1, Triangle.EdgeType2, Triangle.EdgeType3 };
				const int32 Neighbours[3] = { Triangle.NeighborIndex1, Triangle.NeighborIndex2, Triangle.NeighborIndex3 };
				const int32 Portals[3] = { Triangle.PortalId1, Triangle.PortalId2, Triangle.PortalId3 };
				for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
				{
					if (Types[EdgeIndex] < 3) { ++EdgeCounts[Types[EdgeIndex]]; }
					if (Neighbours[EdgeIndex] < 0) { ++BoundaryEdges; }
					if (Portals[EdgeIndex] >= 0)
					{
						++PortalEdges;
						PortalEdgesCrossable += Types[EdgeIndex] == 1 ? 1 : 0;
					}
				}
			}
			AddInfo(FString::Printf(TEXT("foyer floor %s: %d tris; edges %d uncrossable, %d crossable, %d blocking; %d boundary edges; %d portal edges (%d crossable)"),
				*Foyer.CollisionFloorPath, FoyerFloor.Triangles.Num(), EdgeCounts[0], EdgeCounts[1], EdgeCounts[2], BoundaryEdges, PortalEdges, PortalEdgesCrossable));
			TestTrue(TEXT("foyer has wall edges"), EdgeCounts[0] > 0);
			TestTrue(TEXT("foyer has portal edges"), PortalEdges > 0);
			TestEqual(TEXT("foyer portal edges are crossable"), PortalEdgesCrossable, PortalEdges);
		}
	}

	// Tree: a single cylinder trunk, no floor.
	FSWGAppearanceCollision Tree;
	TestTrue(TEXT("tree .lod parses"), FSWGAppearanceCollisionReader::Read(Tre->CreateIffReader(TEXT("appearance/lod/core_decd_kenalpatree_a1.lod")), Tree));
	TestTrue(TEXT("tree collision is a cylinder"), Tree.HasCollisionExtent() && Tree.CollisionExtent->Type == ESWGCollisionExtentType::Cylinder);
	if (Tree.HasCollisionExtent())
	{
		TestTrue(TEXT("tree radius ~1.97 m"), FMath::IsNearlyEqual(Tree.CollisionExtent->Radius, 196.7f, 1.0f));
		TestTrue(TEXT("tree height ~12.96 m"), FMath::IsNearlyEqual(Tree.CollisionExtent->Height, 1296.0f, 2.0f));
	}
	TestTrue(TEXT("tree has no floor"), Tree.FloorPath.IsEmpty());

	// Table: composite holding a mesh, a cylinder and boxes.
	FSWGAppearanceCollision Table;
	TestTrue(TEXT("table .lod parses"), FSWGAppearanceCollisionReader::Read(Tre->CreateIffReader(TEXT("appearance/lod/frn_impl_table_giant_s01.lod")), Table));
	if (TestTrue(TEXT("table collision is a composite"), Table.HasCollisionExtent() && Table.CollisionExtent->Type == ESWGCollisionExtentType::Composite))
	{
		const TArray<FSWGCollisionExtent>& Parts = Table.CollisionExtent->Children;
		TestEqual(TEXT("table parts"), Parts.Num(), 13);
		TestTrue(TEXT("table first part is a mesh"), Parts.Num() > 0 && Parts[0].Type == ESWGCollisionExtentType::Mesh && Parts[0].Vertices.Num() == 102 && Parts[0].Indices.Num() == 387);
	}

	// Console: detail extent whose finest child is a composite of cylinders.
	FSWGAppearanceCollision Console;
	TestTrue(TEXT("console .lod parses"), FSWGAppearanceCollisionReader::Read(Tre->CreateIffReader(TEXT("appearance/lod/frn_tech_command_console_s01.lod")), Console));
	if (TestTrue(TEXT("console collision is a detail extent"), Console.HasCollisionExtent() && Console.CollisionExtent->Type == ESWGCollisionExtentType::Detail))
	{
		const TArray<FSWGCollisionExtent>& Levels = Console.CollisionExtent->Children;
		TestEqual(TEXT("console detail levels"), Levels.Num(), 2);
		TestTrue(TEXT("console finest level is 4 cylinders"), Levels.Num() == 2 && Levels[1].Type == ESWGCollisionExtentType::Composite && Levels[1].Children.Num() == 4);
	}

	// Building exterior: NULL collision, no floor — only the bounding extent.
	FSWGAppearanceCollision Cantina;
	TestTrue(TEXT("cantina .lod parses"), FSWGAppearanceCollisionReader::Read(Tre->CreateIffReader(TEXT("appearance/lod/thm_tato_cantina_r0_mesh_r0.lod")), Cantina));
	TestFalse(TEXT("cantina has no collision extent"), Cantina.HasCollisionExtent());
	TestTrue(TEXT("cantina has no floor"), Cantina.FloorPath.IsEmpty());

	// The collision gate, up the DERV chain.
	int32 Flags = -1;
	TestTrue(TEXT("bridge block flags found"), Tre->FindTemplateIntParam(TEXT("object/static/structure/tatooine/shared_bridge_tatooine_small_style_01.iff"), TEXT("collisionMaterialBlockFlags"), Flags));
	TestEqual(TEXT("bridge blocks"), Flags, 1);
	TestTrue(TEXT("chair block flags found"), Tre->FindTemplateIntParam(TEXT("object/tangible/camp/shared_camp_lawn_chair.iff"), TEXT("collisionMaterialBlockFlags"), Flags));
	TestEqual(TEXT("chair does not block"), Flags, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
