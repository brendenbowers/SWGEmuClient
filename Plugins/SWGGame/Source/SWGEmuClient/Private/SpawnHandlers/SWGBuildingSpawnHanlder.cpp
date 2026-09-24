#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "Objects/World/SWGDoor.h"
#include "Objects/World/SWGStaticProp.h"
#include "TRE/SWGInteriorLayoutReader.h"
#include "TRE/SWGPobReader.h"
#include "TRE/SWGFloorReader.h"
#include "TRE/SWGDoorStyleRow.h"
#include "Engine/DataTable.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Common/SWGLightingChannels.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGInteriorStreamingSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Async/Async.h"

namespace
{
	/**
	 * A mesh request that fails resolves its promise on the worker thread,
	 * and TFuture::Next runs inline there — the actor work these
	 * continuations do (Destroy, attach, components) is game-thread only.
	 */
	TFunction<void(const FSWGMeshGenerationResult&)> OnGameThread(TFunction<void(const FSWGMeshGenerationResult&)> Continuation)
	{
		return [Continuation = MoveTemp(Continuation)](const FSWGMeshGenerationResult& Result)
			{
				if (IsInGameThread())
				{
					Continuation(Result);
					return;
				}
				AsyncTask(ENamedThreads::GameThread, [Continuation, Result]()
					{
						Continuation(Result);
					});
			};
	}

	// One unit of POB light colour in the scene's exposure, before the live
	// swg.RoomLightScale multiplier ASWGCell applies (retail saturates its
	// vertex lighting, so the authored sum reads darker than a linear one).
	constexpr float RoomLightScale = 1.0f;

	// Point lights in the POB fall off as 1/(linear*d), d in metres. Inverse
	// square matched at this distance: I = colour * PointMatchDistance^2 /
	// (linear * PointMatchDistance), in candela once scaled.
	constexpr float PointMatchDistanceMetres = 2.0f;
	constexpr float PointRadiusPerUnitColour = 800.0f; // cm of reach per unit of colour, before clamping
	constexpr float PointRadiusMin = 500.0f;
	constexpr float PointRadiusMax = 2500.0f;

	// UE has no per-room ambient. A shadowless, near-constant point light at
	// the room's centre is the nearest thing: walls, floor and ceiling all
	// face the middle of the room, so N.L is close to 1 where it matters.
	constexpr float AmbientFalloffExponent = 0.5f;
	constexpr float AmbientBoost = 1.0f;

