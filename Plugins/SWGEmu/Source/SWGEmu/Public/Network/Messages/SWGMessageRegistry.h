#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

struct FSWGMessage;

using FSWGMessageFactory = TFunction<TSharedPtr<FSWGNetMessage>(uint32,FSWGMessage&)>;

/**
 * FSWGMessageRegistry maps opcodes to factory functions.
 *
 * Populated at static-init time by TSWGMessageRegistrar<T>. The NetworkSubsystem
 * calls Create() to construct and deserialize a typed message from an incoming packet.
 */
class SWGEMU_API FSWGMessageRegistry
{
public:
	static FSWGMessageRegistry& Get();

	void Register(uint32 Opcode, FSWGMessageFactory&& Factory);

	/** Construct and deserialize the typed message for this opcode. Returns null if unregistered. */
	TSharedPtr<FSWGNetMessage> Create(uint32 Opcode, FSWGMessage& Reader) const;

	bool IsRegistered(uint32 Opcode) const;

private:
	TMap<uint32, FSWGMessageFactory> Factories;
};

/**
 * TSWGMessageRegistrar<T> — place a static instance of this in each message's .cpp
 * to self-register at startup. Use the REGISTER_SWG_MESSAGE macro.
 */
template<typename T>
struct TSWGMessageRegistrar
{
	TSWGMessageRegistrar(uint32 Opcode)
	{
		FSWGMessageRegistry::Get().Register(Opcode, [](uint32 Op, FSWGMessage& Reader) -> TSharedPtr<FSWGNetMessage>
		{
			return MakeShared<T>(Op, Reader);
		});
	}
};

/**
 * Place this in a message's .cpp file to self-register with the registry.
 *
 * Example:
 *   REGISTER_SWG_MESSAGE(FLoginClusterStatusMessage, ESWGMessageOp::LoginClusterStatus)
 */
#define REGISTER_SWG_MESSAGE(MessageType, OpcodeEnum) \
	static TSWGMessageRegistrar<MessageType> GRegistrar_##MessageType(static_cast<uint32>(OpcodeEnum));
