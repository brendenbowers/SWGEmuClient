#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessage.h"

FSWGMessageRegistry& FSWGMessageRegistry::Get()
{
	// Function-local static avoids static initialization order issues.
	static FSWGMessageRegistry Instance;
	return Instance;
}

void FSWGMessageRegistry::Register(uint32 Opcode, FSWGMessageFactory&& Factory)
{
	if (Factories.Contains(Opcode))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMessageRegistry: opcode 0x%08X already registered — overwriting"), Opcode);
	}
	Factories.Add(Opcode, MoveTemp(Factory));
}

TUniquePtr<FSWGNetMessage> FSWGMessageRegistry::Create(uint32 Opcode, FSWGMessage& Reader) const
{
	const FSWGMessageFactory* Factory = Factories.Find(Opcode);
	if (!Factory)
		return nullptr;

	TUniquePtr<FSWGNetMessage> Msg = (*Factory)();
	Msg->Opcode = Opcode;
	Msg->Deserialize(Reader);
	return Msg;
}

bool FSWGMessageRegistry::IsRegistered(uint32 Opcode) const
{
	return Factories.Contains(Opcode);
}
