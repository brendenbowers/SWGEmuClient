# Migration — Monolithic SOE port → UE `PacketHandler` pipeline

This document describes how to move the **current skeleton** (a 1:1 port of the C#
`SocketReader`/`SocketWriter`/`Session`/`CompressioEncryption` layout) to a
**UE-idiomatic architecture** built on Unreal's own networking abstractions.

It is a refactor plan, not a code dump. Each step says *what moves where* and
*why*, so you can apply it incrementally and keep the plugin compiling between
steps.

> Read first, in the engine source:
> - `Engine/Source/Runtime/PacketHandlers/PacketHandler/Public/PacketHandler.h` — the `HandlerComponent` base contract
> - `Engine/Source/Runtime/PacketHandlers/PacketHandler/Public/EncryptionComponent.h` — `FEncryptionComponent`
> - `Engine/Source/Runtime/PacketHandlers/ReliabilityHandlerComponent/` — a worked reliability component

---

## Why move at all?

The C# client puts CRC + XOR + zlib in one `CompressioEncryption` class, called
inline from `SocketReader.DoWork`, with all reliability/fragment/multi state in
one `Session`. That works, but it doesn't match how UE structures transport.

UE runs every datagram through an **ordered list of `HandlerComponent`s**
(`Incoming` on receive, `Outgoing` in reverse on send). The SOE receive path in
`SocketReader.cs` (`_HandleCompressedEncryptedPacket`) is already that shape:

```
CRC check → decrypt → decompress → reliability/session → dispatch
```

Splitting along those seams gives us testable stages, a natural home for the
handshake to flip encryption on, and accurate MTU budgeting via
`GetReservedPacketBits()`.

---

## Target file layout

```
Source/SWGEmu/
├── Public/Network/
│   ├── SWGPacket.h                 # (changed) becomes an FArchive subclass
│   ├── SWGSession.h                # (shrinks) handshake params + key only
│   ├── SWGCrypto.h                 # (kept)    pure algorithms, called by components
│   ├── SWGSocketReader.h           # (thinned) Recv() → Handler->Incoming()
│   └── Handler/                    # (NEW)     the pipeline
│       ├── SWGPacketHandler.h          # owns the ordered component list
│       ├── SWGCrcComponent.h
│       ├── SWGEncryptionComponent.h    # : FEncryptionComponent
│       ├── SWGCompressionComponent.h
│       ├── SWGReliabilityComponent.h
│       └── SWGHandshakeComponent.h
└── Private/Network/ (mirrors Public)
```

`SWGSocketWriter.h/.cpp` is **removed** — sending becomes tick-driven from the
subsystem (mirrors `UIpNetDriver::TickFlush`), not a second blocking thread.

---

## Mapping table

| Current skeleton | Target | Action |
|---|---|---|
| `SWGCrypto::GenerateCRC` | `FSWGCrcComponent` | wrap algorithm in a component |
| `SWGCrypto::Encrypt/Decrypt` | `FSWGEncryptionComponent : FEncryptionComponent` | wrap; key set via `SetEncryptionData` |
| `SWGCrypto::Compress/Decompress` | `FSWGCompressionComponent` | wrap |
| `FSWGSession` seq/ack/window/frag fields | `FSWGReliabilityComponent` | move state into the component |
| `FSWGSession` handshake (state, key, MaxPacketSize) | `FSWGHandshakeComponent` + slim `FSWGSession` | split |
| `FSWGSocketReader` (CRC+decrypt+decompress inline) | `FSWGSocketReader` (thin) + pipeline | gut the per-packet logic |
| `FSWGSocketWriter` (thread) | subsystem tick + `Handler->Outgoing()` | delete the class |
| `USWGNetworkSubsystem` owns `Session` + 2 threads | owns `FSWGPacketHandler` + 1 recv thread | re-wire ownership |

---

## Step-by-step

### Step 0 — Add the `HandlerComponent` build dependency
In `SWGEmu.Build.cs`, add `"PacketHandler"` to `PublicDependencyModuleNames`
(alongside `Sockets`, `Networking`). This is what gives you `HandlerComponent`,
`FEncryptionComponent`, `PacketHandler`, `FBitReader`/`FBitWriter`.

### Step 1 — Keep `SWGCrypto` as a pure algorithm library
No behavior change. The CRC table, XOR cipher, and zlib wrappers stay exactly as
they are. The components below *call into* `FSWGCrypto`; they don't reimplement
it. This keeps the diff small and the algorithms unit-testable on their own.

### Step 2 — Create `FSWGCrcComponent`
- `Incoming(FBitReader&)`: compute CRC over the datagram (seed = encryption key),
  compare to the trailing 2 bytes, drop on mismatch (return an empty/invalidated
  reader). Mirror `_HandleCompressedEncryptedPacket`'s CRC check.
- `Outgoing(FBitWriter&, FOutPacketTraits&)`: append the 2-byte CRC.
- `GetReservedPacketBits()`: return `16` (2 bytes).
- The key lives on the shared session; pass a `FSWGSession*` into the component
  at construction (UE components similarly hold a back-pointer to connection state).

