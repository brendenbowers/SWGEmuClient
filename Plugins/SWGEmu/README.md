# SWG Emu Network Client Plugin

A complete Unreal Engine 5 plugin implementing the SOE (Sony Online Entertainment) UDP protocol for Star Wars Galaxies EMU.

## What's Included

This plugin is structured in 5 phases:

### Phase 1: SOE Protocol Layer — realigning to UE's `PacketHandler` model

The SOE transport is implemented as an **ordered `PacketHandler` pipeline of `HandlerComponent`s**, mirroring Unreal's own networking architecture (`Engine/Source/Runtime/PacketHandlers/`). On receive the pipeline runs front-to-back; on send, back-to-front — implementing the SOE protocol's CRC→decrypt→decompress→reliability stack.

- `FSWGPacket` — Byte buffer with read/write helpers (big-endian aware); target: derive from `FArchive` so messages serialize with `<<`
- `FSWGCrcComponent : HandlerComponent` — CRC32 verify (in) / append (out)
- `FSWGEncryptionComponent : FEncryptionComponent` — XOR stream cipher; `SetEncryptionData()` + `EnableEncryption()` driven by the handshake
- `FSWGCompressionComponent : HandlerComponent` — zlib inflate (in) / deflate (out)
- `FSWGReliabilityComponent : HandlerComponent` — sequence/ACK window, fragment & multi-packet reassembly (model on UE's `ReliabilityHandlerComponent`)
- `FSWGHandshakeComponent : HandlerComponent` — SessionRequest/Response; `RequiresHandshake() == true`, completes via `FPacketHandlerHandshakeComplete`
- `FSWGSocketReader` — Thin receive thread (`FRunnable`): `Recv()` → push bytes into `Handler->Incoming()`. Sending is tick-driven (`Handler->Outgoing()` → `Socket->Send()`), matching `UIpNetDriver::TickFlush`.


### Phase 2: Message Dispatch Layer ✓ Skeleton Complete
- `FSWGMessage` — Game message wrapper (opcode + payload)
- `ESWGMessageOp` — Enum of all ~100 SWG message opcodes
- Handler registration system in `USWGNetworkSubsystem`

### Phase 3: Login Flow (TODO)
- Login server connection sequence
- Character enumeration and selection
- Zone server handoff with ticket
- UMG widget blueprints for UI

### Phase 4: Object System (TODO)
- `USWGObjectManager` — Tracks game objects by SWG ID
- `ASWGObject` — Base actor for SWG entities
- Baseline/Delta parsing for creature, player, tangible objects
- Coordinate system conversion (SWG → Unreal)

### Phase 5: Movement (TODO)
- `USWGMovement` — Custom movement component for client-side movement
- Server correction handling
- Remote player interpolation

## Project Structure

```
Plugins/SWGEmu/
├── Source/SWGEmu/
│   ├── Public/
│   │   ├── SWGEmu.h
│   │   ├── Network/
│   │   │   ├── SWGPacket.h
│   │   │   ├── SWGSession.h            # Holds handshake params, sequence numbers, and relay queues
│   │   │   ├── SWGCrypto.h             # algorithms reused by the Components below
│   │   │   ├── SWGSocketReader.h
│   │   │   ├── Handler/                # NEW — the PacketHandler pipeline
│   │   │   │   ├── SWGPacketHandler.h
│   │   │   │   ├── SWGCrcComponent.h
│   │   │   │   ├── SWGEncryptionComponent.h
│   │   │   │   ├── SWGCompressionComponent.h
│   │   │   │   ├── SWGReliabilityComponent.h
│   │   │   │   └── SWGHandshakeComponent.h
│   │   │   └── Messages/
│   │   │       ├── SWGMessage.h
│   │   │       └── SWGMessageOp.h
│   │   ├── Objects/
│   │   ├── Movement/
│   │   └── Subsystems/
│   │       └── SWGNetworkSubsystem.h
│   ├── Private/
│   │   ├── SWGEmu.cpp
│   │   ├── Network/ (implementation files)
│   │   ├── Subsystems/ (implementation files)
│   │   └── ...
│   └── SWGEmu.Build.cs
├── Resources/
├── Binaries/
├── SWGEmu.uplugin
└── README.md (this file)
```

## Setup

### 1. Create a Game Project (or use an existing one)

```bash
# Via Epic Games Launcher: 
# New Project → C++ → Blank → Select UE 5.3+
```

### 2. Add the Plugin to Your Project

```bash
# Copy the Plugins/SWGEmu folder to your game project:
YourGame/Plugins/SWGEmu/
```

### 3. Add Plugin to .uproject

Edit `YourGame.uproject`:

```json
{
	"Plugins": [
		{
			"Name": "SWGEmu",
			"Enabled": true
		}
	]
}
```

### 4. Regenerate Visual Studio Project & Rebuild

```bash
# Close UE Editor
# Right-click YourGame.uproject → Generate Visual Studio project files
# Open YourGame.sln in Visual Studio
# Build → Rebuild Solution
# Open Editor again
```

## Usage

### Connect to a Server

```cpp
void AMyPlayerController::BeginPlay()
{
	Super::BeginPlay();

	USWGNetworkSubsystem* Net = GetGameInstance()->GetSubsystem<USWGNetworkSubsystem>();
	FString Error;
	if (Net->Connect(TEXT("loginserver.swgemu.com"), 44453, Error))
	{
		UE_LOG(LogTemp, Log, TEXT("Connected!"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Connection failed: %s"), *Error);
	}
}
```

### Listen for Messages

```cpp
void AMyGameMode::BeginPlay()
{
	Super::BeginPlay();

	USWGNetworkSubsystem* Net = GetGameInstance()->GetSubsystem<USWGNetworkSubsystem>();
	
	// Register a handler for a specific opcode
	Net->RegisterMessageHandler((uint32)ESWGMessageOp::BaselinesMessage,
		[this](const FSWGMessage& Msg)
		{
			// Handle baseline message
			int64 ObjectId = Msg.ReadInt64();
			// ... parse rest of message
		});

	// Or listen to all messages
	Net->OnMessageReceived.AddDynamic(this, &AMyGameMode::OnMessageReceived);
}

void AMyGameMode::OnMessageReceived(const FSWGMessage& Msg)
{
	FString OpName = GetMessageOpName(Msg.Opcode);
	UE_LOG(LogTemp, Log, TEXT("Received: %s"), *OpName);
}
```

### Send a Message

```cpp
void AMyCharacter::SendLoginRequest(const FString& Username, const FString& Password)
{
	USWGNetworkSubsystem* Net = GetGameInstance()->GetSubsystem<USWGNetworkSubsystem>();

	FSWGPacket Pkt;
	Pkt.WriteInt16(0x0009);  // DataChannel1
	Pkt.WriteInt32(0x41131f96);  // LoginClientId opcode
	// ... write username/password fields
	Pkt.ShrinkToWriteIndex();

	Net->SendMessage(Pkt, true);  // bReliable=true
}
```

## Implementation Checklist

- [ ] Phase 1: Socket handshake and basic packet send/recv
- [ ] Phase 1: Encryption/decryption working end-to-end
- [ ] Phase 1: Fragment reassembly
- [ ] Phase 1: Reliability (sequence/ACK window)
- [ ] Phase 2: Message dispatch and opcode routing
- [ ] Phase 3: Login server flow (username/password)
- [ ] Phase 3: Character selection UI
- [ ] Phase 3: Zone server handoff
- [ ] Phase 4: Object creation and baseline parsing
- [ ] Phase 4: Delta updates
- [ ] Phase 4: Unreal actor spawning
- [ ] Phase 5: Client-side movement
- [ ] Phase 5: Server corrections

## Debugging

### Logging

The plugin logs to `LogTemp` by default. Increase verbosity in the Editor:

```
Ctrl+Backtick (in Editor)
log LogTemp verbose
```

### Packet Inspection

Each `FSWGPacket` can be printed:

```cpp
FSWGPacket Pkt;
// ...
UE_LOG(LogTemp, Log, TEXT("%s"), *Pkt.ToString());
// Output: FSWGPacket(Size=256, ReadIdx=0, WriteIdx=42, Encrypted=1, Compressed=0)
```

### Message Logging

The built-in `GetMessageOpName()` helper converts opcodes to readable names:

```cpp
UE_LOG(LogTemp, Log, TEXT("Got message: %s (0x%08X)"), 
	*GetMessageOpName(Msg.Opcode), Msg.Opcode);
```

## References

- **SWGEmu Server**: https://github.com/swgemu
- **SOE Protocol**: Implements the Sony Online Entertainment UDP protocol as used by Star Wars Galaxies

## Building for Shipping

The plugin works with packaged games. No special configuration needed — just ensure the Sockets and Networking modules are available on your target platform (they are by default).

## License

This plugin is provided as-is for educational purposes related to SWG EMU.