	/**
	 * The room's lights as the POB authored them (Sheet 00 §00.6). The LGHT
	 * colour is already multiplied through: (3.0; 3.0, 2.94, 2.44) is
	 * (1, 0.98, 0.81) at intensity 3, so alpha is ignored. Everything lands
	 * on channel 1, off until ASWGBuilding turns the player's room on.
	 */
	void BuildRoomLights(ASWGCell* CellActor, const FSWGPobCell& CellData)
	{
		USceneComponent* Root = CellActor ? CellActor->GetRootComponent() : nullptr;
		if (!Root)
		{
			return;
		}

		FVector BoundsCenter, BoundsExtent;
		CellActor->GetActorBounds(false, BoundsCenter, BoundsExtent);
		const float CircumRadius = FMath::Max(BoundsExtent.Size(), 300.0f);

		for (const FSWGPobLight& LightData : CellData.Lights)
		{
			const float Strength = FMath::Max3(LightData.DiffuseColor.R, LightData.DiffuseColor.G, LightData.DiffuseColor.B);
			if (Strength <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const FLinearColor Colour = FLinearColor(LightData.DiffuseColor.R, LightData.DiffuseColor.G, LightData.DiffuseColor.B) / Strength;

			ULightComponent* Light = nullptr;
			switch (LightData.Type)
			{
			case ESWGPobLightType::Parallel:
			{
				UDirectionalLightComponent* Directional = NewObject<UDirectionalLightComponent>(CellActor);
				Directional->SetIntensity(RoomLightScale * Strength);
				Directional->SetAtmosphereSunLight(false);
				// Light travels along the POB frame's forward, which the reader
				// maps to UE +X — a directional light's own emit axis.
				Directional->SetRelativeRotation(LightData.Transform.GetRotation());
				Light = Directional;
				break;
			}
			case ESWGPobLightType::Point:
			{
				UPointLightComponent* Point = NewObject<UPointLightComponent>(CellActor);
				const float Linear = FMath::Max(LightData.LinearAttenuation, KINDA_SMALL_NUMBER);
				Point->SetIntensityUnits(ELightUnits::Candelas);
				Point->SetIntensity(RoomLightScale * Strength * PointMatchDistanceMetres / Linear);
				Point->SetAttenuationRadius(FMath::Clamp(Strength * PointRadiusPerUnitColour / Linear, PointRadiusMin, PointRadiusMax));
				Point->SetRelativeLocation(LightData.Transform.GetLocation());
				Light = Point;
				break;
			}
			case ESWGPobLightType::Ambient:
			{
				UPointLightComponent* Ambient = NewObject<UPointLightComponent>(CellActor);
				Ambient->bUseInverseSquaredFalloff = false;
				Ambient->LightFalloffExponent = AmbientFalloffExponent;
				Ambient->SetIntensity(RoomLightScale * Strength * AmbientBoost);
				Ambient->SetAttenuationRadius(CircumRadius * 2.0f);
				Ambient->SetRelativeLocation(Root->GetComponentTransform().InverseTransformPosition(BoundsCenter));
				Light = Ambient;
				break;
			}
			default:
				continue;
			}

			Light->SetLightColor(Colour);
			Light->SetCastShadows(false);
			Light->SetVisibility(false);
			SWGSetInteriorLightingChannel(*Light);
			Light->SetupAttachment(Root);
			Light->RegisterComponent();
			CellActor->AddRoomLight(Light);
		}
	}

	// How far below the building origin a floor must sit before its terrain is
	// cut, in raw units. Rooms at entrance height keep theirs — a hole there is
	// only somewhere to fall through until the cell's floor collision arrives.
	constexpr float TerrainHoleFloorTolerance = 0.5f;

	// Grown onto every hole, in raw units. Sub-quads are kept by their centre,
	// so half of one survives inside the edge without this — exactly where a
	// room's fittings stand. Under any wall's thickness, so it cuts inward.
	constexpr float TerrainHoleMargin = 0.25f;

	/**
	 * One hole per interior cell whose floor sits below the building origin,
	 * placed in raw world space. Cell 0 is excluded — it's the exterior shell,
	 * and cutting to its extent would leave a moat around the outside walls.
	 *
	 * Prefers the .flr walkable floor, falling back to the embedded CMSH when
	 * that's missing. Floor geometry is building-local UE units, so it converts here.
	 */
	void GatherInteriorFloorHoles(TObjectPtr<USWGTreSubsystem> TreSubsystem, const FSWGPobData& PortalData,
		const FVector& ActorRawPosition, float YawRadians, TArray<FSWGTerrainHole>& OutHoles)
	{
		if (!TreSubsystem)
		{
			return;
		}

		const float SinYaw = FMath::Sin(YawRadians);
		const float CosYaw = FMath::Cos(YawRadians);

		for (int32 CellIndex = 1; CellIndex < PortalData.Cells.Num(); ++CellIndex)
		{
			const FSWGPobCell& Cell = PortalData.Cells[CellIndex];

			const TArray<FVector>* Vertices = nullptr;
			const TCHAR* Source = TEXT("none");

			FSWGFloorData FloorData;
			if (!Cell.CollisionFloorPath.IsEmpty())
			{
				FSWGIffReader FloorReader = TreSubsystem->CreateIffReader(Cell.CollisionFloorPath);
				if (FloorReader.IsValid() && FSWGFloorReader::ReadFloor(FloorReader, FloorData) && !FloorData.Vertices.IsEmpty())
				{
					Vertices = &FloorData.Vertices;
					Source = TEXT("flr");
				}
			}

			if (!Vertices && !Cell.CollisionVertices.IsEmpty())
			{
				Vertices = &Cell.CollisionVertices;
				Source = TEXT("cmsh");
			}

			if (!Vertices)
			{
				UE_LOG(LogTemp, Warning, TEXT("TERRAINHOLE   cell[%d] '%s' has no usable floor geometry (floorPath=%s)"),
					CellIndex, *Cell.CellName, *Cell.CollisionFloorPath);
				continue;
			}

			FBox LocalBounds(ForceInit);
			for (const FVector& Vertex : *Vertices)
			{
				LocalBounds += Vertex;
			}

			// Whether to cut is about the walkable floor, so test its Z before
			// the cell mesh widens the box below.
			const bool bBelowOrigin = LocalBounds.Min.Z < -SWGToUnrealSpace(TerrainHoleFloorTolerance);

			// How wide to cut is not: a .flr stops at the edge of what a player
			// can walk on, leaving anything between that and the wall — the
			// cloning facility's tanks — outside. The cell mesh reaches the walls.
			if (Vertices != &Cell.CollisionVertices)
			{
				for (const FVector& Vertex : Cell.CollisionVertices)
				{
					LocalBounds.Min.X = FMath::Min(LocalBounds.Min.X, Vertex.X);
					LocalBounds.Min.Y = FMath::Min(LocalBounds.Min.Y, Vertex.Y);
					LocalBounds.Max.X = FMath::Max(LocalBounds.Max.X, Vertex.X);
					LocalBounds.Max.Y = FMath::Max(LocalBounds.Max.Y, Vertex.Y);
				}
			}

			UE_LOG(LogTemp, Warning, TEXT("TERRAINHOLE   cell[%d] '%s' source=%s lowestZ(UE)=%.2f belowOrigin=%d"),
				CellIndex, *Cell.CellName, Source, LocalBounds.Min.Z, bBelowOrigin ? 1 : 0);

			if (!bBelowOrigin)
			{
				continue;
			}

			// The rectangle stays axis-aligned in the building's frame, so only
			// its centre rotates — FSWGTerrainModifier's LocalToWorld convention.
			const FVector LocalCentreRaw = SWGToRawSpace(LocalBounds.GetCenter());
			const FVector LocalExtentRaw = SWGToRawSpace(LocalBounds.GetExtent());

			FSWGTerrainHole Hole;
			Hole.Center = FVector2D(
				ActorRawPosition.X + LocalCentreRaw.X * CosYaw - LocalCentreRaw.Y * SinYaw,
				ActorRawPosition.Y + LocalCentreRaw.X * SinYaw + LocalCentreRaw.Y * CosYaw);
			Hole.Extents = FVector2D(LocalExtentRaw.X, LocalExtentRaw.Y) + FVector2D(TerrainHoleMargin, TerrainHoleMargin);
			Hole.YawRadians = YawRadians;
			OutHoles.Add(Hole);
		}
	}

	// How far from the portal plane a wall piece inside the opening still
	// counts as the door, in UE units. Interior wall panels stand right on the
	// plane, and a room's far wall can be parallel and close, so rooms keep
	// this tight. Exterior shells lean and step — the cloning facility's
	// facade sits 25-175 cm in front of its portal at door height — and have
	// nothing parallel behind the door for metres, so the shell reaches further.
	constexpr float PortalCutHalfDepthInterior = 100.0f;
	constexpr float PortalCutHalfDepthExterior = 200.0f;

	// |dot| of a triangle's normal with the doorway's above which the triangle
	// counts as lying in the doorway plane (about 45 degrees). Jambs are ~0.
	constexpr float PortalCutParallelDot = 0.7f;

	// How far below a portal's sill the cut reaches, in UE units.
	constexpr float PortalCutSillDrop = 100.0f;

	// Wall barriers raised along a floor's uncrossable edges, in UE units —
	// Core3's BARRIER_HEIGHT. Taller than any character, short of ceilings
	// with a mezzanine above.
	constexpr float FloorBarrierHeight = 300.0f;

	/** A convex polygon in 3D, as produced by clipping a triangle. */
	using FClipPolygon = TArray<FVector, TInlineAllocator<8>>;

	/**
	 * Sutherland-Hodgman against one plane: the part of Polygon on the side
	 * the plane normal points to goes in OutFront, the rest in OutBack.
	 */
	void SplitPolygon(const FClipPolygon& Polygon, const FPlane& Plane, FClipPolygon& OutFront, FClipPolygon& OutBack)
	{
		OutFront.Reset();
		OutBack.Reset();
		const int32 Count = Polygon.Num();
		for (int32 VertexIndex = 0; VertexIndex < Count; ++VertexIndex)
		{
			const FVector& Current = Polygon[VertexIndex];
			const FVector& Next = Polygon[(VertexIndex + 1) % Count];
			const double CurrentDistance = Plane.PlaneDot(Current);
			const double NextDistance = Plane.PlaneDot(Next);

			(CurrentDistance >= 0.0 ? OutFront : OutBack).Add(Current);
			if ((CurrentDistance >= 0.0) != (NextDistance >= 0.0))
			{
				const FVector Crossing = FMath::Lerp(Current, Next, CurrentDistance / (CurrentDistance - NextDistance));
				OutFront.Add(Crossing);
				OutBack.Add(Crossing);
			}
		}
	}

	/** Fan-triangulates a convex polygon onto the output arrays. */
	void EmitPolygon(const FClipPolygon& Polygon, TArray<FVector>& OutVertices, TArray<int32>& OutIndices)
	{
		if (Polygon.Num() < 3)
		{
			return;
		}
		// A clip along an existing edge leaves a zero-width strip; nothing to collide with.
		FVector AreaVector = FVector::ZeroVector;
		for (int32 VertexIndex = 1; VertexIndex + 1 < Polygon.Num(); ++VertexIndex)
		{
			AreaVector += FVector::CrossProduct(Polygon[VertexIndex] - Polygon[0], Polygon[VertexIndex + 1] - Polygon[0]);
		}
		if (AreaVector.Size() < 2.0f) // < 1 cm^2
		{
			return;
		}
		const int32 Base = OutVertices.Num();
		OutVertices.Append(Polygon.GetData(), Polygon.Num());
		for (int32 VertexIndex = 1; VertexIndex + 1 < Polygon.Num(); ++VertexIndex)
		{
			OutIndices.Add(Base);
			OutIndices.Add(Base + VertexIndex);
			OutIndices.Add(Base + VertexIndex + 1);
		}
	}

	/**
	 * The cell's collision geometry with its doorways cut out. Retail's
	 * collision skipped the triangles a portal covers when a mover crossed it;
	 * cutting the openings out of the mesh up front does the same for a
	 * physics mesh. Every triangle lying in a portal's plane (normal within
	 * PortalCutParallelDot, within the cell's half depth of it) is clipped
	 * against the prism through the opening's outline: the part inside the
	 * doorway is discarded, the rest re-triangulated — so a whole-facade
	 * triangle that spans a door (the cloning facility's does) keeps its wall
	 * and loses only the door. Jambs and lintels are perpendicular and untouched.
	 */
	void CutPortalsFromCollisionImpl(const FSWGPobCell& CellData, TArray<FVector>& OutVertices, TArray<int32>& OutIndices, int32& OutCutTriangles)
	{
		OutCutTriangles = 0;
		OutVertices = CellData.CollisionVertices;
		OutIndices.Reset();
		OutIndices.Reserve(CellData.CollisionIndices.Num());

		const float HalfDepth = CellData.CellIndex == 0 ? PortalCutHalfDepthExterior : PortalCutHalfDepthInterior;

		struct FPortalPrism
		{
			FVector Origin;
			FVector Normal;
			/** Outward-facing side planes through each outline edge, plus the margin. */
			TArray<FPlane, TInlineAllocator<8>> SidePlanes;
		};
		TArray<FPortalPrism> Prisms;

		for (const FSWGPobPortalRef& Portal : CellData.Portals)
		{
			if (!Portal.bIsPassable || Portal.OpeningVertices.Num() < 3)
			{
				continue;
			}

			// Newell's method, as ASWGBuilding::GetEntrances — some openings aren't planar.
			FPortalPrism Prism;
			Prism.Origin = FVector::ZeroVector;
			Prism.Normal = FVector::ZeroVector;
			const int32 Count = Portal.OpeningVertices.Num();
			for (int32 VertexIndex = 0; VertexIndex < Count; ++VertexIndex)
			{
				const FVector& EdgeStart = Portal.OpeningVertices[VertexIndex];
				const FVector& EdgeEnd = Portal.OpeningVertices[(VertexIndex + 1) % Count];
				Prism.Origin += EdgeStart;
				Prism.Normal += FVector(
					(EdgeStart.Y - EdgeEnd.Y) * (EdgeStart.Z + EdgeEnd.Z),
					(EdgeStart.Z - EdgeEnd.Z) * (EdgeStart.X + EdgeEnd.X),
					(EdgeStart.X - EdgeEnd.X) * (EdgeStart.Y + EdgeEnd.Y));
			}
			Prism.Origin /= Count;
			if (!Prism.Normal.Normalize())
			{
				continue;
			}

			for (int32 VertexIndex = 0; VertexIndex < Count; ++VertexIndex)
			{
				const FVector& EdgeStart = Portal.OpeningVertices[VertexIndex];
				const FVector& EdgeEnd = Portal.OpeningVertices[(VertexIndex + 1) % Count];
				FVector SideNormal = FVector::CrossProduct(EdgeEnd - EdgeStart, Prism.Normal).GetSafeNormal();
				if (SideNormal.IsNearlyZero())
				{
					continue;
				}
				// Outward: away from the opening's centre.
				if (FVector::DotProduct(SideNormal, Prism.Origin - EdgeStart) > 0.0f)
				{
					SideNormal = -SideNormal;
				}
				// Exactly on the outline for the sides and lintel: any margin
				// shaves slivers off the walls beside the frame. The sill edge is
				// pushed down instead — a shell that leans or steps leaves a
				// strip of itself under the portal's bottom edge (the cloning
				// facility's is 8 cm, on a sill already 8 cm up), and below a
				// door there is only ground or the building's own floor sheet.
				const FVector PlaneBase = SideNormal.Z < -0.7f ? EdgeStart + FVector(0.0f, 0.0f, -PortalCutSillDrop) : EdgeStart;
				Prism.SidePlanes.Add(FPlane(PlaneBase, SideNormal));
			}
			if (Prism.SidePlanes.Num() >= 3)
			{
				Prisms.Add(MoveTemp(Prism));
			}
		}

		if (Prisms.IsEmpty())
		{
			OutIndices = CellData.CollisionIndices;
			return;
		}

		for (int32 TriangleStart = 0; TriangleStart + 2 < CellData.CollisionIndices.Num(); TriangleStart += 3)
		{
			const int32 IndexA = CellData.CollisionIndices[TriangleStart];
			const int32 IndexB = CellData.CollisionIndices[TriangleStart + 1];
			const int32 IndexC = CellData.CollisionIndices[TriangleStart + 2];
			if (!CellData.CollisionVertices.IsValidIndex(IndexA) || !CellData.CollisionVertices.IsValidIndex(IndexB) || !CellData.CollisionVertices.IsValidIndex(IndexC))
			{
				continue;
			}
			const FVector& CornerA = CellData.CollisionVertices[IndexA];
			const FVector& CornerB = CellData.CollisionVertices[IndexB];
			const FVector& CornerC = CellData.CollisionVertices[IndexC];
			const FVector TriangleNormal = FVector::CrossProduct(CornerB - CornerA, CornerC - CornerA).GetSafeNormal();

			// Pieces of this triangle still standing; starts as the whole thing.
			TArray<FClipPolygon, TInlineAllocator<4>> Pieces;
			Pieces.Add({ CornerA, CornerB, CornerC });
			bool bCut = false;

			for (const FPortalPrism& Prism : Prisms)
			{
				if (FMath::Abs(FVector::DotProduct(TriangleNormal, Prism.Normal)) < PortalCutParallelDot)
				{
					continue;
				}

				TArray<FClipPolygon, TInlineAllocator<4>> NextPieces;
				for (const FClipPolygon& Piece : Pieces)
				{
					// Peel the outside off against each side plane; what survives
					// every plane is inside the doorway prism.
					FClipPolygon Inside = Piece;
					for (const FPlane& SidePlane : Prism.SidePlanes)
					{
						FClipPolygon Outside, StillInside;
						SplitPolygon(Inside, SidePlane, Outside, StillInside);
						if (Outside.Num() >= 3)
						{
							NextPieces.Add(MoveTemp(Outside));
						}
						Inside = MoveTemp(StillInside);
						if (Inside.Num() < 3)
						{
							break;
						}
					}

					if (Inside.Num() < 3)
					{
						continue;
					}

					// Inside the outline — but only door if it sits in the wall,
					// not a floor or ceiling slab that happens to pass under the frame.
					FVector InsideCentroid = FVector::ZeroVector;
					for (const FVector& Vertex : Inside) { InsideCentroid += Vertex; }
					InsideCentroid /= Inside.Num();
					if (FMath::Abs(FVector::DotProduct(InsideCentroid - Prism.Origin, Prism.Normal)) <= HalfDepth)
					{
						bCut = true; // the inside piece is dropped
					}
					else
					{
						NextPieces.Add(MoveTemp(Inside));
					}
				}
				Pieces = MoveTemp(NextPieces);
			}

			if (!bCut)
			{
				OutIndices.Add(IndexA);
				OutIndices.Add(IndexB);
				OutIndices.Add(IndexC);
				continue;
			}

			++OutCutTriangles;
			for (const FClipPolygon& Piece : Pieces)
			{
				EmitPolygon(Piece, OutVertices, OutIndices);
			}
		}
	}

	// Owned by neither spawn concept — bakes floor collision from FSWGPobCell
	// data onto Actor (the building itself for the exterior shell, or a cell
	// actor once FSWGCellSpawnHandler::FinishCell builds it).
	void CreateCollisionForCell(TObjectPtr<USWGTreSubsystem> TreSubsystem, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem, AActor* Actor, const FSWGPobCell& CellData)
	{
		// The cell's CMSH is its walls (and for cell 0, the exterior shell) —
		// what stops you walking through a building. The .flr below is only
		// what you stand on. Cell-local like the floor, so attached the same way.
		if (!CellData.CollisionVertices.IsEmpty() && !CellData.CollisionIndices.IsEmpty() && Actor->GetRootComponent())
		{
			TArray<FVector> WallVertices;
			TArray<int32> WallIndices;
			int32 CutTriangles = 0;
			CutPortalsFromCollisionImpl(CellData, WallVertices, WallIndices, CutTriangles);
			if (!WallIndices.IsEmpty())
			{
				// Salted per cut rule: the saved SM_Collision_* asset is the cache,
				// so a change to how doorways are cut must not reuse the old shell.
				const uint32 WallHash = HashCombine(HashCombine(GetTypeHash(CellData.MeshPath), GetTypeHash(CellData.CellIndex)), GetTypeHash(FString(TEXT("portalcut-4"))));
				UStaticMeshComponent* Walls = MeshGeneratorSubsystem->AddCollisionMeshComponent(*Actor, *Actor->GetRootComponent(), WallHash,
					FString::Printf(TEXT("%s [cell %d walls]"), *CellData.MeshPath, CellData.CellIndex), WallVertices, WallIndices);
				UE_LOG(LogTemp, Log, TEXT("CreateCollisionForCell: cell %d '%s' walls — %d tri(s), %d clipped around %d portal(s)%s"),
					CellData.CellIndex, *CellData.CellName, WallIndices.Num() / 3, CutTriangles, CellData.Portals.Num(), Walls ? TEXT("") : TEXT(" — FAILED"));
			}
		}

		FSWGIffReader FloorReader = TreSubsystem->CreateIffReader(CellData.CollisionFloorPath);
		FSWGFloorData FloorData;
		const bool bFloorParsed = FloorReader.IsValid() && FSWGFloorReader::ReadFloor(FloorReader, FloorData);
		if (!bFloorParsed || FloorData.Triangles.IsEmpty())
		{
			if (FloorReader.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("CreateCollisionForCell: failed to parse floor file %s for cell %s"), *CellData.CollisionFloorPath, *CellData.CellName);
			}
			return;
		}

		// Two colliders, salted separately (the saved SM_Collision_* asset is
		// the cache). The floor is one-sided: a doorway's raised sill is a
		// step, and a capsule already nosing under its edge has to be able to
		// step up through it rather than be caught from below. The barriers
		// along every uncrossable edge are walls with no winding to trust and
		// stay double-sided. Portal edges are crossable and get none — the
		// doorway stays open.
		TArray<int32> FloorIndices;
		FSWGFloorReader::AppendFloorTriangles(FloorData, FloorIndices);
		const uint32 FloorHash = HashCombine(GetTypeHash(CellData.CollisionFloorPath), GetTypeHash(FString(TEXT("floor-onesided-2"))));
		UStaticMeshComponent* FloorCollisionComp = MeshGeneratorSubsystem->AddCollisionMeshComponent(*Actor, *Actor->GetRootComponent(), FloorHash,
			CellData.CollisionFloorPath, FloorData.Vertices, FloorIndices, /*bDoubleSided*/ false);

		TArray<FVector> BarrierVertices;
		TArray<int32> BarrierIndices;
		const int32 Barriers = FSWGFloorReader::AppendBarrierMesh(FloorData, FloorBarrierHeight, BarrierVertices, BarrierIndices);
		if (Barriers > 0)
		{
			const uint32 BarrierHash = HashCombine(GetTypeHash(CellData.CollisionFloorPath), GetTypeHash(FString(TEXT("barriers-2"))));
			MeshGeneratorSubsystem->AddCollisionMeshComponent(*Actor, *Actor->GetRootComponent(), BarrierHash,
				CellData.CollisionFloorPath + TEXT(" [barriers]"), BarrierVertices, BarrierIndices);
		}
		UE_LOG(LogTemp, Log, TEXT("CreateCollisionForCell: cell %d '%s' floor %s — %d tri(s), %d wall barrier(s)%s"),
			CellData.CellIndex, *CellData.CellName, *CellData.CollisionFloorPath, FloorData.Triangles.Num(), Barriers, FloorCollisionComp ? TEXT("") : TEXT(" — floor FAILED"));
		if (!FloorCollisionComp)
		{
			return;
		}

		if (ASWGCell* CellActor = Cast<ASWGCell>(Actor))
		{
			FBox FloorBounds(ForceInit);
			for (const FVector& Vert : FloorData.Vertices)
			{
				FloorBounds += Vert;
			}

			if (FloorBounds.IsValid)
			{
				constexpr float ApproxRoomHeight = 300.f; // guess; the floor mesh carries no ceiling height
				FloorBounds.Max.Z = FloorBounds.Min.Z + ApproxRoomHeight;

				UBoxComponent* TriggerComp = NewObject<UBoxComponent>(Actor);
				TriggerComp->SetupAttachment(Actor->GetRootComponent());
				TriggerComp->SetBoxExtent(FloorBounds.GetExtent());
				TriggerComp->SetRelativeLocation(FloorBounds.GetCenter());
				TriggerComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				TriggerComp->SetCollisionObjectType(ECC_WorldDynamic);
				TriggerComp->SetCollisionResponseToAllChannels(ECR_Ignore);
				TriggerComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
				TriggerComp->RegisterComponent();
				CellActor->TriggerVolume = TriggerComp;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("CreateCollisionForCell: cell %s has no floor vertices — no trigger volume built"), *CellData.CellName);
			}
		}
	}
}

