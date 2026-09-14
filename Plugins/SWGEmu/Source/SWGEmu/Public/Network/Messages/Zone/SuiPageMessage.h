#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * Command types inside a SUI page (Core3 SuiCommand::SCT_*). A page is a
 * script of these run against the named retail UI template.
 */
enum class ESWGSuiCommandType : uint8
{
	None = 0,
	ClearDataSource = 1,
	AddChildWidget = 2,
	SetProperty = 3,           // wide[0] = value; narrow = widget, property
	AddDataItem = 4,           // wide[0] = value; narrow = data source, property ("Name")
	SubscribeToEvent = 5,      // narrow = source widget, 1-byte event type, callback, then (widget, property) pairs to return
	AddDataSourceContainer = 6,
	ClearDataSourceContainer = 7,
	AddDataSource = 8,
};

struct SWGEMU_API FSWGSuiCommand
{
	uint8 Type = 0;
	TArray<FString> WideParams;   // unicode
	TArray<FString> NarrowParams; // ascii

	ESWGSuiCommandType GetType() const { return static_cast<ESWGSuiCommandType>(Type); }
};

/**
 * A server-scripted window (SUI): SuiCreatePage 0xD44B7259 opens one,
 * SuiUpdatePage 0x5F3342F6 replaces an open one's contents. PageId is the
 * handle every later message uses; ScriptClass names the retail UI template
 * ("Script.messageBox", "Script.listBox", "Script.inputBox", ...).
 *
 * Wire layout (Core3 SuiPageData::toBinaryStream):
 *   pageId(int32) scriptClass(ascii) count(int32)
 *   { type(u8) wideCount(int32) unicode* narrowCount(int32) ascii* }*
 *   usingObjectId(int64) forceCloseDistance(float) unknownId(int64)
 *
 * The legacy SuiBox path writes the same shape; its footer variant with three
 * 0x7F7FFFFF ints is tolerated by treating the tail as optional.
 */
struct SWGEMU_API FSuiCreatePageMessage : public FSWGNetMessage
{
	uint32 PageId = 0;
	FString ScriptClass;
	TArray<FSWGSuiCommand> Commands;

	/** The object the window is about; the client closes it when farther than ForceCloseDistance. */
	uint64 UsingObjectId = 0;
	float ForceCloseDistance = 0.f;

	FSuiCreatePageMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};

/** Same payload as FSuiCreatePageMessage, for a page that is already open. */
struct SWGEMU_API FSuiUpdatePageMessage : public FSuiCreatePageMessage
{
	FSuiUpdatePageMessage(uint32 OPCode, FSWGMessage& Reader) : FSuiCreatePageMessage(OPCode, Reader) {}
};

/**
 * The server closing a window it opened (opcode 0x990B5DE0), e.g. when the
 * player walked out of range or the underlying object went away.
 *
 * Wire layout: pageId(int32)
 */
struct SWGEMU_API FSuiForceClosePageMessage : public FSWGNetMessage
{
	uint32 PageId = 0;

	FSuiForceClosePageMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
