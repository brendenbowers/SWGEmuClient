#include "TRE/SWGTreArchive.h"
#include "Network/SWGCrypto.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"

uint32 FSWGTreArchive::ReadUInt32LE(const uint8* Bytes)
{
	return (uint32)Bytes[0] | ((uint32)Bytes[1] << 8) | ((uint32)Bytes[2] << 16) | ((uint32)Bytes[3] << 24);
}

bool FSWGTreArchive::LoadIndex(const FString& InFilePath)
{
	FilePath = InFilePath;
	Records.Reset();
	NameToIndex.Reset();

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> File(PlatformFile.OpenRead(*FilePath));
	if (!File.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGTreArchive: failed to open %s"), *FilePath);
		return false;
	}

	uint8 Header[36];
	if (!File->Read(Header, sizeof(Header)))
		return false;

	const FString Magic = FString::ConstructFromPtrSize((const ANSICHAR*)Header, 4);
	const FString Version = FString::ConstructFromPtrSize((const ANSICHAR*)Header + 4, 4);
	if ((Magic != TEXT("EERT") && Magic != TEXT("TREE")) || (Version != TEXT("5000") && Version != TEXT("0005")))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGTreArchive: %s has unrecognized header (%s/%s)"), *FilePath, *Magic, *Version);
		return false;
	}

	const uint32 TotalRecords   = ReadUInt32LE(Header + 8);
	const uint32 DataOffset     = ReadUInt32LE(Header + 12);
	const uint32 RecComp        = ReadUInt32LE(Header + 16);
	const uint32 RecCompSize    = ReadUInt32LE(Header + 20);
	const uint32 NameComp       = ReadUInt32LE(Header + 24);
	const uint32 NameCompSize   = ReadUInt32LE(Header + 28);
	const uint32 NameUncompSize = ReadUInt32LE(Header + 32);

	if (TotalRecords == 0)
		return true; // valid, just empty

	if (!File->Seek((int64)DataOffset))
		return false;

	const int32 RecUncompSize = 24 * (int32)TotalRecords;
	TArray<uint8> RecData;
	RecData.SetNumUninitialized(RecUncompSize);

	if (RecComp == 2)
	{
		TArray<uint8> Compressed;
		Compressed.SetNumUninitialized((int32)RecCompSize);
		if (!File->Read(Compressed.GetData(), Compressed.Num()))
			return false;

		if (FSWGCrypto::Decompress(Compressed.GetData(), Compressed.Num(), RecData.GetData(), RecData.Num()) != RecData.Num())
			return false;
	}
	else
	{
		if (!File->Read(RecData.GetData(), RecData.Num()))
			return false;
	}

	TArray<uint8> NameData;
	NameData.SetNumUninitialized((int32)NameUncompSize);

	if (NameComp == 2)
	{
		TArray<uint8> Compressed;
		Compressed.SetNumUninitialized((int32)NameCompSize);
		if (!File->Read(Compressed.GetData(), Compressed.Num()))
			return false;

		if (FSWGCrypto::Decompress(Compressed.GetData(), Compressed.Num(), NameData.GetData(), NameData.Num()) != NameData.Num())
			return false;
	}
	else
	{
		if (!File->Read(NameData.GetData(), NameData.Num()))
			return false;
	}

	Records.Reserve((int32)TotalRecords);
	for (uint32 i = 0; i < TotalRecords; ++i)
	{
		const int32 O = (int32)i * 24;
		const uint32 NameOffset = ReadUInt32LE(RecData.GetData() + O + 20);

		int32 End = (int32)NameOffset;
		while (End < NameData.Num() && NameData[End] != 0)
			++End;

		FSWGTreRecord Record;
		Record.Name = FString::ConstructFromPtrSize((const ANSICHAR*)(NameData.GetData() + NameOffset), End - (int32)NameOffset);
		Record.UncompSize = ReadUInt32LE(RecData.GetData() + O + 4);
		Record.FileOffset = ReadUInt32LE(RecData.GetData() + O + 8);
		Record.CompType   = ReadUInt32LE(RecData.GetData() + O + 12);
		Record.CompSize   = ReadUInt32LE(RecData.GetData() + O + 16);

		NameToIndex.Add(Record.Name, Records.Num());
		Records.Add(MoveTemp(Record));
	}

	return true;
}

const FSWGTreRecord* FSWGTreArchive::FindRecord(const FString& VirtualPath) const
{
	if (const int32* Index = NameToIndex.Find(VirtualPath))
		return &Records[*Index];
	return nullptr;
}

TArray<uint8> FSWGTreArchive::Extract(const FSWGTreRecord& Record) const
{
	TArray<uint8> Result;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> File(PlatformFile.OpenRead(*FilePath));
	if (!File.IsValid())
		return Result;

	if (!File->Seek((int64)Record.FileOffset))
		return Result;

	if (Record.CompType == 2)
	{
		TArray<uint8> Compressed;
		Compressed.SetNumUninitialized((int32)Record.CompSize);
		if (!File->Read(Compressed.GetData(), Compressed.Num()))
			return Result;

		Result.SetNumUninitialized((int32)Record.UncompSize);
		const int32 Decompressed = FSWGCrypto::Decompress(Compressed.GetData(), Compressed.Num(), Result.GetData(), Result.Num());
		if (Decompressed != Result.Num())
			Result.Reset();
	}
	else
	{
		Result.SetNumUninitialized((int32)Record.UncompSize);
		if (!File->Read(Result.GetData(), Result.Num()))
			Result.Reset();
	}

	return Result;
}
