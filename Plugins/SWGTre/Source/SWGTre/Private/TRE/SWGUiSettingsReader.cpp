#include "TRE/SWGUiSettingsReader.h"

#include "TRE/SWGIffReader.h"
#include "TRE/SWGIFFChunkReader.h"

namespace
{
	/** A DATS/INTS/STRS record: name\0, little-endian int32 length, then that many bytes. */
	bool ReadRecord(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, FString& OutName, FString& OutValue)
	{
		FSWGIFFChunkReader Record(Chunk, Reader);
		int32 Length = 0;
		if (!Record.ReadTerminiatedString(OutName) || !Record.ReadValueLE(Length) || Length < 0 || !Record.CanRead(Length))
		{
			return false;
		}

		const uint8* Bytes = Reader.GetChunkData(Chunk) + Record.GetPosition();
		OutValue = FString::ConstructFromPtrSize(reinterpret_cast<const ANSICHAR*>(Bytes), Length);
		return true;
	}

	/** Splits "itemStr_2_10" into ("itemStr", 2, 10). */
	bool ParseSlotKey(const FString& Name, FString& OutField, int32& OutPane, int32& OutSlot)
	{
		TArray<FString> Parts;
		Name.ParseIntoArray(Parts, TEXT("_"));
		if (Parts.Num() != 3 || !Parts[1].IsNumeric() || !Parts[2].IsNumeric())
		{
			return false;
		}
		OutField = Parts[0];
		OutPane  = FCString::Atoi(*Parts[1]);
		OutSlot  = FCString::Atoi(*Parts[2]);
		return true;
	}
}

bool FSWGUiSettingsReader::ReadToolbar(const TArray<uint8>& Data, TArray<FSWGToolbarSlot>& OutSlots)
{
	OutSlots.Reset();

	FSWGIffReader Reader(Data);
	if (Reader.GetRootFormType() != FName(TEXT("UIST")))
	{
		return false;
	}

	FSWGIffChunk Root;
	if (!Reader.FindForm(SWG_IFF_TAG('U', 'I', 'S', 'T'), Root))
	{
		return false;
	}

	// The version form (0003 here) holds one FORM OWNE per page.
	for (const FSWGIffChunk& VersionForm : Reader.FindChildForms(Root))
	{
		for (const FSWGIffChunk& Page : Reader.FindChildForms(VersionForm))
		{
			if (Page.FormType != SWG_IFF_TAG('O', 'W', 'N', 'E'))
			{
				continue;
			}

			FSWGIffChunk NameChunk;
			if (!Reader.FindChildChunk(Page, SWG_IFF_TAG('N', 'A', 'M', 'E'), NameChunk)
				|| FSWGIFFChunkReader(NameChunk, Reader).ReadTerminiatedString() != TEXT("SwgCuiToolbar"))
			{
				continue;
			}

			FSWGIffChunk Records;
			if (!Reader.FindChildForm(Page, SWG_IFF_TAG('D', 'A', 'T', 'S'), Records))
			{
				return false;
			}

			// Records arrive per field, not per slot, so gather by (pane, slot).
			TMap<FIntPoint, FSWGToolbarSlot> Slots;
			TArray<FIntPoint> Order;
			for (const FSWGIffChunk& Record : Reader.FindAllChildChunks(Records, SWG_IFF_TAG('D', 'A', 'T', 'A')))
			{
				FString Name, Value, Field;
				int32 Pane = 0, Slot = 0;
				if (!ReadRecord(Reader, Record, Name, Value) || !ParseSlotKey(Name, Field, Pane, Slot))
				{
					continue;
				}

				const FIntPoint Key(Pane, Slot);
				FSWGToolbarSlot* Entry = Slots.Find(Key);
				if (!Entry)
				{
					Entry = &Slots.Add(Key);
					Entry->Pane = Pane;
					Entry->Slot = Slot;
					Order.Add(Key);
				}

				if (Field == TEXT("itemType"))
				{
					Entry->Type = Value;
				}
				else if (Field == TEXT("itemStr"))
				{
					Entry->Text = Value;
				}
				else if (Field == TEXT("itemId"))
				{
					Entry->ObjectId = FCString::Atoi64(*Value);
				}
			}

			for (const FIntPoint& Key : Order)
			{
				OutSlots.Add(Slots[Key]);
			}
			return true;
		}
	}

	return false;
}

FString FSWGUiSettingsReader::MakeUiSettingsPath(const FString& ClientDirectory, const FString& Account, const FString& Galaxy, int64 CharacterId)
{
	return FPaths::Combine(ClientDirectory, TEXT("profiles"), Account, Galaxy, FString::Printf(TEXT("%lld.uis"), CharacterId));
}
