#pragma once

#include "CoreMinimal.h"

struct FSWGPacket;

/**
 * A creature's posture changed and the change should animate now
 * (sub-opcode 0x131, Core3 PostureMessage).
 *
 * The server pairs this with a CREO delta3 carrying the same posture, and the
 * two mean different things: the delta keeps the client's copy of the
 * variable right, this one asks for the transition animation. Core3 sends it
 * for voluntary changes (/kneel, /prone) and, via
 * CreatureObject::updatePostures(true), for incapacitation and death — it is
 * the closest thing to a "this creature just died" message. It is deliberately
 * withheld for postures a CombatAction forces (a knockdown), where the
 * reaction clip in the CombatAction is the transition.
 *
 * Payload (Core3 PostureMessage):
 *   posture(byte) immediate(byte, always 1)
 */
struct SWGEMU_API FPostureUpdateIn
{
	/** An ESWGPosture value. */
	uint8 Posture = 0;

	/** Core3 always writes 1 here; kept so a future value shows up in logs rather than being silently dropped. */
	uint8 Immediate = 0;

	bool Parse(FSWGPacket& Packet);
};
