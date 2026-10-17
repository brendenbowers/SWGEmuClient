#include "Network/Objects/Zone/Ship/ShipObjectBaseline.h"
#include "Network/Objects/Zone/Object/SWGDeltaListHelpers.h"

namespace SWGShipBaselineParser
{
	template<class V, class ReadValue>
	TSWGBaselineList<TPair<uint32,V>> ReadComponentMap(FSWGPacket& P, ReadValue Read)
	{
		return ReadBaselineMap<TPair<uint32,V>>(P, [&Read](FSWGPacket& Item)
		{
			const uint32 Slot = Item.ReadUInt32();
			return TPair<uint32,V>(Slot, Read(Item));
		});
	}
	bool ApplyDelta(FSWGPacket& P, FShipObjectBaseline& Out, uint8 Slot, uint16 UpdateCount)
	{
		FShipObjectBaseline N = Out;
		auto FloatValue = [](FSWGPacket& Item) { return Item.ReadFloat(); };
		auto UIntValue = [](FSWGPacket& Item) { return Item.ReadUInt32(); };
		for (uint16 Update = 0; Update < UpdateCount; ++Update)
		{
			const uint16 Index = P.ReadUInt16();
			if (P.IsError()) return false;
			if (Slot == 1)
			{
				if (Index == 0) N.ServerObjectsStart = P.ReadInt32();
				else if (Index == 1) N.ServerObjectsEnd = P.ReadInt32();
				else if (Index >= 2 && Index <= 5) N.OwnerComponentFloatMaps[Index-2] = ReadComponentMap<float>(P, FloatValue);
				else if (Index == 6) N.ComponentNames = ReadComponentMap<FString>(P, [](FSWGPacket& Item) { return Item.ReadUnicodeString(); });
				else if (Index == 7) N.ComponentCreators = ReadComponentMap<uint64>(P, [](FSWGPacket& Item) { return Item.ReadUInt64(); });
				else if (Index >= 8 && Index <= 13) N.OwnerComponentFloatMaps[Index-4] = ReadComponentMap<float>(P, FloatValue);
				else if (Index >= 14 && Index <= 16) N.AmmoMaps[Index-14] = ReadComponentMap<uint32>(P, UIntValue);
				else if (Index >= 17 && Index <= 36) N.OwnerValues[Index-17] = P.ReadFloat();
				else if (Index == 37) N.DroidObjectId = P.ReadUInt64();
				else return false;
			}
			else if (Slot == 3)
			{
				switch (Index)
				{
				case 0: N.Tangible.Complexity = P.ReadFloat(); break;
				case 1: N.Tangible.ObjectName = FSWGStringId::Read(P); break;
				case 2: N.Tangible.CustomName = P.ReadUnicodeString(); break;
				case 3: N.Tangible.Volume = P.ReadInt32(); break;
				case 4: N.Tangible.CustomizationBytes = P.ReadAsciiBytes(); break;
				case 5: ApplyIndexedListChanges(ReadInt32DeltaVectorChanges(P), N.Tangible.VisibleComponents); break;
				case 6: N.Tangible.OptionsBitmask = P.ReadInt32(); break;
				case 7: N.Tangible.UseCount = P.ReadInt32(); break;
				case 8: N.Tangible.ConditionDamage = P.ReadInt32(); break;
				case 9: N.Tangible.MaxCondition = P.ReadInt32(); break;
				case 10: N.Tangible.ObjectVisible = P.ReadByte(); break;
				case 11: N.Slip = P.ReadFloat(); break;
				case 12: N.ChassisHealth = P.ReadFloat(); break;
				case 13: N.ChassisMaxHealth = P.ReadFloat(); break;
				case 14: N.ChassisCrc = P.ReadUInt32(); break;
				case 15: N.ArmorMax = ReadComponentMap<float>(P, FloatValue); break;
				case 16: N.ArmorCurrent = ReadComponentMap<float>(P, FloatValue); break;
				case 17: N.HitpointsCurrent = ReadComponentMap<float>(P, FloatValue); break;
				case 18: N.HitpointsMax = ReadComponentMap<float>(P, FloatValue); break;
				case 19: N.ComponentOptions = ReadComponentMap<uint32>(P, UIntValue); break;
				case 20: N.MaxFrontShield = P.ReadFloat(); break;
				case 21: N.MaxRearShield = P.ReadFloat(); break;
				default: return false;
				}
			}
			else if (Slot == 4)
			{
				switch (Index)
				{
				case 0: N.Mass = P.ReadFloat(); break;
				case 1: N.ChassisSpeed = P.ReadFloat(); break;
				case 2: N.CapacitorEnergy = P.ReadFloat(); break;
				case 3: N.BoosterEnergy = P.ReadFloat(); break;
				case 4: N.RefireEfficiency = ReadComponentMap<float>(P, FloatValue); break;
				default: return false;
				}
			}
			else if (Slot == 6)
			{
				if (Index == 0) N.Tangible.Unknown076 = P.ReadInt32();
				else if (Index == 1) ApplyIndexedListChanges(ReadDeltaVectorChanges<uint64>(P, [](FSWGPacket& Item) { return Item.ReadUInt64(); }), N.Tangible.DefenderList);
				else if (Index == 2) N.ShipId = P.ReadUInt16();
				else if (Index == 3) N.Flight.Acceleration = P.ReadFloat();
				else if (Index == 4) N.Flight.Deceleration = P.ReadFloat();
				else if (Index >= 5 && Index <= 7) N.Flight.AngularAcceleration[Index-5] = P.ReadFloat();
				else if (Index >= 8 && Index <= 10) N.Flight.AngularRates[Index-8] = P.ReadFloat();
				else if (Index == 11) N.Flight.MaxSpeed = P.ReadFloat();
				else if (Index == 12) N.TargetId = P.ReadUInt64();
				else if (Index == 13) N.TargetSlot = P.ReadUInt32();
				else if (Index == 14) ApplyIndexedListChanges(ReadDeltaVectorChanges<uint8>(P, [](FSWGPacket& Item) { return Item.ReadByte(); }), N.Targetable);
				else if (Index == 15)
				{
					auto Changes = ReadDeltaVectorMapChanges<TPair<uint32,uint32>>(P, [](FSWGPacket& Item)
					{ const uint32 Key = Item.ReadUInt32(); return TPair<uint32,uint32>(Key, Item.ReadUInt32()); });
					ApplyKeyedListChanges(Changes, N.ComponentCrcs, [](const auto& A, const auto& B) { return A.Key == B.Key; });
				}
				else if (Index == 16) N.WingName = P.ReadAsciiString();
				else if (Index == 17) N.TypeName = P.ReadAsciiString();
				else if (Index == 18) N.Difficulty = P.ReadAsciiString();
				else if (Index == 19) N.Faction = P.ReadAsciiString();
				else if (Index == 20) N.FrontShield = P.ReadFloat();
				else if (Index == 21) N.RearShield = P.ReadFloat();
				else if (Index == 22) N.GuildId = P.ReadUInt32();
				else return false;
			}
			else return false;
		}
		if (P.IsError() || P.GetRemaining() != 0 || !FMath::IsFinite(N.Flight.MaxSpeed)
			|| !FMath::IsFinite(N.Flight.Acceleration) || !FMath::IsFinite(N.Flight.Deceleration)
			|| N.Flight.AngularAcceleration.ContainsNaN() || N.Flight.AngularRates.ContainsNaN()) return false;
		Out = MoveTemp(N); return true;
	}
	bool ParseBase1(FSWGPacket& P, FShipObjectBaseline& Out)
	{
		FShipObjectBaseline Next = Out;
		Next.ServerObjectsStart = P.ReadInt32(); Next.ServerObjectsEnd = P.ReadInt32();
		auto FloatValue = [](FSWGPacket& Item) { return Item.ReadFloat(); };
		for (int32 Index = 0; Index < 4; ++Index) Next.OwnerComponentFloatMaps[Index] = ReadComponentMap<float>(P, FloatValue);
		Next.ComponentNames = ReadComponentMap<FString>(P, [](FSWGPacket& Item) { return Item.ReadUnicodeString(); });
		Next.ComponentCreators = ReadComponentMap<uint64>(P, [](FSWGPacket& Item) { return Item.ReadUInt64(); });
		for (int32 Index = 4; Index < 10; ++Index) Next.OwnerComponentFloatMaps[Index] = ReadComponentMap<float>(P, FloatValue);
		for (auto& Map : Next.AmmoMaps) Map = ReadComponentMap<uint32>(P, [](FSWGPacket& Item) { return Item.ReadUInt32(); });
		for (float& Value : Next.OwnerValues) Value = P.ReadFloat();
		Next.DroidObjectId = P.ReadUInt64();
		if (P.IsError() || P.GetRemaining() != 0) return false;
		Next.bHasBase1 = true; Out = MoveTemp(Next); return true;
	}
	bool ParseBase3(FSWGPacket& P, FShipObjectBaseline& Out)
	{
		FShipObjectBaseline Next = Out;
		SWGTangibleBaselineParser::ParseBase3(P, Next.Tangible);
		Next.Slip = P.ReadFloat(); Next.ChassisHealth = P.ReadFloat(); Next.ChassisMaxHealth = P.ReadFloat();
		Next.ChassisCrc = P.ReadUInt32();
		auto FloatValue = [](FSWGPacket& Item) { return Item.ReadFloat(); };
		Next.ArmorMax = ReadComponentMap<float>(P, FloatValue);
		Next.ArmorCurrent = ReadComponentMap<float>(P, FloatValue);
		Next.HitpointsCurrent = ReadComponentMap<float>(P, FloatValue);
		Next.HitpointsMax = ReadComponentMap<float>(P, FloatValue);
		Next.ComponentOptions = ReadComponentMap<uint32>(P, [](FSWGPacket& Item) { return Item.ReadUInt32(); });
		Next.MaxFrontShield = P.ReadFloat(); Next.MaxRearShield = P.ReadFloat();
		if (P.IsError() || P.GetRemaining() != 0) return false;
		Next.bHasBase3 = true; Out = MoveTemp(Next); return true;
	}
	bool ParseBase4(FSWGPacket& P, FShipObjectBaseline& Out)
	{
		FShipObjectBaseline Next = Out;
		Next.Mass = P.ReadFloat(); Next.ChassisSpeed = P.ReadFloat();
		Next.CapacitorEnergy = P.ReadFloat(); Next.BoosterEnergy = P.ReadFloat();
		Next.RefireEfficiency = ReadComponentMap<float>(P, [](FSWGPacket& Item) { return Item.ReadFloat(); });
		if (P.IsError() || P.GetRemaining() != 0) return false;
		Next.bHasBase4 = true; Out = MoveTemp(Next); return true;
	}
	bool ParseBase6(FSWGPacket& P, FShipObjectBaseline& Out)
	{
		FShipObjectBaseline Next = Out;
		SWGTangibleBaselineParser::ParseBase6(P, Next.Tangible);
		Next.ShipId = P.ReadUInt16();
		Next.Flight.Acceleration = P.ReadFloat(); Next.Flight.Deceleration = P.ReadFloat();
		for (int32 Axis = 0; Axis < 3; ++Axis) Next.Flight.AngularAcceleration[Axis] = P.ReadFloat();
		for (int32 Axis = 0; Axis < 3; ++Axis) Next.Flight.AngularRates[Axis] = P.ReadFloat();
		Next.Flight.MaxSpeed = P.ReadFloat();
		Next.TargetId = P.ReadUInt64(); Next.TargetSlot = P.ReadUInt32();
		Next.Targetable = ReadBaselineVector<uint8>(P, [](FSWGPacket& Item) { return Item.ReadByte(); });
		Next.ComponentCrcs = ReadComponentMap<uint32>(P, [](FSWGPacket& Item) { return Item.ReadUInt32(); });
		Next.WingName = P.ReadAsciiString(); Next.TypeName = P.ReadAsciiString();
		Next.Difficulty = P.ReadAsciiString(); Next.Faction = P.ReadAsciiString();
		Next.FrontShield = P.ReadFloat(); Next.RearShield = P.ReadFloat(); Next.GuildId = P.ReadUInt32();
		if (P.IsError() || P.GetRemaining() != 0 || !FMath::IsFinite(Next.Flight.MaxSpeed)
			|| !FMath::IsFinite(Next.Flight.Acceleration) || !FMath::IsFinite(Next.Flight.Deceleration)
			|| Next.Flight.AngularAcceleration.ContainsNaN() || Next.Flight.AngularRates.ContainsNaN()) return false;
		Next.bHasBase6 = true; Out = MoveTemp(Next); return true;
	}
}
