#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGFxCommon.h"
#include "TRE/SWGIffReader.h"

/**
 * .prt particle effects (FORM PEFT). Decoded from all 1018 retail files (2755 emitters, plus 10 zero-byte stubs):
 *
 *   PEFT/0000|0001|0002
 *     PTIM                       effect timing (absent in the single 0000 test file)
 *     0000                       int32 group count; 0002 adds 4 floats (see FSWGParticleEffect::Header0002)
 *     EMGP/0000|0001             emitter group: [PTIM] + 0000 int32 emitter count
 *       EMTR/0006..0014          [PTIM] + 16 x WVFM + 0000 params + (PTQD | PTMH)
 *         PTQD: PTCL, FORM 0000|0001 { WVFM x3 (rotation, length, width), PTEX, [0001: 0000 1 byte = width from length] }
 *         PTMH: PTCL, 0000 mesh path, WVFM x4 (scale, rotation x/y/z)
 *           PTCL/0002|0003: 0000 {name, byte random spin, int32 attachment count}, CLRR, WVFM alpha, WVFM speed multiplier,
 *             [WVFM x3 rotation X/Y/Z in turns (0003 only)], PATT x count
 *   (EMTR/0000 in the lone test file holds PTCL directly, with different curve counts; kept raw.)
 *
 * Counts: EMTR 0006=12, 0009=78, 0010=28, 0011=696, 0012=1056, 0013=838, 0014=46 (+1 test 0000);
 * PTQD 2612 / PTMH 142 / bare 1; PTCL 0002=1824, 0003=930; PATT 322; flocking emitters 76 (mode 1=59, 2=11, 3=6).
 */

/**
 * The 16 emitter WVFMs, in file order. Slot meanings confirmed by the spawn/alter code (0x7a3990, 0x7a2e10, 0x7a6980):
 * the emitter object's sampler array starts at emitter+0x2b0, 8 bytes per slot. Curves are sampled at the run fraction
 * (elapsed / run length) and the length-valued ones are multiplied by the effect scale.
 */
enum class ESWGEmitterCurve : uint8
{
	TranslateX,
	TranslateY,
	TranslateZ,
	/** Rotation curves are in turns (x 2*pi, 0x1923c84); emission is along local +Y, so a bolt rotates X by 0.25 to fire along +Z. */
	RotateX,
	RotateY,
	RotateZ,
	/** Style 0: added to the shape radius along the shape vector. Style 1: push along the emission direction. */
	Distance,
	/** Scale of the unit emission shape vector (rain: 20, campfire: 0.05). */
	ShapeSize,
	/** Cone half-angle in degrees (x pi/180, 0x1923c80); used by emission style 1 only. */
	Spread,
	/** Particles per second, or metres between particles when generating by distance. */
	Rate,
	/** Initial speed in m/s; the particle's PTCL speed curve then multiplies it over life. */
	Speed,
	/** Always zero in all 2755 emitters and never sampled by the client: inherit velocity, cluster count and radius. */
	InheritVelocity,
	ClusterCount,
	ClusterRadius,
	/** Particle lifetime in seconds (butterfly 60 s, rain 1.5 s, bolts 20 s). */
	Lifetime,
	/** Multiple of 9.8 m/s^2 pulling along -Y (campfire smoke -0.075 rises slowly). */
	Gravity,
	Count
};

/** Mesh-particle (PTMH) curve slots. */
enum class ESWGMeshCurve : uint8
{
	Scale,
	RotateX,
	RotateY,
	RotateZ,
	Count
};

/** Quad-particle (PTQD) curve slots. Bolts are Length 20 / Width 0.1, which pins the last two. */
enum class ESWGQuadCurve : uint8
{
	Rotation,
	Length,
	Width,
	Count
};

