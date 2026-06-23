#pragma once

#include "CoreMinimal.h"

/**
 * ESWGMessageOp enumerates all known SWG message opcodes.
 *
 * These are 32-bit CRC values that identify message types on the wire.
 * Note: Some values are "fake" — computed by CRC on a string rather than a true opcode.
 */
enum class ESWGMessageOp : uint32
{
	// ── Login Server Messages ─────────────────────────────────
	LoginClientToken = 0xAAB296C6u,
	LoginEnumCluster = 0xC11C63B9u,
	LoginClusterStatus = 0x3436AEB6u,
	EnumerateCharacterId = 0x65EA4574u,
	SelectCharacter = 0xb5098d76u,

	LoginClientID = 0x41131F96u,

	// ── Zone Server: Scene & Objects ──────────────────────────
	CmdStartScene = 0x3AE6DFAEu,         // Server: load terrain
	CmdSceneReady = 0x43FD1C22u,         // Client: finished loading
	CmdSceneReady2 = 0x48f493c5u,        // Alternate SceneReady opcode

	SceneCreateObjectByCrc = 0xFE89DDEAu, // Create object via CRC
	SceneEndBaselines = 0x2C436037u,     // Object fully initialized
	SceneDestroyObject = 0x4D45D504u,    // Destroy object

	BaselinesMessage = 0x68A75F0Cu,      // Full object state snapshot
	DeltasMessage = 0x12862153u,         // Incremental field updates

	UpdateTransformMessage = 0x1B24F808u,           // Server-pushed position (packed shorts)
	UpdateTransformMessageWithParent = 0xC867AB5Au, // Position relative to cell/parent
	UpdateContainmentMessage = 0x56CBDE9Eu,        // Attach/detach from container
	UpdatePostureMessage = 0x0BDE6B41u,            // Stance (standing/sitting/etc)
	UpdatePvpStatusMessage = 0x08a1c126u,

	ObjControllerMessage = 0x80CE5E46u,  // Wrapper; inner op identifies sub-type

	// ── Client Lifecycle ───────────────────────────────────────
	ClientIdMsg = 0xd5899226u,
	ClientPermissionsMessage = 0xE00730E5u,
	ClientInactivity = 0x0F5D5325u,
	ClientLogout = 0x42FD19DDu,
	ConnectPlayerMessage = 0x2e365218u,
	ConnectPlayerResponseMessage = 0x6137556Fu,

	ErrorMessage = 0xb5abf91au,

	// ── Object Type Four-CCs ───────────────────────────────────
	// These are used as ObjectType in Baseline/Delta messages.
	// Format: FOURCC (e.g., 0x4352454F = "CREO")
	CREO = 0x4352454Fu,  // Creature (NPC/Player)
	PLAY = 0x504C4159u,  // Player
	TANO = 0x54414E4Fu,  // Tangible (item)
	STAO = 0x5354414Fu,  // Static (prop)
	BUIO = 0x4255494Fu,  // Building
	HINO = 0x48494E4Fu,  // House Installation
	INSO = 0x494E534Fu,  // Installation (factory/harvester)
	SCLT = 0x53434C54u,  // Scene Cell
	GRUP = 0x47525550u,  // Group
	GILD = 0x47494C44u,  // Guild
	MISO = 0x4D49534Fu,  // Mission
	ITNO = 0x4F4E5449u,  // Item
	FCYT = 0x46435954u,  // Factory/Crafting

	// ── Other Messages ────────────────────────────────────────
	ParametersMessage = 0x487652DAu,
	AttributeListMessage = 0xf3f12f2au,

	// Add more as needed during implementation
	Null = 0x0u,
};

/**
 * Get a human-readable name for a message opcode.
 * Returns "Unknown (0xXXXXXXXX)" if not recognized.
 */
SWGEMU_API FString GetMessageOpName(uint32 Opcode);
