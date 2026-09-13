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
 * Axes. SWG's native frame (files and the wire) is x east, y up, z north,
 * with models facing +z — left-handed, like UE. "Raw" space is Core3's
 * relabeling of it: x east, y north, z up, metres. Read as UE (X, Y, Z) that
 * raw frame is a mirror image (UE has Y to the right of X, but north is to
 * the LEFT of east), which is why the raw<->UE step below is a rotation and
 * not just the scale: UE X = north (raw Y), UE Y = east (raw X), UE Z = up.
 * SWG's +z forward therefore lands on UE +X, so meshes need no yaw offset
 * and headings map 1:1. FSWGIFFChunkReader::ReadVectorLE applies the same
 * rotation to native file geometry directly.
 */
FORCEINLINE FVector SWGToUnrealSpace(const FVector& RawPos)
{
	return FVector(RawPos.Y, RawPos.X, RawPos.Z) * SWGWorldScale;
}

FORCEINLINE float SWGToUnrealSpace(float RawValue)
{
	return RawValue * SWGWorldScale;
}

FORCEINLINE FVector SWGToRawSpace(const FVector& UnrealPos)
{
	return FVector(UnrealPos.Y, UnrealPos.X, UnrealPos.Z) / SWGWorldScale;
}

FORCEINLINE float SWGToRawSpace(float UnrealValue)
{
	return UnrealValue / SWGWorldScale;
}

/**
 * Raw-space yaw (FSWGTerrainPlacement, FSWGTerrainHole) is the usual
 * counter-clockwise angle in the raw (x east, y north) plane. UE yaw turns
 * +X (north) toward +Y (east), which in that plane is clockwise.
 */
FORCEINLINE float SWGToRawYawRadians(float UnrealYawDegrees)
{
	return -FMath::DegreesToRadians(UnrealYawDegrees);
}

/** Native (x, y-up, z) quaternion components -> UE, the same axis rotation as SWGToUnrealSpace. */
FORCEINLINE FQuat SWGNativeToUnrealRotation(float X, float Y, float Z, float W)
{
	return FQuat(Z, X, Y, W);
}

/** Inverse of SWGNativeToUnrealRotation: the result's X/Y/Z/W are the native wire components, in order. */
FORCEINLINE FQuat SWGUnrealToNativeRotation(const FQuat& UnrealRotation)
{
	return FQuat(UnrealRotation.Y, UnrealRotation.Z, UnrealRotation.X, UnrealRotation.W);
}

/** Characters are yaw-only on the wire; drops any pitch/roll before converting. */
FORCEINLINE FQuat SWGCharacterHeadingToNativeRotation(const FRotator& UnrealRotation)
{
	return SWGUnrealToNativeRotation(FRotator(0.0f, UnrealRotation.Yaw, 0.0f).Quaternion());
}