void FSWGCellSpawnHandler::CutPortalsFromCollision(const FSWGPobCell& CellData, TArray<FVector>& OutVertices, TArray<int32>& OutIndices, int32& OutCutTriangles)
{
	CutPortalsFromCollisionImpl(CellData, OutVertices, OutIndices, OutCutTriangles);
}

REGISTER_SWG_ACTOR_SPAWN_HANDLER(FSWGBuildingSpawnHandler, ASWGBuilding)
REGISTER_SWG_ACTOR_SPAWN_HANDLER(FSWGCellSpawnHandler, ASWGCell)

void FSWGCellSpawnHandler::SpawnInteriorLayout(ASWGCell* CellActor, ASWGBuilding* BuildingActor, const FSWGPobCell& CellData, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem)
{
	UWorld* World = CellActor ? CellActor->GetWorld() : nullptr;
	if (!World || !MeshGeneratorSubsystem)
	{
		return;
	}

	// Cell-relative, composed against the cell as it stands now (just attached
	// to its building) and left unattached — the cell's root is replaced when
	// its mesh lands. No server identity, so no object graph registration.
	const FTransform CellTransform = CellActor->GetActorTransform();
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FSWGInteriorLayoutNode& Node : BuildingActor->InteriorLayout.Nodes)
	{
		if (Node.CellName != CellData.CellName)
		{
			continue;
		}

		ASWGStaticProp* Prop = World->SpawnActor<ASWGStaticProp>(ASWGStaticProp::StaticClass(), Node.Transform * CellTransform, SpawnParams);
		if (!Prop)
		{
			continue;
		}

		Prop->bInteriorLighting = true;
		CellActor->InteriorActors.Add(Prop);
		MeshGeneratorSubsystem->RequestMeshForTemplatePath(Prop, Node.TemplatePath);
	}
}

