#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * One FORM WEAR group from a mobile template's clientDataFile (.cdf): the
 * wearable meshes a "dressed_*" NPC is drawn with and the customization
 * (WCSI) that colors them. This is where most NPC clothing/armor comes from —
 * the server never sends TANO wearables for these templates; the outfit is
 * baked into the client data.
 */
struct FSWGClientDataWearable
{
	/** appearance/mesh/*.lmg paths — one skeletal wearable each. */
	TArray<FString> MeshPaths;

	/** Full customization variable name (e.g. "/private/index_color_1") -> value, applied to every mesh in this group. */
	TMap<FString, int32> Customization;
};

/**
 * One named hook from CSND / CEFT / EVNT: [event\0][target\0] (the last string may lack its null).
 * Events repeat (several "footstep" entries pick by surface or at random), so these are kept as arrays.
 */
struct FSWGClientDataEvent
{
	/** "footstep", "vocalize", "hitlight", "hitheavy", "attacklight", "attackheavy", "hitground", emote names, ... */
	FString EventName;

	/** CSND: a .snd. CEFT: a .cef (or .snd). EVNT: a .snd. */
	FString Target;
};

/**
 * FORM FLAG (flagpoles, 10 files) or FORM BANR (banner poles, 11 files): a cloth appearance driven by wind.
 * APPR .sat (its flag.lat / flag.ash pick the idle, medium and heavy wind loops), HPNT hardpoint it hangs from,
 * then three floats that are the same in every file: WSCL 1.0, MASP 10.0, and for BANR LIFT 30.0.
 * Meanings inferred from the names (wind scale, max wind speed, banner lift angle or force).
 */
struct FSWGClientDataFlag
{
	bool bBanner = false;
	FString AppearancePath;
	FString Hardpoint;
	float WindScale = 1.f;
	float MaxWindSpeed = 10.f;
	float Lift = 0.f;
};

/** HOBJ (304 chunks): [appearance\0][hardpoint\0] - a .prt, .apt or .sat parented to a hardpoint (waterfall spray, ship controllers). */
struct FSWGClientDataHardpointObject
{
	FString AppearancePath;
	FString Hardpoint;
};

/**
 * LOBJ (82 chunks, 2 types): int32 type, appearance path (type 1: a glow .prt, often empty), then 17 floats.
 * Confirmed layout (SWGEmu.exe 0x609470, 0x6095d0): every chunk is exactly path + 68 bytes. Float 0-2 is the diffuse RGB (alpha 1.0;
 * max component 0.255 for the streetlamps, so it is pre-scaled), 3-5 the specular RGB (zero for type 1). Type 1 reads float 6 as the
 * light range (m); type 2 keeps floats 6-9 (6 and 7 read as ranges: campfire 15 and 25; 8-9 unverified). Floats 10-12 are the
 * constant/linear/quadratic attenuation (stored at light+0x100..0x108, same as CLGT). Float 14-16 is the light's offset from the object origin in SWG axes (candlestick 0.7 up,
 * tiki torch 1.445 up, streetlamps 2.9-3.7 up - the lamp heads). Float 13 reads as an int that is 1 only on the
 * non-"_on" streetlamps (and the field constructor), so it is taken as "lit at night only". Float 13 lands in light+0x161
 * (semantics unverified). All floats stay in Params.
 */
struct FSWGClientDataLight
{
	/** 1 = glow particle + light, 2 = plain point light. */
	int32 Type = 0;
	FString EffectPath;
	FLinearColor Color = FLinearColor::Black;
	FLinearColor SecondColor = FLinearColor::Black;
	float Range = 0.f;
	float SecondRange = 0.f;
	/** SWG axes (x right, y up, z forward), metres, from the object origin. */
	FVector3f Offset = FVector3f::ZeroVector;
	/** Inferred from float 13: lit only after dark (the non-"_on" streetlamps). */
	bool bNightOnly = false;
	float Params[17] = {};
};

