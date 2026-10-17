#pragma once
#include "Network/Messages/Zone/ObjectControllerMessage.h"

struct FJtlShipListRequest : FObjectControllerMessage
{
	uint64 TerminalId;
	FJtlShipListRequest(uint64 PlayerId, uint64 InTerminalId)
		: FObjectControllerMessage(0x41C, PlayerId, 0x0B), TerminalId(InTerminalId) {}
	FSWGPacket Serialize() const
	{
		FSWGPacket Packet = SerializeBase();
		Packet.WriteUInt32(0);
		Packet.WriteUInt64(TerminalId);
		return Packet;
	}
};

struct FJtlShipListEntry
{
	uint64 ShipId = 0;
	FString ParkingLocation;
};

struct FJtlShipListResponse
{
	static constexpr uint32 ControllerType = 0x41D;
	uint64 TerminalId = 0;
	FString PlayerLocation;
	TArray<FJtlShipListEntry> Ships;

	bool Deserialize(FSWGPacket& Packet)
	{
		const uint32 Count = Packet.ReadUInt32();
		if (Packet.IsError() || Count == 0 || Count > uint32(Packet.GetRemaining() / 10)) return false;
		FJtlShipListResponse Parsed;
		Parsed.TerminalId = Packet.ReadUInt64();
		Parsed.PlayerLocation = Packet.ReadAsciiString();
		if (Parsed.TerminalId == 0 || Packet.IsError()) return false;
		TSet<uint64> Seen;
		Seen.Add(Parsed.TerminalId);
		Parsed.Ships.Reserve(Count - 1);
		for (uint32 Index = 1; Index < Count; ++Index)
		{
			FJtlShipListEntry Entry;
			Entry.ShipId = Packet.ReadUInt64();
			Entry.ParkingLocation = Packet.ReadAsciiString();
			if (Packet.IsError() || Entry.ShipId == 0 || Seen.Contains(Entry.ShipId)) return false;
			Seen.Add(Entry.ShipId);
			Parsed.Ships.Add(MoveTemp(Entry));
		}
		if (Packet.GetRemaining() != 0) return false;
		*this = MoveTemp(Parsed);
		return true;
	}
};