TWeakObjectPtr<UDataTable> FSWGCellSpawnHandler::GetDoorStyleTable()
{
	static TWeakObjectPtr<UDataTable> CachedTable;
	if (!CachedTable.IsValid())
	{
		CachedTable = LoadObject<UDataTable>(nullptr, SWGDoorStyle::DataTablePath);
	}
	return CachedTable;
}

bool FSWGCellSpawnHandler::HandleActorSpawn(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo)
{
	ASWGCell* CellActor = Cast<ASWGCell>(&Actor);
	UGameInstance* GameInstance = Actor.GetWorld() ? Actor.GetWorld()->GetGameInstance() : nullptr;
	if (!CellActor || !GameInstance)
	{
		return true;
	}

	USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>();
	if (!ObjectGraph)
	{
		return true;
	}

	CheckAndFinishCell(*ObjectGraph, CellActor->GetObjectId(),
		GameInstance->GetSubsystem<USWGTreSubsystem>(), GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>());

	return true;
}

void FSWGCellSpawnHandler::CheckAndFinishCell(USWGObjectGraphSubsystem& ObjectGraph, int64 ObjectId, TObjectPtr<USWGTreSubsystem> TreSubsystem, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem)
{
	ASWGCell* CellActor = Cast<ASWGCell>(ObjectGraph.FindActor(ObjectId));
	if (!CellActor || CellActor->OwningBuilding.IsValid())
	{
		return;
	}

	const int64* ContainerId = ObjectGraph.FindContainerId(ObjectId);
	const int32* CellNumber = ObjectGraph.FindCellNumber(ObjectId);
	if (!ContainerId || !CellNumber)
	{
		return; // still waiting on containment and/or the TLCS baseline
	}

	if (AActor* ContainerActor = ObjectGraph.FindActor(*ContainerId))
	{
		if (ASWGBuilding* BuildingActor = Cast<ASWGBuilding>(ContainerActor))
		{
			FinishCell(CellActor, BuildingActor, *CellNumber, TreSubsystem, MeshGeneratorSubsystem);
		}
		return;
	}

	// Owning building hasn't spawned yet — finish once it's ready rather than
	// polling.
	TWeakObjectPtr<ASWGCell> WeakCell = CellActor;
	TWeakObjectPtr<USWGObjectGraphSubsystem> WeakObjectGraph = &ObjectGraph;
	const int64 ContainerIdCopy = *ContainerId;
	TSharedPtr<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
	*Handle = ObjectGraph.OnObjectReady.AddLambda([WeakObjectGraph, WeakCell, ContainerIdCopy, Handle, TreSubsystem, MeshGeneratorSubsystem](int64 ReadyObjectId)
		{
			if (ReadyObjectId != ContainerIdCopy || !WeakObjectGraph.IsValid())
			{
				return;
			}

			WeakObjectGraph->OnObjectReady.Remove(*Handle);

			if (ASWGCell* Cell = WeakCell.Get())
			{
				CheckAndFinishCell(*WeakObjectGraph.Get(), Cell->GetObjectId(), TreSubsystem, MeshGeneratorSubsystem);
			}
		});
}

