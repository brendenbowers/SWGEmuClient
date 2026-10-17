#pragma once

#include "CoreMinimal.h"
#include "Network/Objects/Zone/Ship/ShipObjectBaseline.h"

class USWGObjectGraphSubsystem;
class USWGTreSubsystem;
class AActor;

/** A weapon component the HUD indicates. Condition is its hitpoints as a fraction (0 disabled, 1 intact). */
struct SWGUIHOLO_API FSWGSpaceHudWeapon
{
	int32 Slot = 0;
	float Condition = 0.f;
};

/** Core3's ship component slots (ComponentSlots.h). Weapons start at WeaponStart. */
namespace SWGShipSlot
{
	constexpr int32 Reactor = 0, Engine = 1, Shield0 = 2, Shield1 = 3, Armor0 = 4, Armor1 = 5, Capacitor = 6, Booster = 7, WeaponStart = 12;
}

/** What a radar blip stands for: retail draws ships with the default icon and everything else static as a structure. */
enum class ESWGSpaceHudBlipKind : uint8 { Ship, Structure };
/** Who the object is to the pilot, from the ship baselines' faction strings. */
enum class ESWGSpaceHudRelation : uint8 { Neutral, Friendly, Hostile };

struct SWGUIHOLO_API FSWGSpaceHudBlip
{
	int64 ObjectId = 0;
	/** In the ship's frame (X ahead, Y right, Z up), unreal units. */
	FVector Relative = FVector::ZeroVector;
	ESWGSpaceHudBlipKind Kind = ESWGSpaceHudBlipKind::Ship;
	ESWGSpaceHudRelation Relation = ESWGSpaceHudRelation::Neutral;
	bool bTarget = false;
};

/**
 * Condition fractions (0..1) of a ship, from its baselines. The baselines carry a front and a rear shield and armor, not
 * retail's four quadrants, so the HUD shows the front value on both forward quadrants and the rear one on both rear ones.
 * A fraction is unset where the baselines do not carry both the value and its maximum.
 */
struct SWGUIHOLO_API FSWGSpaceHudVitals
{
	TOptional<float> FrontShield;
	TOptional<float> RearShield;
	TOptional<float> FrontArmor;
	TOptional<float> RearArmor;
	/** All component hitpoints together. */
	TOptional<float> System;
	TOptional<float> Chassis;

	static FSWGSpaceHudVitals FromBaseline(const FShipObjectBaseline& State);
};

/** The six components of retail's components page. Shield and armor cover both slots of each; Weapon covers every weapon slot. */
enum class ESWGSpaceHudComponent : uint8 { Reactor, Engine, Weapon, Capacitor, Shield, Armor, Count };

/** A missile or countermeasure launcher with its ammunition count (AmmoMaps: current, max, class, keyed by weapon slot). */
struct SWGUIHOLO_API FSWGSpaceHudLauncher
{
	int32 Slot = 0;
	int32 Ammo = 0;
	int32 AmmoMax = 0;
	bool bCountermeasure = false;
};

/** The ship the pilot has targeted, from its own baselines. Empty strings and unset fractions mean the client does not have them. */
struct SWGUIHOLO_API FSWGSpaceHudTarget
{
	int64 ObjectId = 0;
	FString Name;
	FString TypeName;
	FString Difficulty;
	FString Faction;
	FSWGSpaceHudVitals Vitals;
	TWeakObjectPtr<AActor> Actor;
	bool bIsShip = false;
	/** The target ship to your own, from the two faction strings; picks the friendly or enemy target bracket. */
	ESWGSpaceHudRelation Relation = ESWGSpaceHudRelation::Neutral;
};

/**
 * Everything the piloting HUD shows, gathered from the ship baselines and the object graph. An unset optional means the
 * baselines do not carry the value, and the element that shows it stays hidden rather than showing a made-up number.
 * Positions are in the ship's frame, which is the world frame while piloting (the local ship sits at the origin).
 */
struct SWGUIHOLO_API FSWGSpaceHudData
{
	/** Metres per second. */
	TOptional<float> Speed;
	TOptional<float> MaxSpeed;
	/** The booster's top speed; the gauge's overdrive region runs from MaxSpeed up to it. */
	TOptional<float> BoostMaxSpeed;
	/** The pilot's throttle setting, 0..1. */
	TOptional<float> Throttle;
	TOptional<float> Capacitor;
	TOptional<float> CapacitorMax;
	TOptional<float> Booster;
	TOptional<float> BoosterMax;
	TArray<FSWGSpaceHudWeapon> Weapons;

	bool bHasTarget = false;
	FVector TargetLocation = FVector::ZeroVector;
	/** The target's velocity relative to the ship, in unreal units per second. */
	FVector TargetRelativeVelocity = FVector::ZeroVector;
	FVector ShipLocation = FVector::ZeroVector;
	FVector ShipForward = FVector::ForwardVector;
	/** Metres per second. Not in any client data or baseline, so unset unless swg.Space.HudProjectileSpeed is set. */
	TOptional<float> ProjectileSpeed;

	/** The pilot's ship, for the miniature in the radar. */
	TWeakObjectPtr<AActor> ShipActor;
	FSWGSpaceHudVitals Vitals;
	TOptional<float> Components[int32(ESWGSpaceHudComponent::Count)];
	TArray<FSWGSpaceHudLauncher> Launchers;
	/** Missiles in flight at the ship (CreateMissileMessage). */
	int32 IncomingMissiles = 0;
	bool bPilotIncapacitated = false;
	/** Whether the server handles the escapePod command (it does; the pod launches on a second press). */
	bool bCanEject = true;

