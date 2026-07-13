#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SWGFormTagMapping.generated.h"

/**
 * One row of the FormTag -> actor class DataTable. The DataTable's Row Name
 * itself IS the FORM tag (e.g. "SCOT") — look up with
 * DataTable->FindRow<FSWGFormTagMapping>(FName("SCOT"), ...).
 *
 * ActorClass is left unset (None) for FORM tags that don't spawn a visual
 * actor at all (SITN/SDSC/SPLY/SHOT) or that we haven't built yet (SSHP) —
 * both cases should be treated as "don't spawn," not an error, by the
 * dispatch code. See world-object-plan.html "Template FORM types".
 */
USTRUCT(BlueprintType)
struct SWGTRE_API FSWGFormTagMapping : public FTableRowBase
{
	GENERATED_BODY()

	/** The inferred SOE template class name, e.g. "SharedCreatureObjectTemplate" — for reference only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString TemplateClassName;

	/** The actor class to spawn for this FORM tag. Leave unset for data-only / no-visual-presence types. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TSubclassOf<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString Notes;
};

namespace SWGFormTagDispatch
{
	/**
	 * Looks up FormTag (e.g. "SCOT") in MappingTable and returns its ActorClass,
	 * or nullptr if the tag isn't found OR its row has no actor class set
	 * (SITN/SDSC/SPLY/SHOT/SSHP by design — both cases mean "don't spawn").
	 */
	SWGTRE_API TSubclassOf<AActor> ResolveActorClass(const UDataTable* MappingTable, FName FormTag);
}
