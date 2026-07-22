#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGIffReader.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	uint32 ReadUInt32LE(const uint8* Bytes)
	{
		return (uint32)Bytes[0] | ((uint32)Bytes[1] << 8) | ((uint32)Bytes[2] << 16) | ((uint32)Bytes[3] << 24);
	}
}

void USWGTreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!TreDirectory.IsEmpty() && AutoLoad)
	{
		LoadArchives(TreDirectory);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("USWGTreSubsystem: TreDirectory not configured (set via DefaultGame.ini or call LoadArchives() explicitly)"));
	}
}

void USWGTreSubsystem::Deinitialize()
{
	Archives.Reset();
	VirtualPathToArchiveIndex.Reset();
	CrcToTemplatePath.Reset();

	Super::Deinitialize();
}

bool USWGTreSubsystem::LoadArchives(const FString& Directory)
{
	Archives.Reset();
	VirtualPathToArchiveIndex.Reset();
	CrcToTemplatePath.Reset();

	if (!FPaths::DirectoryExists(Directory))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTreSubsystem: directory not found: %s"), *Directory);
		return false;
	}

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *(Directory / TEXT("*.tre")), true, false);
	FoundFiles.Sort(); // filenames are zero-padded (patch_00 .. patch_14), so lexicographic == load/override order

	if (FoundFiles.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTreSubsystem: no .tre files found in %s"), *Directory);
		return false;
	}

	for (const FString& FileName : FoundFiles)
	{
		const FString FullPath = Directory / FileName;

		TUniquePtr<FSWGTreArchive> Archive = MakeUnique<FSWGTreArchive>();
		if (Archive->LoadIndex(FullPath))
		{
			Archives.Add(MoveTemp(Archive));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGTreSubsystem: failed to index %s"), *FullPath);
		}
	}

	BuildVirtualFileTable();
	BuildCrcTable();

	UE_LOG(LogTemp, Log, TEXT("USWGTreSubsystem: loaded %d archives from %s (%d templates resolvable by CRC)"),
		Archives.Num(), *Directory, CrcToTemplatePath.Num());

	return Archives.Num() > 0;
}

FSWGIffReader USWGTreSubsystem::CreateIffReader(const FString& VirtualPath) const
{
	TArray<uint8> Bytes = ExtractFile(VirtualPath);
	FSWGIffReader Reader(Bytes);
	return Reader;
}

void USWGTreSubsystem::BuildVirtualFileTable()
{
	VirtualPathToArchiveIndex.Reset();

	for (int32 ArchiveIndex = 0; ArchiveIndex < Archives.Num(); ++ArchiveIndex)
	{
		for (const FSWGTreRecord& Record : Archives[ArchiveIndex]->GetRecords())
		{
			VirtualPathToArchiveIndex.Add(Record.Name, ArchiveIndex); // later (higher-priority) archive overwrites
		}
	}
}

bool USWGTreSubsystem::FileExists(const FString& VirtualPath) const
{
	return VirtualPathToArchiveIndex.Contains(VirtualPath);
}

TArray<FString> USWGTreSubsystem::FindVirtualPaths(const FString& Substring) const
{
	TArray<FString> Result;
	for (const TPair<FString, int32>& Entry : VirtualPathToArchiveIndex)
	{
		if (Entry.Key.Contains(Substring))
		{
			Result.Add(Entry.Key);
		}
	}
	return Result;
}

TArray<uint8> USWGTreSubsystem::ExtractFile(const FString& VirtualPath) const
{
	const int32* ArchiveIndex = VirtualPathToArchiveIndex.Find(VirtualPath);
	if (!ArchiveIndex)
		return {};

	const FSWGTreArchive& Archive = *Archives[*ArchiveIndex];
	const FSWGTreRecord* Record = Archive.FindRecord(VirtualPath);
	if (!Record)
		return {};

	return Archive.Extract(*Record);
}

FString USWGTreSubsystem::ResolveTemplatePath(uint32 Crc) const
{
	if (const FString* Path = CrcToTemplatePath.Find(Crc))
		return *Path;
	return FString();
}

void USWGTreSubsystem::BuildCrcTable()
{
	CrcToTemplatePath.Reset();

	TArray<uint8> Bytes = ExtractFile(TEXT("misc/object_template_crc_string_table.iff"));
	if (Bytes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTreSubsystem: object_template_crc_string_table.iff not found, CRC lookups unavailable"));
		return;
	}

	FSWGIffReader Reader(Bytes);
	FSWGIffChunk RootChunk;
	if (!Reader.FindForm(SWG_IFF_TAG('0','0','0','0'), RootChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTreSubsystem: CRC table FORM 0000 not found"));
		return;
	}

	const uint8* DataBytes = nullptr;
	const uint8* CrctBytes = nullptr;
	const uint8* StrtBytes = nullptr;
	const uint8* StngBytes = nullptr;
	int32 CrctSize = 0, StrtSize = 0, StngSize = 0;

	for (const FSWGIffChunk& Child : Reader.ReadChildren(RootChunk))
	{
		if (Child.Tag == SWGIffTags::Data)      DataBytes = Reader.GetChunkData(Child);
		else if (Child.Tag == SWG_IFF_TAG('C','R','C','T')) { CrctBytes = Reader.GetChunkData(Child); CrctSize = Child.DataSize; }
		else if (Child.Tag == SWG_IFF_TAG('S','T','R','T')) { StrtBytes = Reader.GetChunkData(Child); StrtSize = Child.DataSize; }
		else if (Child.Tag == SWG_IFF_TAG('S','T','N','G')) { StngBytes = Reader.GetChunkData(Child); StngSize = Child.DataSize; }
	}

	if (!DataBytes || !CrctBytes || !StrtBytes || !StngBytes)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTreSubsystem: CRC table missing an expected chunk (DATA/CRCT/STRT/STNG)"));
		return;
	}

	const uint32 Count = ReadUInt32LE(DataBytes);
	CrcToTemplatePath.Reserve((int32)Count);

	for (uint32 i = 0; i < Count; ++i)
	{
		if ((int32)((i + 1) * 4) > CrctSize || (int32)((i + 1) * 4) > StrtSize)
			break;

		const uint32 Crc = ReadUInt32LE(CrctBytes + i * 4);
		const uint32 Offset = ReadUInt32LE(StrtBytes + i * 4);

		int32 End = (int32)Offset;
		while (End < StngSize && StngBytes[End] != 0)
			++End;

		FString Path = FString::ConstructFromPtrSize((const ANSICHAR*)(StngBytes + Offset), End - (int32)Offset);
		CrcToTemplatePath.Add(Crc, MoveTemp(Path));
	}
}
