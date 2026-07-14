#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "SWGTangibleComponent.generated.h"

struct FSWGPacket;
class UTextRenderComponent;

/**
 * TANO base3 identity/appearance fields — shared by every tangible object
 * (ASWGItem) and every creature (ASWGCreature, since CREO extends TANO on
 * the wire). Durability (ConditionDamage/MaxCondition/UseCount) and combat
 * targeting (DefenderList) are split into USWGConditionComponent and
 * USWGDefenderComponent — see world-object-plan.html "Component breakdown".
 */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGTangibleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGTangibleComponent();

	float          Complexity = 0.f;
	FSWGStringId   ObjectName;
	FString        CustomName;
	int32          Volume = 0;
	FString        CustomizationString;
	TSWGBaselineList<int32> VisibleComponents;
	int32          OptionsBitmask = 0;
	uint8          ObjectVisible = 0;
	bool           bHasBase3 = false;

	// Split in two: TANO base3 interleaves with USWGConditionComponent mid-streamok do
	// (Complexity..OptionsBitmask, then Condition's UseCount/ConditionDamage/
	// MaxCondition, then ObjectVisible last) — see SWGTangibleBaselineParser::ParseBase3.
	void ApplyBase3Part1(FSWGPacket& Packet); // Complexity..OptionsBitmask
	void ApplyBase3Part2(FSWGPacket& Packet); // ObjectVisible

	// Re-derives NameLabel's height above the root from the owner's *current*
	// capsule size — called by USWGMeshGeneratorSubsystem once the owner's
	// real mesh has resized its capsule, since UpdateNameLabel may have
	// already run (and positioned the label) against the default capsule
	// size beforehand. No-op if NameLabel doesn't exist yet.
	void RepositionNameLabel();

private:
	// Dev-visibility name tag floating above the object — CustomName if the
	// player set one (e.g. a crafted/renamed item), otherwise the raw
	// File/StringTableId from ObjectName as a stand-in until STF string-table
	// lookup exists to resolve it to real display text.
	void UpdateNameLabel();

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> NameLabel;
};