	/** Radar contents: objects within RadarRange (unreal units), nearest first. */
	TArray<FSWGSpaceHudBlip> Blips;
	float RadarRange = 0.f;
	/** The zone's north in the ship's frame, if the ship's pose is known. */
	TOptional<FVector> NorthInShip;

	/** Set when bHasTarget. */
	FSWGSpaceHudTarget Target;

	/** Fills Out from the local pilot's ship; false when the pilot has no ready ship. */
	static bool Gather(const USWGObjectGraphSubsystem& Graph, FSWGSpaceHudData& Out);

	/** The ship-component slot a component page entry reads (Weapon covers WeaponStart upward; see the cpp for the rest). */
	static TOptional<float> ComponentCondition(const FShipObjectBaseline& State, ESWGSpaceHudComponent Component);
};
/** The camera the HUD projects through. Size is the HUD's local size in slate units. */
struct SWGUIHOLO_API FSWGSpaceHudView
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float HorizontalFovDegrees = 90.f;
	FVector2D Size = FVector2D(1920.0, 1080.0);
};

struct SWGUIHOLO_API FSWGSpaceHudProjection
{
	/** In front of the camera and inside the view. */
	bool bOnScreen = false;
	FVector2D Position = FVector2D::ZeroVector;
	/** Unit direction from the screen centre toward the point; valid also when it is off screen or behind the camera. */
	FVector2D Direction = FVector2D(0.0, -1.0);
};

/** The HUD's regions. Each is a canvas panel later stages fill: 6c radar, 6d target, 6e status and warnings. */
enum class ESWGSpaceHudSlot : uint8 { Reticle, Radar, Target, Status, Warnings, Count };

/** Slot rectangles for one view size, in slate units. Every slot lies inside Safe. */
struct SWGUIHOLO_API FSWGSpaceHudLayout
{
	FVector2D ViewSize = FVector2D::ZeroVector;
	/** Design unit: one pixel of a 1080-high screen. */
	float Unit = 1.f;
	FBox2D Safe;
	FBox2D Slots[int32(ESWGSpaceHudSlot::Count)];

	const FBox2D& GetSlot(ESWGSpaceHudSlot Slot) const { return Slots[int32(Slot)]; }
	/** Retail's ShipReticle page is 354 px wide; this is its scale into the reticle slot. */
	float GetReticleScale() const { return GetSlot(ESWGSpaceHudSlot::Reticle).GetSize().X / 354.0; }
};

namespace SWGSpaceHud
{
	/** Retail's ShipReticle page, in retail pixels. */
	constexpr float RetailReticleWidth = 354.f;
	constexpr float RetailReticleHeight = 284.f;

	/** A modern widescreen arrangement: gauges on the reticle at the centre, radar bottom centre, target right, status left, warnings top centre. */
	SWGUIHOLO_API FSWGSpaceHudLayout ComputeLayout(const FVector2D& ViewSize);

	/** Retail-like condition bands for status percentages: green from half up, amber from a quarter, red below. */
	enum class EConditionBand : uint8 { Good, Warning, Critical };
	SWGUIHOLO_API EConditionBand ClassifyCondition(float Fraction);
	/** Green (0x62FF15), amber (0xFFAC00) or red (0xFF2D2D), the retail HUD's value colours. */
	SWGUIHOLO_API FLinearColor GetConditionColor(float Fraction);

	SWGUIHOLO_API FSWGSpaceHudProjection Project(const FSWGSpaceHudView& View, const FVector& World);

	/**
	 * Where to aim to hit a target moving at TargetVelocity from a shooter at the origin: the point a projectile of ProjectileSpeed
	 * reaches at the same time as the target. Falls back to the target's position when no intercept exists.
	 */
	SWGUIHOLO_API FVector InterceptPoint(const FVector& TargetRelativeLocation, const FVector& TargetVelocity, float ProjectileSpeed);

	/**
	 * Where an object at Relative (ship frame, X ahead, Y right, Z up) sits in the radar's unit sphere. Range is the distance that
	 * reaches the surface; anything farther is pulled onto it along the same bearing.
	 */
	SWGUIHOLO_API FVector RadarSpherePosition(const FVector& Relative, float Range);

	/** The flat radar's disc position of Relative: x right, y down the screen (ahead is up), unit radius. Altitude is dropped. */
	SWGUIHOLO_API FVector2D RadarDiscPosition(const FVector& Relative, float Range);

	/** The world point Distance along the view ray through ScreenPoint (slate units); the inverse of Project. */
	SWGUIHOLO_API FVector Deproject(const FSWGSpaceHudView& View, const FVector2D& ScreenPoint, double Distance);

	/** The world length that spans Pixels (slate units) across the screen at Distance in front of the camera. */
	SWGUIHOLO_API double WorldSizeForPixels(const FSWGSpaceHudView& View, double Pixels, double Distance);

	/** The retail-style relation of a ship to the pilot's, from the two faction strings (empty or "neutral" is neutral). */
	SWGUIHOLO_API ESWGSpaceHudRelation ClassifyRelation(const FString& OwnFaction, const FString& OtherFaction);
}