/** PATT: a child effect spawned by the particle. Layout confirmed (loader 0x7c2b90); bFlag and the non-collision modes unknown. */
struct FSWGParticleChild
{
	FString Path;
	/** Life fraction (0..1) range the child is requested at: a random point in it is drawn per particle (0/0 birth, 1/1 death). */
	float ValueA = 0.f;
	float ValueB = 0.f;
	/** Stored by the loader (0x7c27e0), 1 in 99%; 0 for 11 fireworks. Not seen in the particle update. */
	bool bFlag = false;
	/** Stored at +0x20 (0x7c2840). 3 = spawned when the particle hits the terrain (0x7a4840). 0, 1, 2 unknown; 2 = follows the particle (inferred). */
	int32 Mode = 0;
};

/** PTCL: what one emitted particle looks like over its life. */
struct FSWGParticleDescription
{
	/** 0 = lone test file, 2 or 3. */
	int32 Version = 0;
	FString Name;
	/**
	 * The bytes after the name are {byte, int32 attachment count} (0x7bfa10). Flags[0] is the byte, confirmed as
	 * "random spin direction" (0x7bee60/0x7beff0, used when a quad spawns, 0x7a4350); Flags[1] is the count's low byte.
	 */
	uint8 Flags[2] = {};
	TArray<uint8> RawNameTail;
	/** Flags[0] as a bool: each particle's rotation sign is random. */
	bool bRandomSpinDirection = false;
	FSWGParticleColorRamp Color;
	/** 0..1 across life; clamped 0..1 in 0003. */
	FSWGWaveform Alpha;
	/** Speed multiplier over life: a particle moves by velocity * Speed(life) * dt (0x7a4840). */
	FSWGWaveform Speed;
	/** 0003 only: per-particle rotation about X, Y, Z in turns (0x7a5ef0, 0x7bf030/040/050); applied when any is non-zero. Almost always zero. */
	TArray<FSWGWaveform> ExtraCurves;
	bool bHasChild = false;
	FSWGParticleChild Child;
};

/**
 * EMTR 0000 params block, decoded from the retail loaders (0014 = 0x7bd780) and users (0x7a3990, 0x7a4840, 0x7a6980,
 * 0x7a2e10). Offsets are bytes after the name from 0009 on; 0006 is shorter.
 */
struct SWGTRE_API FSWGEmitterParams
{
	/** EMTR form version, 0 / 6 / 9..14. */
	int32 Version = 0;
	FString Name;

	// Header (first 48 bytes after the name from 0009 on).

	/** @0 int32: 0 = particles leave along the shape vector, 1 = a cone around +Y at Spread degrees (0x7a3990). */
	int32 EmissionStyle = 0;
	/** @4 int32 (0006: absent): 0 = Rate is particles per second, 1 = Rate is metres between particles (0x7a6980). */
	bool bGenerateByDistance = false;
	/** @8 int32 (0006: @4): ParticleEmitterShape subclass, 0x7c5d30. 0 circle (XZ ring), 1 sphere, 2 rectangle (XZ outline), 3 cube, 4 line (+-Z), 5 plus (+-X, +-Z). */
	int32 Shape = 0;
	/** @12 byte (0006: @8): a finished run loops without waiting for its particles to die, and a burst may fire while particles live (0x7a2e10, 0x7a6980). */
	bool bIgnoreLiveParticles = false;
	/** @13/@17 floats (0006: @9/@13): length of one run in seconds, a random value in the range (0x7b99e0/0x7b99f0, 0x7a2050). */
	float RunTimeMin = 4.f;
	float RunTimeMax = 4.f;
	/** @21 float (0006: absent): most live particles; nothing spawns above it. */
	float MaxParticles = 256.f;
	/** @25 byte (0006: @17): burst emitter, one batch of CountMin..CountMax per run instead of a rate. */
	bool bBurst = false;
	/** @26/@30 int32 (0006: absent): burst batch size range. */
	int32 CountMin = 0;
	int32 CountMax = 0;
	/** @34 byte (0006: @18): each quad starts at a random rotation of 0..1 turns instead of 0. */
	bool bRandomInitialRotation = false;
	/** @35 int32 (0006: @19; 3 loads as 2): quad orientation. 0 = faces the camera, 1 = along the motion in a plane through the emitter axis, 2 = along the motion facing the camera (0x7a5ef0). */
	int32 Orientation = 0;
	/** @39/@40 (0006: @23/@24) are 1 in every emitter of every version; stored, never read. */
	uint8 Always1A = 0;
	uint8 Always1B = 0;
	/** Optional sound played with the emitter (0009+, at offset 48; "sound\veh_distant_xwing.snd"), '/'-normalised. */
	FString SoundPath;

