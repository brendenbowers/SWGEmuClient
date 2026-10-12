#include "Misc/AutomationTest.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "TRE/SWGPobReader.h"
#include "TRE/SWGFloorReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/**
	 * Blocking triangles across a doorway: rays through the portal at knee,
	 * chest and head height along its normal, against the given geometry.
	 * The yardstick the cut is judged by — a player walking the doorway's
	 * centreline is stopped by exactly these.
	 */
	int32 CountDoorwayHits(const TArray<FVector>& Vertices, const TArray<int32>& Indices, const FSWGPobPortalRef& Portal)
	{
		if (Portal.OpeningVertices.Num() < 3)
		{
			return 0;
		}
		FVector Origin = FVector::ZeroVector, Normal = FVector::ZeroVector;
		FBox Bounds(ForceInit);
		const int32 Count = Portal.OpeningVertices.Num();
		for (int32 VertexIndex = 0; VertexIndex < Count; ++VertexIndex)
		{
			const FVector& EdgeStart = Portal.OpeningVertices[VertexIndex];
			const FVector& EdgeEnd = Portal.OpeningVertices[(VertexIndex + 1) % Count];
			Origin += EdgeStart;
			Bounds += EdgeStart;
			Normal += FVector(
				(EdgeStart.Y - EdgeEnd.Y) * (EdgeStart.Z + EdgeEnd.Z),
				(EdgeStart.Z - EdgeEnd.Z) * (EdgeStart.X + EdgeEnd.X),
				(EdgeStart.X - EdgeEnd.X) * (EdgeStart.Y + EdgeEnd.Y));
		}
		Origin /= Count;
		if (!Normal.Normalize())
		{
			return 0;
		}

		int32 Hits = 0;
		for (const float Height : { 50.0f, 120.0f, 170.0f })
		{
			FVector RayOrigin = Origin;
			RayOrigin.Z = Bounds.Min.Z + Height;
			if (RayOrigin.Z > Bounds.Max.Z - 10.0f)
			{
				continue;
			}
			const FVector RayStart = RayOrigin - Normal * 150.0f;
			const FVector RayEnd = RayOrigin + Normal * 150.0f;
			for (int32 TriangleStart = 0; TriangleStart + 2 < Indices.Num(); TriangleStart += 3)
			{
				FVector HitPoint, HitNormal;
				if (FMath::SegmentTriangleIntersection(RayStart, RayEnd,
					Vertices[Indices[TriangleStart]], Vertices[Indices[TriangleStart + 1]], Vertices[Indices[TriangleStart + 2]], HitPoint, HitNormal))
				{
					++Hits;
				}
			}
		}
		return Hits;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGPortalCutTest,
	"SWGEmu.Tre.Pob.PortalCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGPortalCutTest::RunTest(const FString& Parameters)
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
		AddError(TEXT("no .tre archives loaded"));
		return false;
	}

	// The cloning facility: one facade-sized triangle closes the door. Its
	// shell must lose that doorway and nothing else of note.
	FSWGPobData Cloning;
	if (TestTrue(TEXT("cloning pob parses"), FSWGPobReader::ReadPob(Tre->CreateIffReader(TEXT("appearance/mun_tato_cloning_s01.pob")), Cloning)) && !Cloning.Cells.IsEmpty())
	{
		const FSWGPobCell& Shell = Cloning.Cells[0];
		TestEqual(TEXT("cloning doorway blocked before"), CountDoorwayHits(Shell.CollisionVertices, Shell.CollisionIndices, Shell.Portals[0]), 3);

		TArray<FVector> Vertices;
		TArray<int32> Indices;
		int32 Cut = 0;
		FSWGCellSpawnHandler::CutPortalsFromCollision(Shell, Vertices, Indices, Cut);
		TestEqual(TEXT("cloning doorway open after"), CountDoorwayHits(Vertices, Indices, Shell.Portals[0]), 0);
		TestTrue(TEXT("cloning cut touched a triangle"), Cut >= 1);
		TestTrue(TEXT("cloning shell kept its wall"), Indices.Num() / 3 >= Shell.CollisionIndices.Num() / 3);
		AddInfo(FString::Printf(TEXT("cloning shell: %d -> %d triangles, %d clipped"), Shell.CollisionIndices.Num() / 3, Indices.Num() / 3, Cut));

		// What is left near the doorway at ground level after the cut, and where the walkable floor is.
		FBox PortalBounds(ForceInit);
		for (const FVector& Vertex : Shell.Portals[0].OpeningVertices) { PortalBounds += Vertex; }
		const FBox DoorFoot = FBox(FVector(PortalBounds.Min.X - 50.0, PortalBounds.Min.Y - 250.0, -200.0), FVector(PortalBounds.Max.X + 50.0, PortalBounds.Max.Y + 250.0, 120.0));
		for (int32 TriangleStart = 0; TriangleStart + 2 < Indices.Num(); TriangleStart += 3)
		{
			const FVector& CornerA = Vertices[Indices[TriangleStart]];
			const FVector& CornerB = Vertices[Indices[TriangleStart + 1]];
			const FVector& CornerC = Vertices[Indices[TriangleStart + 2]];
			if (DoorFoot.IsInside(CornerA) || DoorFoot.IsInside(CornerB) || DoorFoot.IsInside(CornerC))
			{
				AddInfo(FString::Printf(TEXT("cloning near-door tri %s | %s | %s"), *CornerA.ToCompactString(), *CornerB.ToCompactString(), *CornerC.ToCompactString()));
			}
		}
		FSWGFloorData Floor;
		FSWGIffReader FloorReader = Tre->CreateIffReader(Shell.CollisionFloorPath);
		if (FloorReader.IsValid() && FSWGFloorReader::ReadFloor(FloorReader, Floor))
		{
			FBox FloorBounds(ForceInit);
			for (const FVector& Vertex : Floor.Vertices) { FloorBounds += Vertex; }
			AddInfo(FString::Printf(TEXT("cloning exterior floor %s: %d verts %d tris bounds %s"), *Shell.CollisionFloorPath, Floor.Vertices.Num(), Floor.Triangles.Num(), *FloorBounds.ToString()));
			// The walkway's edges: the ones onto the terrain (its outer rim) must be
			// crossable or the plinth fences itself; the doorway edge is a portal.
			int32 RimUncrossable = 0, RimCrossable = 0, Uncrossable = 0;
			for (const FSWGFloorTriangle& Triangle : Floor.Triangles)
			{
				const int32 Corners[3] = { Triangle.CornerIndex1, Triangle.CornerIndex2, Triangle.CornerIndex3 };
				const uint8 Types[3] = { Triangle.EdgeType1, Triangle.EdgeType2, Triangle.EdgeType3 };
				const int32 Neighbours[3] = { Triangle.NeighborIndex1, Triangle.NeighborIndex2, Triangle.NeighborIndex3 };
				for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
				{
					Uncrossable += Types[EdgeIndex] == 0 ? 1 : 0;
					if (Neighbours[EdgeIndex] >= 0) { continue; }
					const FVector& Start = Floor.Vertices[Corners[EdgeIndex]];
					const FVector& End = Floor.Vertices[Corners[(EdgeIndex + 1) % 3]];
					// Rim = both ends on the floor's lowest level (ground contact).
					if (Start.Z < 0.0 && End.Z < 0.0)
					{
						(Types[EdgeIndex] == 0 ? RimUncrossable : RimCrossable)++;
						AddInfo(FString::Printf(TEXT("cloning walkway rim edge type %d: %s -> %s"), Types[EdgeIndex], *Start.ToCompactString(), *End.ToCompactString()));
					}
				}
			}
			AddInfo(FString::Printf(TEXT("cloning walkway: %d uncrossable edges total; ground rim %d crossable, %d uncrossable"), Uncrossable, RimCrossable, RimUncrossable));
		}
		else
		{
			AddInfo(FString::Printf(TEXT("cloning exterior floor '%s' did not load"), *Shell.CollisionFloorPath));
		}
	}

	// A shell whose doorway is already open must keep it open.
	FSWGPobData Cantina;
	if (TestTrue(TEXT("cantina parses"), FSWGPobReader::ReadPob(Tre->CreateIffReader(TEXT("appearance/thm_tato_cantina.pob")), Cantina)) && !Cantina.Cells.IsEmpty())
	{
		TArray<FVector> Vertices;
		TArray<int32> Indices;
		int32 Cut = 0;
		FSWGCellSpawnHandler::CutPortalsFromCollision(Cantina.Cells[0], Vertices, Indices, Cut);
		// Its door tunnel is a few cm narrower than the portal quad, so the
		// facade face beside each jamb loses an 8-11 cm sliver — harmless, the
		// jamb behind still blocks. What matters is the way through stays open.
		for (const FSWGPobPortalRef& Portal : Cantina.Cells[0].Portals)
		{
			TestEqual(TEXT("cantina doorway open before"), CountDoorwayHits(Cantina.Cells[0].CollisionVertices, Cantina.Cells[0].CollisionIndices, Portal), 0);
			TestEqual(TEXT("cantina doorway open after"), CountDoorwayHits(Vertices, Indices, Portal), 0);
		}
		TestTrue(TEXT("cantina shell only nicked"), Cut <= 8);
	}

	// The Tatooine small house has a downward-facing CMSH sheet five cm
	// above the portal sill, extending across the ramp. Its walkable .flr
	// is separate, so the shell sheet must not obstruct the entrance.
	FSWGPobData SmallHouse;
	if (TestTrue(TEXT("small house parses"), FSWGPobReader::ReadPob(
		Tre->CreateIffReader(TEXT("appearance/ply_tato_house_sm_s01_fp1.pob")), SmallHouse)) && !SmallHouse.Cells.IsEmpty())
	{
		const FSWGPobCell& Shell = SmallHouse.Cells[0];
		TArray<FVector> Vertices;
		TArray<int32> Indices;
		int32 Cut = 0;
		FSWGCellSpawnHandler::CutPortalsFromCollision(Shell, Vertices, Indices, Cut);
		const FSWGPobPortalRef& Portal = Shell.Portals[0];
		FBox Bounds(ForceInit);
		for (const FVector& Vertex : Portal.OpeningVertices) { Bounds += Vertex; }
		const FVector Normal = FVector::CrossProduct(Portal.OpeningVertices[1] - Portal.OpeningVertices[0],
			Portal.OpeningVertices[2] - Portal.OpeningVertices[0]).GetSafeNormal();
		const FVector RayStart(Bounds.GetCenter().X - Normal.X * 150.0, Bounds.GetCenter().Y - Normal.Y * 150.0, Bounds.Min.Z + 5.0);
		const FVector RayEnd(Bounds.GetCenter().X + Normal.X * 150.0, Bounds.GetCenter().Y + Normal.Y * 150.0, Bounds.Min.Z + 5.0);
		int32 Hits = 0;
		for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
		{
			FVector Hit, HitNormal;
			if (FMath::SegmentTriangleIntersection(RayStart, RayEnd,
				Vertices[Indices[i]], Vertices[Indices[i + 1]], Vertices[Indices[i + 2]], Hit, HitNormal))
			{
				++Hits;
				AddInfo(FString::Printf(TEXT("sill hit %s normal %s tri %s | %s | %s; cut %d"), *Hit.ToCompactString(), *HitNormal.ToCompactString(), *Vertices[Indices[i]].ToCompactString(), *Vertices[Indices[i + 1]].ToCompactString(), *Vertices[Indices[i + 2]].ToCompactString(), Cut));
			}
		}
		TestEqual(TEXT("small house sill is clear"), Hits, 0);
	}
	int32 PobCount = 0, DoorwaysBlockedBefore = 0, DoorwaysBlockedAfter = 0, ShellDoorwaysBlockedAfter = 0, DoorwaysTotal = 0;
	TArray<FString> StillBlocked;
	for (const FString& PobPath : Tre->FindVirtualPaths(TEXT(".pob")))
	{
		if (!PobPath.EndsWith(TEXT(".pob")))
		{
			continue;
		}
		FSWGPobData Data;
		if (!FSWGPobReader::ReadPob(Tre->CreateIffReader(PobPath), Data))
		{
			continue;
		}
		++PobCount;
		for (const FSWGPobCell& Cell : Data.Cells)
		{
			if (Cell.CollisionIndices.IsEmpty())
			{
				continue;
			}
			TArray<FVector> Vertices;
			TArray<int32> Indices;
			int32 Cut = 0;
			FSWGCellSpawnHandler::CutPortalsFromCollision(Cell, Vertices, Indices, Cut);
			for (const FSWGPobPortalRef& Portal : Cell.Portals)
			{
				if (!Portal.bIsPassable || Portal.OpeningVertices.Num() < 3)
				{
					continue;
				}
				++DoorwaysTotal;
				const int32 Before = CountDoorwayHits(Cell.CollisionVertices, Cell.CollisionIndices, Portal);
				const int32 After = CountDoorwayHits(Vertices, Indices, Portal);
				DoorwaysBlockedBefore += Before > 0 ? 1 : 0;
				DoorwaysBlockedAfter += After > 0 ? 1 : 0;
				ShellDoorwaysBlockedAfter += (After > 0 && Cell.CellIndex == 0) ? 1 : 0;
				if (After > 0 && StillBlocked.Num() < 12)
				{
					StillBlocked.Add(FString::Printf(TEXT("%s cell %d '%s' portal %d: %d hit(s) remain (was %d)"), *PobPath, Cell.CellIndex, *Cell.CellName, Portal.PortalNumber, After, Before));
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("corpus: %d pob(s), %d doorway(s) through collision cells — blocked %d before, %d after the cut"), PobCount, DoorwaysTotal, DoorwaysBlockedBefore, DoorwaysBlockedAfter));
	for (const FString& Line : StillBlocked)
	{
		AddInfo(TEXT("   ") + Line);
	}
	// Interior cells may keep a remainder: cave rooms have portal quads far
	// larger than the passage, so a centreline ray meets real rock. Every
	// exterior shell must open, though — that is how you get in at all.
	TestEqual(TEXT("no exterior doorway stays blocked"), ShellDoorwaysBlockedAfter, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
