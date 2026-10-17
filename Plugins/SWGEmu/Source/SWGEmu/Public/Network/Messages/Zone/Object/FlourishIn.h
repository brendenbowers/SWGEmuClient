#pragma once

#include "CoreMinimal.h"

struct FSWGPacket;

/**
 * An entertainer performed a flourish (sub-opcode 0x166, Core3 Flourish). The envelope's ObjectId is
 * the performer. Core3 broadcasts it for /flourish 1-8, and with -1 when the performer asks to stop
 * (the outro phase; the PerformanceType delta to 0 follows once it has played).
 *
 * Payload: flourishId(int32)
 */
struct SWGEMU_API FFlourishIn
{
	/** 1-8 selects performance.iff flourish1-8; -1 means the performer is stopping: play the outro. */
	int32 FlourishId = 0;

	bool Parse(FSWGPacket& Packet);

	/** True for 1-8. */
	bool IsFlourish() const { return FlourishId >= 1 && FlourishId <= 8; }
};
