#include "Customization/SWGCustomizationVariables.h"

bool FSWGCustomizationVariables::Parse(const TArray<uint8>& RawBytes, FSWGCustomizationVariables& OutResult)
{
	OutResult.Values.Empty();

	if (RawBytes.Num() == 0)
	{
		return true; // Core3's getData() emits nothing for an empty variable set.
	}
	if (RawBytes.Num() < 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCustomizationVariables: %d byte(s) is too short for a header"), RawBytes.Num());
		return false;
	}

	int32 Offset = 0;
	bool bError = false;

	// Mirrors CustomizationVariables::parseFromClientString's nextByte lambda exactly.
	auto NextByte = [&RawBytes, &Offset, &bError]() -> uint8
	{
		if (bError || !RawBytes.IsValidIndex(Offset))
		{
			bError = true;
			return 0;
		}
		uint8 Byte = RawBytes[Offset++];
		if (Byte == 0xFF)
		{
			if (!RawBytes.IsValidIndex(Offset))
			{
				bError = true;
				return 0;
			}
			const uint8 Escape = RawBytes[Offset++];
			switch (Escape)
			{
				case 0x01: Byte = 0x00; break;
				case 0x02: Byte = 0xFF; break;
				default:
					// 0x03 (EOS) or anything else here means the stream ended
					// (or was corrupt) where a data byte was expected.
					bError = true;
					return 0;
			}
		}
		return Byte;
	};

	const uint8 Unknown = NextByte();
	if (bError)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCustomizationVariables: truncated header"));
		return false;
	}
	if (Unknown != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCustomizationVariables: unexpected header byte %d (expected 1)"), Unknown);
	}

	const uint8 TotalVars = NextByte();
	if (bError)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCustomizationVariables: truncated count byte"));
		return false;
	}
	if (TotalVars == 0xFF)
	{
		return true; // Explicit "empty" marker.
	}

	for (uint8 i = 0; i < TotalVars; ++i)
	{
		uint8 Type = NextByte();
		int16 Value = (int16)NextByte();
		if (bError)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGCustomizationVariables: truncated while reading variable %d/%d"), i + 1, TotalVars);
			OutResult.Values.Empty();
			return false;
		}

		if (Type & 0x80)
		{
			Type &= 0x7F;
			const uint8 HighByte = NextByte();
			if (bError)
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGCustomizationVariables: truncated high byte for variable %d/%d"), i + 1, TotalVars);
				OutResult.Values.Empty();
				return false;
			}
			Value = (int16)(Value | (HighByte << 8));
		}

		OutResult.Values.Add(Type, Value);
	}

	// Whatever's left must be exactly the raw (unescaped) EOS marker.
	if (RawBytes.Num() - Offset != 2 || RawBytes[Offset] != 0xFF || RawBytes[Offset + 1] != 0x03)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCustomizationVariables: missing/misplaced EOS marker after %d variable(s), %d byte(s) unconsumed"),
			TotalVars, RawBytes.Num() - Offset);
		OutResult.Values.Empty();
		return false;
	}

	return true;
}