bool FSWGBuildingSpawnHandler::HandleActorSpawn(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo)
{

	UWorld* World = Actor.GetWorld();
	Initialize(World);

	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: Failed to initialize subsystems"));
		return false;
	}


	FString TemplateName = SpawnInfo.TemplateName;
	if (TemplateName.IsEmpty())
	{
		TemplateName = TreSubsystem->ResolveTemplatePath(SpawnInfo.TemplateCrc);
	}
	
	if (TemplateName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: Failed to resolve template name"));
		return false;
	}

	FString PobPath;
	if (!MeshGeneratorSubsystem->ResolvePortalLayoutPath(TemplateName, PobPath))
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: template %s has no portalLayoutFilename in its DERV chain"), *TemplateName);
		return false;
	}

	FSWGIffReader Reader = TreSubsystem->CreateIffReader(PobPath);
	if (!Reader.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: failed to open portal layout file %s"), *PobPath);
		return false;
	}

	ASWGBuilding* BuildingActor = Cast<ASWGBuilding>(&Actor);
	if (!FSWGPobReader::ReadPob(Reader, BuildingActor->PortalData))
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: failed to parse portal layout file %s"), *PobPath);
		return false;
	}

	if (BuildingActor->PortalData.Cells.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: portal layout file %s has no cells"), *PobPath);
		return false;
	}

	// Optional: most housing has none, city buildings have hundreds of pieces.
	FString IlfPath;
	if (MeshGeneratorSubsystem->ResolveInteriorLayoutPath(TemplateName, IlfPath))
	{
		FSWGIffReader IlfReader = TreSubsystem->CreateIffReader(IlfPath);
		if (!IlfReader.IsValid() || !FSWGInteriorLayoutReader::ReadInteriorLayout(IlfReader, BuildingActor->InteriorLayout))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: failed to read interior layout %s for %s"), *IlfPath, *TemplateName);
		}
	}

	// Now that the portal layout is parsed, stamp this building's terrain edit.
	// After ReadPob, not before: only the POB knows which cells to cut under.
	// Rooms below the entrance are handled by removing terrain under them, not by
	// sinking the pad, which put the doorway at the bottom of a pit.
	if (UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
	{
		if (USWGTerrainSubsystem* TerrainSubsystem = GameInstance->GetSubsystem<USWGTerrainSubsystem>())
		{
			// The subsystem works in raw space (the .trn's own units and axes),
			// not final UE actor coordinates — yaw included.
			const FVector ActorLocation = Actor.GetActorLocation();
			const FVector RawPosition = SWGToRawSpace(ActorLocation);
			const float YawRadians = SWGToRawYawRadians(Actor.GetActorRotation().Yaw);

			UE_LOG(LogTemp, Warning, TEXT("TERRAINPAD %s cells=%d actorUE=(%.1f,%.1f,%.1f) padRawZ=%.3f yaw=%.1f"),
				*TemplateName, BuildingActor->PortalData.Cells.Num(),
				ActorLocation.X, ActorLocation.Y, ActorLocation.Z,
				RawPosition.Z, Actor.GetActorRotation().Yaw);

			// Owned by the building's object id so a streamed-out .ws building
			// takes its pad and holes with it (USWGTerrainSubsystem::RemoveObjectTerrainEdits).
			const ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(&Actor);
			const int64 OwnerObjectId = NetObject ? NetObject->GetObjectId() : 0;

			TerrainSubsystem->ApplyObjectTerrainModification(TemplateName, RawPosition, YawRadians, OwnerObjectId);

			TArray<FSWGTerrainHole> Holes;
			GatherInteriorFloorHoles(TreSubsystem, BuildingActor->PortalData, RawPosition, YawRadians, Holes);
			for (FSWGTerrainHole& Hole : Holes)
			{
				Hole.OwnerObjectId = OwnerObjectId;
			}
			TerrainSubsystem->AddTerrainHoles(Holes);
		}
	}

	// Cell 0 is always the exterior shell (Sheet 00 §00.2). Keyed by index,
	// not name: most buildings call it "exterior" but plenty just use "r0"
	// (ply_nboo_cloning_facility_s01.pob, thm_tato_cantina.pob, ...). Every
	// other cell is an interior room, built on demand from its own CCLT
	// SceneCreateObjectByCrc/UpdateContainmentMessage pair.
	const FSWGPobCell& ExteriorCell = BuildingActor->PortalData.Cells[0];
	if (ExteriorCell.MeshPath.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: exterior cell '%s' in portal layout file %s has no mesh path"), *ExteriorCell.CellName, *PobPath);
		return true;
	}

	// The .lod goes through as-is: the mesh generator builds every level of
	// the shell into one asset (USWGMeshGeneratorSubsystem::ResolveLodLevels).
	MeshGeneratorSubsystem->RequestMesh(BuildingActor, ExteriorCell.MeshPath);
	CreateCollisionForCell(TreSubsystem, MeshGeneratorSubsystem, BuildingActor, ExteriorCell);

	// Entrance doors belong to the shell, not to the room behind them: rooms
	// stream in only when the player is close, and a doorway shouldn't stand
	// open until then. The hardpoint can sit on either side of the portal, so
	// take the exterior's refs plus any interior ref that faces cell 0.
	FSWGCellSpawnHandler::SpawnCellDoors(BuildingActor, ExteriorCell.CellName, ExteriorCell.Portals, MeshGeneratorSubsystem);
	TArray<FSWGPobPortalRef> EntrancePortals;
	for (int32 CellIndex = 1; CellIndex < BuildingActor->PortalData.Cells.Num(); ++CellIndex)
	{
		for (const FSWGPobPortalRef& PortalRef : BuildingActor->PortalData.Cells[CellIndex].Portals)
		{
			if (PortalRef.ConnectingCellIndex == 0 && PortalRef.bHasDoorHardpoint)
			{
				EntrancePortals.Add(PortalRef);
			}
		}
	}
	FSWGCellSpawnHandler::SpawnCellDoors(BuildingActor, ExteriorCell.CellName, EntrancePortals, MeshGeneratorSubsystem);

	return true;
}

