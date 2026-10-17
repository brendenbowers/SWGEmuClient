#pragma once
#include "CoreMinimal.h"
#include "Network/Objects/Zone/Object/TangibleObjectBaseline.h"

struct SWGEMU_API FShipFlightLimits
{
	float Acceleration = 0, Deceleration = 0;
	FVector AngularAcceleration = FVector::ZeroVector; // pitch, yaw, roll
	FVector AngularRates = FVector::ZeroVector; // pitch, yaw, roll
	float MaxSpeed = 0;
};

/** SHIP fields in Core3 baseline order. Collection counters are retained for deltas. */
struct SWGEMU_API FShipObjectBaseline
{
	FTangibleObjectBaseline Tangible;
	int32 ServerObjectsStart = 0, ServerObjectsEnd = 0;
	// Baseline-1 float maps: fields 2,3,4,5,8,9,10,11,12,13 in wire order.
	TSWGBaselineList<TPair<uint32,float>> OwnerComponentFloatMaps[10];
	TSWGBaselineList<TPair<uint32,FString>> ComponentNames;
	TSWGBaselineList<TPair<uint32,uint64>> ComponentCreators;
	TSWGBaselineList<TPair<uint32,uint32>> AmmoMaps[3]; // fields 14..16
	float OwnerValues[20] = {}; // baseline-1 scalar fields 17..36, in Core3 order
	uint64 DroidObjectId = 0;
	uint16 ShipId = 0;
	FShipFlightLimits Flight;
	uint64 TargetId = 0;
	uint32 TargetSlot = 0, ChassisCrc = 0, GuildId = 0;
	float Slip = 0, ChassisHealth = 0, ChassisMaxHealth = 0;
	float FrontShield = 0, RearShield = 0, MaxFrontShield = 0, MaxRearShield = 0;
	float Mass = 0, ChassisSpeed = 0, CapacitorEnergy = 0, BoosterEnergy = 0;
	TSWGBaselineList<uint8> Targetable;
	TSWGBaselineList<TPair<uint32,uint32>> ComponentCrcs, ComponentOptions;
	TSWGBaselineList<TPair<uint32,float>> ArmorMax, ArmorCurrent, HitpointsCurrent, HitpointsMax, RefireEfficiency;
	FString WingName, TypeName, Difficulty, Faction;
	bool bHasBase1 = false, bHasBase3 = false, bHasBase4 = false, bHasBase6 = false;
};
namespace SWGShipBaselineParser
{
	SWGEMU_API bool ParseBase1(FSWGPacket& Packet, FShipObjectBaseline& Out);
	SWGEMU_API bool ParseBase3(FSWGPacket& Packet, FShipObjectBaseline& Out);
	SWGEMU_API bool ParseBase4(FSWGPacket& Packet, FShipObjectBaseline& Out);
	SWGEMU_API bool ParseBase6(FSWGPacket& Packet, FShipObjectBaseline& Out);
	SWGEMU_API bool ApplyDelta(FSWGPacket& Packet, FShipObjectBaseline& Out, uint8 Slot, uint16 UpdateCount);
}