	// Tail (after the sound string).

	/** tail@0 byte: particles are emitter-local and ride with it; clear = they are spawned in the world and left there (0x7a2660, 0x7a3990). */
	bool bLocalSpace = false;
	/** tail@1 byte: particles collide with the terrain and bounce (0x7a4840). */
	bool bTerrainCollide = false;
	/** tail@2 byte: a terrain hit ends the particle's life (0x7a4840). */
	bool bKillOnTerrainHit = false;
	/** tail@3 float (0012+): height above the terrain that counts as a hit. */
	float GroundOffset = 0.f;
	/**
	 * Five floats, 0.5,0.5,0.5,0.5,1.0 by default (0009-0011 tail@11..27, 0012+ tail@7..23):
	 * [0]/[1] horizontal bounce factor min/max, [2]/[3] vertical bounce factor min/max (picked per run, 0x7a2050),
	 * [4] wind resistance, 1 = immune (0x7a4840: wind velocity is wind * (1 - resistance)).
	 */
	float ParamFloats[5] = {0.5f, 0.5f, 0.5f, 0.5f, 1.f};
	/** Pair, -1/-1 by default; 15/50 and 300/800 on fireworks (0009-0011 tail@31/@35, 0012+ tail@27/@31). Stored at +0x244/+0x248, never read by the particle code: unknown (a distance range?). */
	float RangeMin = -1.f;
	float RangeMax = -1.f;
	/** 0012+ tail@35 int32: 0 none, 1/2/3 on 25 flocking effects (butterflies=1, beetles=2, fish=3). Non-zero turns on the flocking pass (0x7a4840). */
	int32 FlockMode = 0;
	/**
	 * Flocking, only edited when FlockMode > 0 (0010+, 0012+ tail@39..): [0] neighbour radius m, [1] alignment gain,
	 * [2] cohesion gain, [3] separation gain, [4] horizontal half-extent m and [5] vertical half-extent m of the box
	 * the flock stays in around the emitter, [6] vertical edge mode (0x7a4840). Butterflies 5,.3,.1,.4,3,2,0.
	 */
	float FlockParams[7] = {};
	/** 0011 only: one more float after FlockParams (0 mostly). */
	float Float0011 = 0.f;
	/** 0012+ tail@67 int32: how the box edge turns a flock around, 0 = hard push back, 1 = soft push plus reflecting velocity. */
	int32 FlockBoundsShape = 0;
	/** 0012+ tail@79 float, 0..0.9 (dust storm 0.9, steam 0.7, smoke 0.1-0.4): how far the particle colour is pulled toward the scene ambient colour (0x7a2660). */
	float AmbientBlend = 0.f;
	/** 0012+ tail@83 byte: spawn on the terrain surface. tail@84 byte: and start at rest (0x7a3990). */
	bool bSnapToTerrain = false;
	bool bTerrainZeroVelocity = false;
	/** 0012+ tail@85 float, almost always 0: height above the terrain for bSnapToTerrain. */
	float TerrainHeightOffset = 0.f;
	/** 0012+ tail@89 float: flocks put an angle in radians here (0.698 = 40 deg); a random turn limit (0x7a4840 end). tail@93 float (16, 3, 1 on flocks) is stored and never read. */
	float FlockTurnAngle = 0.f;
	float Tail93 = 0.f;
	/** 0013 tail@97: one particle is emitted the moment a run starts. 0014 tail@98: the particle can be dropped by the particle LOD setting (0x7a4350). */
	bool bEmitOnStart = false;
	bool bLodDroppable = false;

