#include "TRE/SWGWorldSnapshotReader.h"

namespace
{
	// Same convention confirmed for terrain (.trn) DATA chunks: the IFF
	// container itself is big-endian (chunk tags/sizes), but payload data
	// within a leaf chunk is little-endian (matching the original x86
	// authoring tools) — see SWGTerrainReader.cpp's ReadFloatLE/ReadUInt32LE.
	float ReadFloatLE(const uint8* Data, int32 Offset)
	{
		uint32 Bits = Data[Offset] | (Data[Offset + 1] << 8) | (Data[Offset + 2] << 16) | (Data[Offset + 3] << 24);
		float Result;
		FMemory::Memcpy(&Result, &Bits, sizeof(float));
		return Result;
	}

	uint32 ReadUInt32LE(const uint8* Data, int32 Offset)
	{
		return Data[Offset] | (Data[Offset + 1] << 8) | (Data[Offset + 2] << 16) | (Data[Offset + 3] << 24);
	}
}

bool FSWGWorldSnapshotReader::ReadWorldSnapshot(const FSWGIffReader& Reader, FSWGWorldSnapshotData& OutData)
{
	TArray<FSWGIffChunk> TopChunks = Reader.ReadChunks();
	if (TopChunks.Num() == 0 || !TopChunks[0].IsForm() || TopChunks[0].FormType != TEXT("WSNP"))
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGWorldSnapshotReader: top-level chunk is not FORM WSNP"));
		return false;
	}

	TArray<FSWGIffChunk> WsnpChildren = Reader.ReadChildren(TopChunks[0]);
	if (WsnpChildren.Num() == 0 || !WsnpChildren[0].IsForm())
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGWorldSnapshotReader: WSNP has no version FORM"));
		return false;
	}

	const FSWGIffChunk& VersionForm = WsnpChildren[0];
	if (VersionForm.FormType != TEXT("0001"))
	{
		// Matches Core3's own WorldSnapshotIff::readObject — only version 0001
		// is handled; 0000 (and anything else) is an explicit unhandled case there too.
		UE_LOG(LogTemp, Error, TEXT("FSWGWorldSnapshotReader: unhandled WSNP version '%s'"), *VersionForm.FormType);
		return false;
	}

	TArray<FSWGIffChunk> VersionChildren = Reader.ReadChildren(VersionForm);

	int32 NodeCount = 0;
	for (const FSWGIffChunk& Child : VersionChildren)
	{
		if (Child.IsForm() && Child.FormType == TEXT("NODS"))
		{
			TArray<FSWGIffChunk> NodeForms = Reader.ReadChildren(Child);
			for (const FSWGIffChunk& NodeForm : NodeForms)
			{
				if (!NodeForm.IsForm() || NodeForm.Tag != TEXT("FORM"))
					continue;

				FSWGWorldSnapshotNode Node;
				if (ReadNode(Reader, NodeForm, Node))
				{
					OutData.Nodes.Add(MoveTemp(Node));
					++NodeCount;
				}
			}
		}
		else if (!Child.IsForm() && Child.Tag == TEXT("OTNL"))
		{
			const uint8* Data = Reader.GetChunkData(Child);
			const int32 Size = Reader.GetChunkSize(Child);
			if (Size < 4)
				continue;

			const int32 Count = (int32)ReadUInt32LE(Data, 0);
			int32 Offset = 4;
			OutData.ObjectTemplateNames.Reserve(Count);
			for (int32 i = 0; i < Count && Offset < Size; ++i)
			{
				// Null-terminated string (Chunk::readString in Core3) — read
				// bytes until 0x00, matching FString::FromUTF8/ANSI conventions.
				const int32 Start = Offset;
				while (Offset < Size && Data[Offset] != 0)
					++Offset;

				FString Name(Offset - Start, reinterpret_cast<const ANSICHAR*>(Data + Start));
				OutData.ObjectTemplateNames.Add(MoveTemp(Name));
				++Offset; // skip the null terminator
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("FSWGWorldSnapshotReader: parsed %d top-level node(s), %d object template name(s)"),
		NodeCount, OutData.ObjectTemplateNames.Num());

	return true;
}

bool FSWGWorldSnapshotReader::ReadNode(const FSWGIffReader& Reader, const FSWGIffChunk& NodeForm, FSWGWorldSnapshotNode& OutNode)
{
	TArray<FSWGIffChunk> NodeChildren = Reader.ReadChildren(NodeForm);
	if (NodeChildren.Num() == 0 || !NodeChildren[0].IsForm())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGWorldSnapshotReader: NODE has no version FORM"));
		return false;
	}

	const FSWGIffChunk& VersionForm = NodeChildren[0];
	if (VersionForm.FormType != TEXT("0000"))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGWorldSnapshotReader: unhandled NODE version '%s'"), *VersionForm.FormType);
		return false;
	}

	TArray<FSWGIffChunk> VersionChildren = Reader.ReadChildren(VersionForm);
	if (VersionChildren.Num() == 0 || VersionChildren[0].IsForm() || VersionChildren[0].Tag != TEXT("DATA"))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGWorldSnapshotReader: NODE version form's first child isn't DATA"));
		return false;
	}

	const FSWGIffChunk& DataChunk = VersionChildren[0];
	const uint8* D = Reader.GetChunkData(DataChunk);
	const int32 DSize = Reader.GetChunkSize(DataChunk);
	if (DSize < 52)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGWorldSnapshotReader: NODE DATA chunk too small (%d bytes)"), DSize);
		return false;
	}

	OutNode.ObjectID = ReadUInt32LE(D, 0);
	OutNode.ParentID = ReadUInt32LE(D, 4);
	OutNode.NameID = ReadUInt32LE(D, 8);
	OutNode.CellID = ReadUInt32LE(D, 12);

	const float QW = ReadFloatLE(D, 16);
	const float QX = ReadFloatLE(D, 20);
	const float QY = ReadFloatLE(D, 24);
	const float QZ = ReadFloatLE(D, 28);
	// Same Y/Z relabeling as position below (SWG Y-up -> UE Z-up), but a plain
	// component swap alone represents a *reflection* (Y-up -> Z-up via a
	// 2-axis swap has determinant -1, not a proper rotation) — for a pure-yaw
	// quaternion (cos,0,sin,0) that means the surviving axis component must
	// also be negated to preserve the actual rotation angle/sense instead of
	// its mirror image (negating just the sin part is exactly the "-theta"
	// quaternion). Static building placements are yaw-only, so this is the
	// component that visibly flips a building's facing direction — confirmed
	// wrong before this fix (starport/SWGBuilding1 faced backwards).
	OutNode.Direction = FQuat(QX, QZ, -QY, QW);

	const float X = ReadFloatLE(D, 32);
	const float Z = ReadFloatLE(D, 36);
	const float Y = ReadFloatLE(D, 40);
	OutNode.Position = FVector(X, Y, Z);

	OutNode.GameObjectType = ReadFloatLE(D, 44);
	OutNode.Unknown2 = ReadUInt32LE(D, 48);

	// Remaining children (after DATA) are nested NODE forms — matches Core3's
	// "versionForm->getChunksSize() - 1" child-node count exactly.
	for (int32 i = 1; i < VersionChildren.Num(); ++i)
	{
		const FSWGIffChunk& ChildForm = VersionChildren[i];
		if (!ChildForm.IsForm() || ChildForm.Tag != TEXT("FORM"))
			continue;

		FSWGWorldSnapshotNode ChildNode;
		if (ReadNode(Reader, ChildForm, ChildNode))
		{
			OutNode.Children.Add(MoveTemp(ChildNode));
		}
	}

	return true;
}
