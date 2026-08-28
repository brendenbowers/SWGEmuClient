#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/ObjectControllerMessage.h"

/**
 * Queues a command on the server (sub-opcode 0x116) — the message behind every
 * ability use, toolbar press and slash command.
 *
 * ActionCRC identifies the command: Core3 registers each one under the hash of
 * its lowercased name (CommandConfigManager), which FSWGCrc32::HashString
 * computes. ActionCount is the client's own queue id, echoed back on the
 * server's CommandQueueRemove so a reply can be matched to its request.
 *
 * Payload (CommandQueueEnqueueCallback::parse):
 *   size(int32, unused server-side) actionCount(int32) actionCRC(int32)
 *   targetId(int64) arguments(unicode)
 */
struct SWGEMU_API FCommandQueueEnqueue : public FObjectControllerMessage
{
public:
	uint32  ActionCount = 0;
	uint32  ActionCRC   = 0;
	uint64  TargetId    = 0;
	FString Arguments;

	FCommandQueueEnqueue(uint64 ObjectId, uint32 ActionCRC, uint32 ActionCount = 0, uint64 TargetId = 0, const FString& Arguments = FString());
	~FCommandQueueEnqueue() = default;

	FSWGPacket Serialize() const;
};
