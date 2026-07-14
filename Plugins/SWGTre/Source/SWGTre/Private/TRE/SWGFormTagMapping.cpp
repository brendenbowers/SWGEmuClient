#include "TRE/SWGFormTagMapping.h"

TSubclassOf<AActor> SWGFormTagDispatch::ResolveActorClass(const UDataTable* MappingTable, FName FormTag)
{
	if (!MappingTable)
		return nullptr;

	static const FString Context(TEXT("SWGFormTagDispatch::ResolveActorClass"));
	const FSWGFormTagMapping* Row = MappingTable->FindRow<FSWGFormTagMapping>(FormTag, Context, /*bWarnIfRowMissing*/ false);
	if (!Row)
		return nullptr;

	return Row->ActorClass;
}