	/** Every byte after the name, split at the sound string, so nothing is lost. */
	TArray<uint8> RawHeader;
	TArray<uint8> RawTail;

	bool IsFlocking() const { return FlockMode > 0; }
	float GetWindResistance() const { return ParamFloats[4]; }
};

enum class ESWGParticleKind : uint8
{
	/** EMTR 0000 test file: PTCL without a PTQD/PTMH wrapper. */
	Bare,
	/** PTQD: quad with a PTEX shader; camera-facing or along its motion per Params.Orientation. */
	Quad,
	/** PTMH: an .msh/.apt/.sat mesh per particle. */
	Mesh
};

struct SWGTRE_API FSWGParticleEmitter
{
	int32 Version = 0;
	FSWGEffectTiming Timing;
	FSWGWaveform Curves[(int32)ESWGEmitterCurve::Count];
	FSWGEmitterParams Params;
	ESWGParticleKind Kind = ESWGParticleKind::Quad;
	FSWGParticleDescription Particle;

	/**
	 * Quad: Rotation (turns), Length, Width (confirmed: description +0xac/+0xc4/+0xdc). Both sizes are half-extents:
	 * the corners are the centre +- length * axis1 +- width * axis2 (0x7a2660). Mesh: Scale, RotateX, RotateY, RotateZ
	 * (description +0xac/+0xc4/+0xdc/+0xf4; the mesh axis use was not traced).
	 */
	TArray<FSWGWaveform> ShapeCurves;
	/** Quad only. */
	FSWGParticleTexture Texture;
	/** PTQD 0001 trailing byte (0x7b81a0, read at 0x7a2660): the quad is as wide as it is long and the Width curve is ignored. */
	bool bWidthFromLength = false;
	/** Mesh only: .msh/.apt/.sat, '/'-normalised. */
	FString MeshPath;

	const FSWGWaveform& GetCurve(ESWGEmitterCurve Curve) const { return Curves[(int32)Curve]; }
	bool IsQuad() const { return Kind == ESWGParticleKind::Quad; }
};

struct FSWGEmitterGroup
{
	int32 Version = 0;
	FSWGEffectTiming Timing;
	TArray<FSWGParticleEmitter> Emitters;
};

struct SWGTRE_API FSWGParticleEffect
{
	/** 0 (test file), 1 or 2. */
	int32 Version = 0;
	FSWGEffectTiming Timing;
	/** 0002 only: 4 floats after the group count - (1,0,1,1) in 185 of 196 files; 20 or 64 first on bolts/black holes. Unknown. */
	float Header0002[4] = {};
	TArray<FSWGEmitterGroup> Groups;

	int32 GetEmitterCount() const;

	/** Paths this effect needs, '/'-normalised and de-duplicated: PTEX shaders, PTMH meshes, PATT child .prt files, emitter sounds. */
	void CollectReferences(TArray<FString>& OutShaders, TArray<FString>& OutMeshes, TArray<FString>& OutChildEffects, TArray<FString>& OutSounds) const;
};

class SWGTRE_API FSWGParticleEffectReader
{
public:
	/** False for an empty/invalid reader (10 retail .prt are zero bytes) or any layout this reader does not expect; OutError says where. */
	static bool Read(const FSWGIffReader& Reader, FSWGParticleEffect& OutEffect, FString* OutError = nullptr);

	/** Reads a FORM EMTR (used by .ltn-style readers that embed emitters, and by tests). */
	static bool ReadEmitter(const FSWGIffReader& Reader, const FSWGIffChunk& EmitterForm, FSWGParticleEmitter& OutEmitter, FString* OutError = nullptr);
};