/**
 * CHLD (5558 chunks, an appearance .apt/.prt/.lmg) and CHL2 (1410, an object template .iff): a child placed relative to the
 * parent. Exactly path + 68 bytes: position x,y,z (SWG axes, metres, confirmed - e.g. bridge pieces at +-8 m), heading in
 * degrees (inferred: -135, 135, -45 seen), and 13 more floats that are zero in all but 11 chunks (unknown, kept raw).
 */
struct FSWGClientDataChild
{
	bool bObjectTemplate = false;
	FString Path;
	/** SWG-native axes as stored (x right, y up, z forward); not converted. */
	FVector3f Offset = FVector3f::ZeroVector;
	float HeadingDegrees = 0.f;
	float Extra[13] = {};
};

/**
 * FORM VTHR (clientdata/vehicle/*.iff, and ship .cdf engines): one thrust level of a vehicle.
 * INFO is a float 0, 0.5, 0.75, 1.0 (the speed fraction the level starts at), then HOBJ thruster particles
 * ([prt\0][hardpoint\0]) and one VSND: [soundHardpoint\0][idle\0][accel\0][decel\0][run\0] and four damage
 * fire sounds. The four vehicle sounds repeat in every level; empty strings mean none.
 */
struct FSWGClientDataThrust
{
	float Level = 0.f;
	TArray<FSWGClientDataHardpointObject> Thrusters;
	FString SoundHardpoint;
	FString IdleSound;
	FString AccelSound;
	FString DecelSound;
	FString RunSound;
	TArray<FString> FireSounds;
};

/** FORM VGEF: INFO is [byte surface: 0 land, 1 water][sound\0][hardpoint\0][prt\0] - the dust or wake trail under a vehicle. */
struct FSWGClientDataGroundEffect
{
	bool bWater = false;
	FString Sound;
	FString Hardpoint;
	FString AppearancePath;
};

struct FSWGClientDataFile
{
	/** CSSI: the creature's own body customization ("/shared_owner/index_color_skin", "/shared_owner/blend_fat", ...). */
	TMap<FString, int32> Customization;

	TArray<FSWGClientDataWearable> Wearables;

	/** ASND: attached looping .snd paths (breathing, machinery, soundobject ambience). */
	TArray<FString> AttachedSounds;

	/** CSND: event -> .snd. */
	TArray<FSWGClientDataEvent> SoundEvents;

	/** CEFT: event -> clienteffect .cef (or .snd), e.g. footsteps per surface. */
	TArray<FSWGClientDataEvent> EffectEvents;

	/** EVNT (player species cdf): emote/footstep event -> .snd. */
	TArray<FSWGClientDataEvent> PlayerEvents;

	/** FLAG / BANR forms. */
	TArray<FSWGClientDataFlag> Flags;
	TArray<FSWGClientDataHardpointObject> HardpointObjects;
	TArray<FSWGClientDataLight> Lights;
	/** CHLD and CHL2, in file order. */
	TArray<FSWGClientDataChild> Children;

	/** VTHR levels in file order (ascending Level) and VGEF ground effects. */
	TArray<FSWGClientDataThrust> Thrusts;
	TArray<FSWGClientDataGroundEffect> GroundEffects;
};

/**
 * Reads clientdata/*.cdf: FORM CLDF > FORM 0000 > CSSI chunks
 * ([name\0][int32 LE]) plus FORM WEAR groups of MESH ([path\0]) and WCSI
 * ([name\0][int32 LE]) chunks, and the sound hooks ASND ([path\0]) and
 * CSND/CEFT/EVNT ([event\0][target\0]), and the visual hooks FLAG/BANR, HOBJ,
 * LOBJ, CHLD/CHL2, and the vehicle forms VTHR/VGEF. Only top-level chunks are read: ship cdfs nest further
 * ASND/sound forms (DAMA, DSEF, ...) that are out of scope, as are the
 * ship-only HLOB/IHOB/GLOW chunks and the rare PALV. Layout confirmed against every current .cdf in
 * the TRE set — WCSI always follows the group's MESH entries.
 */
class SWGTRE_API FSWGClientDataFileReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGClientDataFile& OutData);
};
