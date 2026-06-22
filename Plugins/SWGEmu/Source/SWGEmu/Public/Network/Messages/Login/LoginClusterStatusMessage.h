#pragma once

#include "CoreMinimal.h"
#include "../SWGNetMessage.h"
#include "../../Objects/ServerDetails.h"

struct FSWGMessage;

/**
 * LoginClusterStatusMessage (opcode 0x3436AEB6)
 *
 * Sent by the login server listing available game server clusters.
 *
 * Wire layout (at payload cursor):
 *   count(4) [ serverID(4) ip(str) port(2) pingPort(2) pop(4) maxCap(4)
 *              maxChars(4) distance(4) status(4) notRecommended(1) ] x count
 */
struct FLoginClusterStatusMessage : public FSWGNetMessage
{
	TArray<FServerDetails> Servers;

	virtual bool Deserialize(FSWGMessage& Reader) override;

	const FServerDetails* FindServer(uint32 ServerID) const;
};