void FSWGCellSpawnHandler::FinishCell(ASWGCell* CellActor, ASWGBuilding* BuildingActor, int32 CellIndex, TObjectPtr<USWGTreSubsystem> TreSubsystem, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem, bool bForceInterior)
{
	if (!CellActor || !BuildingActor || CellActor->OwningBuilding.IsValid())
	{
		// Null args, or already finished (e.g. a duplicate containment message).
		return;
	}

	if (!BuildingActor->PortalData.Cells.IsValidIndex(CellIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: cell index %d out of range for building %s (%d cells)"), CellIndex, *BuildingActor->GetName(), BuildingActor->PortalData.Cells.Num());
		return;
	}

	const FSWGPobCell& CellData = BuildingActor->PortalData.Cells[CellIndex];

	UGameInstance* GameInstance = CellActor->GetWorld() ? CellActor->GetWorld()->GetGameInstance() : nullptr;
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;

	// Every room waits for USWGInteriorStreamingSubsystem, which calls back
	// through ASWGBuilding::LoadRoom with bForceInterior once the player is
	// close enough (and, for a room visible from outside, looking this way).
	// Except the room the local player zoned in inside: streaming judges by
	// the player's position, which isn't real until this cell is finished
	// and USWGObjectGraphSubsystem::ApplyContainment can compose it.
	const bool bHoldsLocalPlayer = ObjectGraph && ObjectGraph->IsLocalPlayerContainedIn(CellActor->GetObjectId());
	if (!bForceInterior && !bHoldsLocalPlayer)
	{
		USWGInteriorStreamingSubsystem* Streaming = GameInstance ? GameInstance->GetSubsystem<USWGInteriorStreamingSubsystem>() : nullptr;
		if (Streaming && !Streaming->ShouldLoadRoom(*BuildingActor, CellIndex))
		{
			BuildingActor->DeferredNetworkCells.Add({ CellActor, CellIndex });
			Streaming->RegisterBuilding(BuildingActor);
			return;
		}
	}

	FString CellMeshPath;
	if (!MeshGeneratorSubsystem->ResolveLodMeshPath(CellData.MeshPath, CellMeshPath) || CellMeshPath.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: cell %s has no usable mesh path (raw: %s)"), *CellData.CellName, *CellData.MeshPath);
		return;
	}

	CellActor->CellNumber = CellData.CellIndex;
	CellActor->MeshPath = CellMeshPath;
	CellActor->OwningBuilding = BuildingActor;
	CellActor->bCanSeeParent = CellData.CanSeeParent;

	BuildingActor->Cells.Add(CellActor);
	CellActor->AttachToActor(BuildingActor, FAttachmentTransformRules::KeepRelativeTransform);

	SpawnInteriorLayout(CellActor, BuildingActor, CellData, MeshGeneratorSubsystem);

	TWeakObjectPtr<ASWGCell> CellActorWeakPtr = CellActor;
	MeshGeneratorSubsystem->RequestMesh(CellActor, CellMeshPath).Next(OnGameThread([CellActorWeakPtr, TreSubsystem, MeshGeneratorSubsystem](const FSWGMeshGenerationResult& Result)
		{
			if (!CellActorWeakPtr.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: CellActor is no longer valid when mesh generation completed"));
				return;
			}

			ASWGCell* CellActor = CellActorWeakPtr.Get();

			if (!CellActor->OwningBuilding.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: CellActor's OwningBuilding is no longer valid when mesh generation completed"));
				return;
			}

			ASWGBuilding* BuildingActor = CellActor->OwningBuilding.Get();

			FSWGPobCell CellData = BuildingActor->PortalData.Cells[CellActor->CellNumber];
			if (Result.MeshOrComponent.IsType<FEmptyVariantState>())
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: Failed to create or laod mesh for cell %s"), *CellData.CellName);
				BuildingActor->Cells.Remove(CellActor);
				CellActor->Destroy();
				return;
			}

			// Re-attach: USWGMeshGeneratorSubsystem::BuildGeneratedMeshComponent
			// just replaced CellActor's root with the newly-built render mesh
			// component
			CellActor->AttachToActor(BuildingActor, FAttachmentTransformRules::KeepWorldTransform);

			// The room's geometry is lit by its own lights only — see
			// BuildRoomLights and SWGInteriorLightingChannel.
			if (UPrimitiveComponent* RoomMesh = Cast<UPrimitiveComponent>(CellActor->GetRootComponent()))
			{
				SWGSetInteriorLightingChannel(*RoomMesh, /*bAlsoWorld*/ false);
			}
			BuildRoomLights(CellActor, CellData);
			CreateCollisionForCell(TreSubsystem, MeshGeneratorSubsystem, CellActor, CellData);
			CellActor->bCollisionReady = true;

			// Only place/reveal occupants once there is a floor for them to stand
			// on. OwningBuilding becomes valid before the async room mesh does, so
			// using that alone lets a login pawn fall during this gap.
			if (UGameInstance* GameInstance = CellActor->GetGameInstance())
			{
				if (USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>())
				{
					ObjectGraph->NotifyCellFinished(CellActor->GetObjectId());
				}
			}
			BuildingActor->RegisterCellTrigger(CellActor, CellData.CanSeeParent);
		}));

	SpawnCellDoors(BuildingActor, CellData.CellName, CellData.Portals, MeshGeneratorSubsystem);
}