### Step 3 — Create `FSWGEncryptionComponent : FEncryptionComponent`
Implement the pure-virtuals from `EncryptionComponent.h`:
- `SetEncryptionData(const FEncryptionData&)` — stash the 4-byte SOE key.
- `EnableEncryption()` / `DisableEncryption()` / `IsEncryptionEnabled()`.
- `Incoming`: `FSWGCrypto::Decrypt(...)` starting at the correct offset (2 or 4
  depending on op, see `SocketReader.cs`).
- `Outgoing`: `FSWGCrypto::Encrypt(...)`.
- `GetReservedPacketBits()`: `0` (XOR is in-place, no size change).
Encryption starts **disabled**; the handshake enables it once the key arrives.

### Step 4 — Create `FSWGCompressionComponent`
- `Incoming`: if the compression flag byte is set, `FSWGCrypto::Decompress`.
- `Outgoing`: `FSWGCrypto::Compress`; set/clear the flag byte.
- `GetReservedPacketBits()`: `8` (1 flag byte) — be conservative.

### Step 5 — Create `FSWGReliabilityComponent` (the big one)
Move these fields out of `FSWGSession` into this component:
`OutSeqNext`, `InSeqNext`, `LastSeqAcked`, `WindowPackets`, `WindowLock`,
`FragTotalSize`, `FragCurrentSize`, `FragBuffer`, `OutgoingReliable`,
`IncomingMessages`.
Model the bookkeeping on UE's `ReliabilityHandlerComponent`
(`LocalPacketID` / `LocalPacketIDACKED` / `RemotePacketID`, a buffered-resend
list, and a resend timer driven from `Tick()`):
- `Outgoing`: assign a `uint16` seq on DataChannel1, buffer the packet for resend.
- `Incoming`: on DataAck retire acked window packets; on DataChannel deliver
  in-order and emit a DataAck; on DataFrag/MultiPacket reassemble (port the
  `HandleDataFrag` / `ParseMultiPacket` sketches from the HTML guide) and push the
  finished payload to the dispatch queue.
- `Tick(DeltaTime)`: resend unacked window packets past the resend interval.

### Step 6 — Create `FSWGHandshakeComponent`
- `RequiresHandshake()` returns `true`.
- `NotifyHandshakeBegin()`: send `SessionRequest`.
- `Incoming`: on `SessionResponse`, parse the key + `MaxPacketSize`, call
  `EncryptionComponent->SetEncryptionData()` then `EnableEncryption()`, and fire
  the `FPacketHandlerHandshakeComplete` delegate so the subsystem knows it can
  start sending game traffic.

### Step 7 — Create `FSWGPacketHandler`
A thin owner of the ordered component list. Construction order **is** the receive
order:
```
[ Crc, Encryption, Compression, Reliability, Handshake ]
```
- `Incoming(FBitReader&)`: call each component's `Incoming` front-to-back.
- `Outgoing(FBitWriter&, FOutPacketTraits&)`: call back-to-front.
- Sum `GetReservedPacketBits()` across components to validate against the 496-byte
  SOE MTU before sending.
(You may instead use UE's stock `PacketHandler` and `AddHandler<>()`; a small
custom owner is fine for a fixed pipeline and easier to read.)

### Step 8 — Thin out `FSWGSocketReader`
Replace the per-packet CRC/decrypt/decompress logic with two lines: wrap the
received bytes in an `FBitReader` and call `Handler->Incoming(Reader)`. The reader
thread no longer knows anything about crypto or reliability.

### Step 9 — Delete `FSWGSocketWriter`; send from the subsystem tick
Implement `USWGNetworkSubsystem::Tick` (or a timer) to:
1. Drain outgoing game messages into an `FBitWriter`.
2. `Handler->Outgoing(Writer, Traits)`.
3. `Socket->Send(...)`.
Also call `Handler->Tick(DeltaTime)` so the reliability component can resend.

### Step 10 — Slim `FSWGSession`
After Steps 5–6, `FSWGSession` keeps only shared connection facts the components
read: `EncryptionKey`, `MaxPacketSize`, `WindowResendSize`, `State`, and the
ping/timeout timestamps. Everything sequence/fragment-related now lives in
`FSWGReliabilityComponent`.

### Step 11 — Make `FSWGPacket` an `FArchive`
So game messages (Phase 2) serialize with `<<` like UE's `NetSerialize`, derive
`FSWGPacket` from `FArchive` and implement `Serialize(void*, int64)` plus
`SetIsLoading`/`SetIsSaving`. Keep the existing big-endian read/write helpers as
convenience wrappers. This is independent of Steps 1–10 and can be done last.

---

## Suggested order & checkpoints

1. Steps 0–1 (deps + keep algorithms) → still compiles, no behavior change.
2. Steps 2–4 (Crc/Encryption/Compression components) → unit-test each in isolation.
3. Step 7 (handler owner) wiring those three → round-trip a raw packet.
4. Steps 5–6 (reliability + handshake) → can complete a real SessionResponse.
5. Steps 8–10 (thin reader, tick send, slim session) → end-to-end connect.
6. Step 11 (`FArchive`) → cleaner Phase 2 message serialization.

Each checkpoint leaves the plugin in a buildable state.

## References
- Architecture rationale & diagrams: `e:/Projects/swgemu-unreal-guide.html` (Architecture Map + Phase 1)
- C# reference implementation: `e:/Projects/swgemu.client/SWG.Client/Network/`
- UE transport source: `e:/Projects/UnrealEngine/Engine/Source/Runtime/PacketHandlers/`
