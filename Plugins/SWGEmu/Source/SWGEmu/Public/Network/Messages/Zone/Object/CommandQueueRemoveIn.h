#pragma once

#include "CoreMinimal.h"

struct FSWGPacket;

/**
 * Why the server took a queued command off our queue (sub-opcode 0x117).
 *
 * Core3 spells this as two ints rather than one error enum: Error is the
 * category and ErrorDetail qualifies the two categories that need it. The
 * values come from QueueCommand::onFail / onStateFail / onLocomotionFail,
 * which are the only callers of clearQueueAction that pass anything non-zero.
 */
enum class ESWGCommandError : uint32
{
	/** Command ran. Timer is its cooldown, and nothing failed. */
	None = 0,

	/** ErrorDetail is the ESWGLocomotion we were in — see QueueCommand::onLocomotionFail. */
	InvalidLocomotion = 1,

	/** The player lacks the ability. ObjectController rejects it before the command runs. */
	MissingAbility = 2,

	InvalidTarget = 3,

	TooFar = 4,

	/** ErrorDetail is the ESWGState bit index that blocked it — see QueueCommand::onStateFail. */
	InvalidState = 5,
};

/**
 * The server's reply to a CommandQueueEnqueue (sub-opcode 0x117).
 *
 * The client's completion signal, sent on both outcomes — there is no separate
 * success message. Failures that carry a player-visible explanation send it as
 * system chat and leave Error at zero, so zero Error with a zero Timer means
 * "did not run, look at chat" rather than success.
 *
 * Payload (Core3 CommandQueueRemove):
 *   actionCount(int32) timer(float, seconds) error(int32) errorDetail(int32)
 */
struct SWGEMU_API FCommandQueueRemoveIn
{
	uint32 ActionCount = 0;

	/** Cooldown before the next queued command runs, in seconds. */
	float Timer = 0.f;

	uint32 Error = 0;
	uint32 ErrorDetail = 0;

	bool Parse(FSWGPacket& Packet);

	ESWGCommandError GetError() const { return static_cast<ESWGCommandError>(Error); }
	bool IsSuccess() const { return Error == 0; }

	/** A short description of Error/ErrorDetail for logs. */
	FString DescribeError() const;
};