void FSWGCellSpawnHandler::SpawnCellDoors(ASWGBuilding* BuildingActor, const FString& CellName, TArrayView<const FSWGPobPortalRef> Portals, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem)
{
	UWorld* World = BuildingActor ? BuildingActor->GetWorld() : nullptr;
	if (!World || !MeshGeneratorSubsystem)
	{
		return;
	}

	for (const FSWGPobPortalRef& PortalRef : Portals)
	{
		if (PortalRef.DoorStyle.IsEmpty())
		{
			continue; // open archway, no door object
		}

		// A reference with no hardpoint has nothing to place the door by —
		// leave it to the other side of the portal.
		if (!PortalRef.bHasDoorHardpoint)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::SpawnCellDoors: cell %s portal %d has door style '%s' but no hardpoint — leaving the door to the connecting cell"),
				*CellName, PortalRef.PortalNumber, *PortalRef.DoorStyle);
			continue;
		}

		if (BuildingActor->Doors.ContainsByPredicate([&PortalRef](const TObjectPtr<ASWGDoor> DoorObj) { return DoorObj && DoorObj->PortalNumber == PortalRef.PortalNumber; }))
		{
			continue;
		}

		// The style name is a row key, not an appearance name: cantina_door
		// and door_cantina_up both draw appearance/cantina_door.apt, and no
		// row's appearance sits under appearance/lod/.
		const FSWGDoorStyleRow* StyleRow = nullptr;
		if (TWeakObjectPtr<UDataTable> DoorStyleTable = GetDoorStyleTable(); DoorStyleTable.IsValid())
		{
			StyleRow = DoorStyleTable->FindRow<FSWGDoorStyleRow>(FName(*PortalRef.DoorStyle), TEXT("FSWGCellSpawnHandler::SpawnCellDoors"), false);
		}
		if (!StyleRow || StyleRow->DoorAppearance.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::SpawnCellDoors: cell %s portal %d door style '%s' has no door_style.iff row or no doorAppearance"),
				*CellName, PortalRef.PortalNumber, *PortalRef.DoorStyle);
			continue;
		}
		if (!StyleRow->DoorAppearance2.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::SpawnCellDoors: door style '%s' is a double door (%s) — second leaf not spawned yet"),
				*PortalRef.DoorStyle, *StyleRow->DoorAppearance2);
		}

		FString DoorMeshPath;
		if (!MeshGeneratorSubsystem->ResolveAppearanceStaticMeshPath(StyleRow->DoorAppearance, DoorMeshPath) || DoorMeshPath.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::SpawnCellDoors: portal %d door style %s: appearance %s resolved to no mesh"),
				PortalRef.PortalNumber, *PortalRef.DoorStyle, *StyleRow->DoorAppearance);
			continue;
		}

		ASWGDoor* DoorActor = World->SpawnActor<ASWGDoor>(ASWGDoor::StaticClass(), FTransform::Identity);
		BuildingActor->Doors.Add(DoorActor);
		DoorActor->PortalNumber = PortalRef.PortalNumber;
		DoorActor->AttachToActor(BuildingActor, FAttachmentTransformRules::KeepRelativeTransform);
		DoorActor->SetActorRelativeTransform(PortalRef.DoorHardpoint);

		// Copied: the table row pointer isn't guaranteed to outlive the request.
		const FSWGDoorStyleRow StyleCopy = *StyleRow;
		TWeakObjectPtr<ASWGDoor> DoorActorWeakPtr = DoorActor;
		TWeakObjectPtr<ASWGBuilding> OwningBuilding = BuildingActor;
		MeshGeneratorSubsystem->RequestMesh(DoorActor, DoorMeshPath).Next(OnGameThread([DoorActorWeakPtr, StyleCopy, OwningBuilding](const FSWGMeshGenerationResult& Result)
			{
				if (!DoorActorWeakPtr.IsValid() || !OwningBuilding.IsValid())
				{
					return;
				}

				if (Result.MeshOrComponent.IsType<FEmptyVariantState>())
				{
					OwningBuilding->Doors.Remove(DoorActorWeakPtr.Get());
					DoorActorWeakPtr->Destroy();
					return;
				}

				DoorActorWeakPtr->AttachToActor(OwningBuilding.Get(), FAttachmentTransformRules::KeepWorldTransform);
				DoorActorWeakPtr->InitializeDoorStyle(&StyleCopy);
			}));
	}
}


void FSWGBuildingSpawnHandler::Initialize(UWorld* World)
{
	if (bIsInitialized)
	{
		return;
	}

	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::Initialize: World is null"));
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::Initialize: GameInstance is null"));
		return;
	}

	TreSubsystem = GameInstance->GetSubsystem<USWGTreSubsystem>();
	MeshGeneratorSubsystem = GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>();

	bIsInitialized = TreSubsystem && MeshGeneratorSubsystem;
}
