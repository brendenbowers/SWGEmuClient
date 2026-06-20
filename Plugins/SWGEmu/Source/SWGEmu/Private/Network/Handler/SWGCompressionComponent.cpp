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
	// If the caller has marked this packet as not compressible (e.g. handshake packets),
	// skip compression and append the disabled flag.
	// Phase 1: never compress regardless — full zlib outgoing is Phase 2.
	// In Phase 2: compress here, set Traits.bIsCompressed = true, flag = SWGCompressionFlagEnabled.
	(void)Traits.bAllowCompression; // consulted in Phase 2
	Traits.bIsCompressed = false;

	uint8 Flag = SWGCompressionFlagDisabled;
	Packet.Serialize(&Flag, 1);
}

int32 FSWGCompressionComponent::GetReservedPacketBits() const
{
	return 8; // 1 flag byte appended on outgoing
}
