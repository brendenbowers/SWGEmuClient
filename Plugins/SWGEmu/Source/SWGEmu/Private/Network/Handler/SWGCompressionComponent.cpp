#include "Network/Handler/SWGCompressionComponent.h"
#include "Network/SWGSessionOp.h"
#include "Network/SWGCrypto.h"
#include "Network/SWGSession.h"

// Maximum decompressed payload per packet. SOE packets are bounded by MaxPacketSize
// (typically 496 bytes), so a conservative 8 KB covers realistic zlib expansion ratios.
static constexpr int32 kDecompressBufferSize = 8192;

FSWGCompressionComponent::FSWGCompressionComponent(FSWGSession* InSession)
	: HandlerComponent(FName(TEXT("SWGCompression")))
	, Session(InSession)
{
}

void FSWGCompressionComponent::Initialize()
{
	Initialized();
}

bool FSWGCompressionComponent::IsValid() const
{
	return Session != nullptr;
}

void FSWGCompressionComponent::Incoming(FBitReader& Packet)
{
	const int32 NumBytes = (int32)Packet.GetNumBytes();
	if (NumBytes < 3)
		return; // need at least op(1-2) + comp_flag(1)

	const uint8* Data = Packet.GetData();

	// Compression flag is the last byte (after CRC has already been stripped by FSWGCrcComponent).
	const uint8 CompFlag = Data[NumBytes - 1];
	const int32 DataLen = NumBytes - 1; // strip the flag byte

	// Header bytes to preserve: 2 for session packets, 1 for fastpath.
	const int32 StartOffset = (int32)SWGGetPayloadStartOffset(Data);

	if (CompFlag == SWGCompressionFlagEnabled)
	{
		const int32 CompressedLen = DataLen - StartOffset;
		if (CompressedLen <= 0)
		{
			Packet.SetError();
			return;
		}

		TArray<uint8> DecompBuf;
		DecompBuf.SetNumUninitialized(kDecompressBufferSize);

		const int32 DecompLen = FSWGCrypto::Decompress(
			Data + StartOffset, CompressedLen,
			DecompBuf.GetData(), DecompBuf.Num());

		if (DecompLen <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGCompressionComponent: decompress failed, dropping packet"));
			Packet.SetError();
			return;
		}

		// Reassemble: [header bytes][decompressed payload]
		TArray<uint8> Result;
		Result.SetNumUninitialized(StartOffset + DecompLen);
		FMemory::Memcpy(Result.GetData(), Data, StartOffset);
		FMemory::Memcpy(Result.GetData() + StartOffset, DecompBuf.GetData(), DecompLen);

		Packet.SetData(MoveTemp(Result), (int64)(StartOffset + DecompLen) * 8);
	}
	else
	{
		// Not compressed — strip the flag byte only.
		TArray<uint8> Trimmed(Data, DataLen);
		Packet.SetData(MoveTemp(Trimmed), (int64)DataLen * 8);
	}
}

void FSWGCompressionComponent::Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits)
{

	uint8 Flag = SWGCompressionFlagDisabled;
	if (!Traits.bAllowCompression)
	{
		Packet.Serialize(&Flag, 1);
		return;
	}

	const int32 NumBytes    = (int32)Packet.GetNumBytes();
	const uint8* RawData    = Packet.GetData();
	const int32 StartOffset = (int32)SWGGetPayloadStartOffset(RawData);
	const int32 PayloadLen  = NumBytes - StartOffset;

	if (PayloadLen <= 0)
	{
		Packet.Serialize(&Flag, 1);
		return;
	}

	// Copy header bytes before resetting Packet (Reset() would invalidate RawData).
	TArray<uint8> Header(RawData, StartOffset);

	// Try to compress the payload region. Compress() returns 0 if compression
	// would increase the size, so we use that as the no-benefit signal.
	TArray<uint8> CompressedBuf;
	CompressedBuf.SetNumUninitialized(NumBytes + 64);

	const int32 CompressedLen = FSWGCrypto::Compress(
		RawData + StartOffset, PayloadLen,
		CompressedBuf.GetData(), CompressedBuf.Num());

	if (CompressedLen > 0)
	{
		// Compression helped — rebuild packet: [header][compressed_payload][0x01]
		Packet.Reset();
		Packet.Serialize(Header.GetData(), StartOffset);
		Packet.Serialize(CompressedBuf.GetData(), CompressedLen);
		Flag = SWGCompressionFlagEnabled;
	}

	Packet.Serialize(&Flag, 1);
}

int32 FSWGCompressionComponent::GetReservedPacketBits() const
{
	return 8; // 1 flag byte appended on outgoing
}
