#include "Network/Messages/SWGFourCC.h"

FString SWGFourCCToString(uint32 FourCC)
{
	const TCHAR Chars[5] = {
		(TCHAR)((FourCC >> 24) & 0xFF),
		(TCHAR)((FourCC >> 16) & 0xFF),
		(TCHAR)((FourCC >> 8)  & 0xFF),
		(TCHAR)(FourCC & 0xFF),
		0
	};
	return FString(Chars);
}
