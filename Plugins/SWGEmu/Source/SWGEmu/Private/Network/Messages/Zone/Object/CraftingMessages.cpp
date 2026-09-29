#include "Network/Messages/Zone/Object/CraftingMessages.h"
#include "Network/SWGPacket.h"

// ── Server → client ───────────────────────────────────────────────────────

bool FSWGCraftingSchematicListIn::Parse(FSWGPacket& Packet)
{
	ToolId = Packet.ReadUInt64();
	StationId = Packet.ReadUInt64();

	const int32 Count = Packet.ReadInt32();
	if (Packet.IsError() || Count < 0 || Count > 10000)
	{
		return false;
	}

	Schematics.Reset(Count);
	for (int32 Index = 0; Index < Count && !Packet.IsError(); ++Index)
	{
		FSWGCraftingSchematicEntry& Entry = Schematics.AddDefaulted_GetRef();
		Entry.SchematicCrc = static_cast<int32>(Packet.ReadUInt32());
		Packet.ReadUInt32(); // crc, repeated
		Entry.ToolTab = static_cast<int32>(Packet.ReadUInt32());
	}
	return !Packet.IsError();
}

bool FSWGCraftingDraftSlotsIn::Parse(FSWGPacket& Packet)
{
	SchematicCrc = Packet.ReadUInt32();
	Packet.ReadUInt32(); // crc, repeated
	Complexity = Packet.ReadInt32();
	Size = Packet.ReadInt32();
	Packet.ReadByte(); // constant 2

	const int32 SlotCount = Packet.ReadInt32();
	if (Packet.IsError() || SlotCount < 0 || SlotCount > 1000)
	{
		return false;
	}

	Slots.Reset(SlotCount);
	for (int32 Index = 0; Index < SlotCount && !Packet.IsError(); ++Index)
	{
		FSWGDraftSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Deserialize(Packet);
	}
	Packet.ReadInt16(); // terminator

	return !Packet.IsError();
}

bool FSWGCraftingResourceWeightsIn::Parse(FSWGPacket& Packet)
{
	SchematicCrc = Packet.ReadUInt32();
	Packet.ReadUInt32(); // crc, repeated

	const uint8 BatchCount = Packet.ReadByte();
	BatchWeights.Reset(BatchCount);
	for (uint8 Index = 0; Index < BatchCount && !Packet.IsError(); ++Index)
	{
		FSWGResourceWeight& Weight = BatchWeights.AddDefaulted_GetRef();
		Weight.Deserialize(Packet);
	}

	const uint8 WeightCount = Packet.ReadByte();
	Weights.Reset(WeightCount);
	for (uint8 Index = 0; Index < WeightCount && !Packet.IsError(); ++Index)
	{
		FSWGResourceWeight& Weight = Weights.AddDefaulted_GetRef();
		Weight.Deserialize(Packet);
	}

	return !Packet.IsError();
}

bool FSWGCraftingIngredientSlotsIn::Parse(FSWGPacket& Packet)
{
	ToolId = Packet.ReadUInt64();
	ManufactureSchematicId = Packet.ReadUInt64();
	PrototypeId = Packet.ReadUInt64();
	Packet.ReadInt32(); // constant 2
	AllowFactory = Packet.ReadByte();

	const int32 SlotCount = Packet.ReadInt32();
	if (Packet.IsError() || SlotCount < 0 || SlotCount > 1000)
	{
		return false;
	}

	Slots.Reset(SlotCount);
	for (int32 Index = 0; Index < SlotCount && !Packet.IsError(); ++Index)
	{
		FSWGDraftSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Deserialize(Packet);
	}
	Packet.ReadInt16(); // terminator

	return !Packet.IsError();
}

bool FSWGCraftingStatusIn::Parse(FSWGPacket& Packet)
{
	SubType = Packet.ReadInt32();
	Value = Packet.ReadInt32();
	Counter = Packet.ReadByte();
	return !Packet.IsError();
}

bool FSWGCraftingCloseWindowIn::Parse(FSWGPacket& Packet)
{
	Counter = Packet.ReadByte();
	return !Packet.IsError();
}

// ── Client → server ──────────────────────────────────────────────────────

FSWGCraftingExperimentMessage::FSWGCraftingExperimentMessage(uint64 PlayerId, uint8 InCounter, const TArray<FSWGCraftingExperimentRow>& InRows)
	: FObjectControllerMessage(0x106u, PlayerId, 0x0Bu)
	, Counter(InCounter)
	, Rows(InRows)
{}

FSWGPacket FSWGCraftingExperimentMessage::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);
	Pkt.WriteUInt32(0); // "size" — parsed and ignored by the server
	Pkt.WriteByte(Counter);
	Pkt.WriteInt32(Rows.Num());
	for (const FSWGCraftingExperimentRow& Row : Rows)
	{
		Pkt.WriteInt32(Row.RowIndex);
		Pkt.WriteInt32(Row.Points);
	}
	return Pkt;
}

FSWGCraftingAddIngredientMessage::FSWGCraftingAddIngredientMessage(uint64 PlayerId, uint64 InIngredientObjectId, int32 InSlot, uint8 InCounter)
	: FObjectControllerMessage(0x107u, PlayerId, 0x0Bu)
	, IngredientObjectId(InIngredientObjectId)
	, Slot(InSlot)
	, Counter(InCounter)
{}

FSWGPacket FSWGCraftingAddIngredientMessage::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);
	Pkt.WriteUInt32(0); // "size" — parsed and ignored by the server
	Pkt.WriteUInt64(IngredientObjectId);
	Pkt.WriteInt32(Slot);
	Pkt.WriteUInt32(0); // unused second int
	Pkt.WriteByte(Counter);
	return Pkt;
}

FSWGCraftingRemoveIngredientMessage::FSWGCraftingRemoveIngredientMessage(uint64 PlayerId, int32 InSlot, uint64 InIngredientObjectId, uint8 InCounter)
	: FObjectControllerMessage(0x108u, PlayerId, 0x0Bu)
	, Slot(InSlot)
	, IngredientObjectId(InIngredientObjectId)
	, Counter(InCounter)
{}

FSWGPacket FSWGCraftingRemoveIngredientMessage::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);
	Pkt.WriteUInt32(0); // "size" — parsed and ignored by the server
	Pkt.WriteInt32(Slot);
	Pkt.WriteUInt64(IngredientObjectId);
	Pkt.WriteByte(Counter);
	return Pkt;
}

FSWGCraftingCustomizeMessage::FSWGCraftingCustomizeMessage(uint64 PlayerId, const FString& InName, uint8 InTemplateChoice,
	int32 InSchematicCount, const TArray<FSWGCraftingCustomizationEdit>& InEdits)
	: FObjectControllerMessage(0x15Au, PlayerId, 0x0Bu)
	, Name(InName)
	, TemplateChoice(InTemplateChoice)
	, SchematicCount(InSchematicCount)
	, Edits(InEdits)
{}

FSWGPacket FSWGCraftingCustomizeMessage::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);
	Pkt.WriteUInt32(0); // "size" — parsed and ignored by the server
	Pkt.WriteUnicodeString(Name);
	Pkt.WriteByte(TemplateChoice);
	Pkt.WriteInt32(SchematicCount);
	Pkt.WriteByte(static_cast<uint8>(Edits.Num()));
	for (const FSWGCraftingCustomizationEdit& Edit : Edits)
	{
		Pkt.WriteInt32(Edit.Index);
		Pkt.WriteInt32(Edit.Value);
	}
	return Pkt;
}
