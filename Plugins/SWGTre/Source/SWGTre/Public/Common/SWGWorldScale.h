#pragma once

#include "CoreMinimal.h"

/**
 * Raw server/file position data (network transform messages, .trn terrain
 * evaluation, .ws world-snapshot spawn positions) is authored in a much
 * smaller unit than the mesh geometry these actors are built from — this is
 * the single conversion factor between the two, applied only at the
 * boundary where raw data becomes a final UE Actor transform or mesh
 * vertex. Terrain evaluator internals (boundaries/affectors/fractals) and
 * anything else that stays entirely within "raw space" comparisons (e.g.
 * WorldSnapshotSpawnRadius against a raw Node.Position) must NOT apply this
 * — only the final placement/vertex step should.
 */
constexpr float SWGWorldScale = 100.0f;

/**
 * SWG models face +Z, which ReadVectorLE's swg(x,y,z) -> ue(x,z,y) swap lands
 * on UE +Y; UE characters face actor +X. Every character mesh applies this
 * turn, whichever path built it, and server headings add the opposite sign
 * back (see USWGObjectGraphSubsystem). Non-character meshes keep the
 * unrotated +Y forward and take no offset at either end.
 */
constexpr float SWGCharacterMeshYaw = -90.0f;

FORCEINLINE FVector SWGToUnrealSpace(const FVector& RawPos)
{
	return RawPos * SWGWorldScale;
}

FORCEINLINE float SWGToUnrealSpace(float RawValue)
{
	return RawValue * SWGWorldScale;
}

FORCEINLINE FVector SWGToRawSpace(const FVector& UnrealPos)
{
	return UnrealPos / SWGWorldScale;
}

FORCEINLINE float SWGToRawSpace(float UnrealValue)
{
	return UnrealValue / SWGWorldScale;
}
