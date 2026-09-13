#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/TangibleObjectBaseline.h"
#include "Network/Objects/Zone/Object/TangibleObjectDelta.h"
#include "Customization/SWGCustomizationVariables.h"
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

	// Raw wire payload for the "customization" baseline field (skin/hair/eye
	// color indices, etc.) — read via ReadAsciiBytes, not ReadAsciiString,
	// since it's binary data dressed as a string (see
	// FSWGCustomizationVariables for why that distinction matters).
	// DecodedCustomization is the parsed (TypeId -> Value) form; see its own
	// comment for what's still missing (a TypeId -> name/effect mapping).
	TArray<uint8>  CustomizationBytes;
	FSWGCustomizationVariables DecodedCustomization;

	// Baked-in body customization from the owner's template .cdf (CSSI —
	// NPC skin color, face blends); the server sends none for NPCs. Set by
	// USWGObjectGraphSubsystem at spawn, before the mesh builds.
	FSWGCustomizationVariables ClientDataCustomization;

	/** ClientDataCustomization with DecodedCustomization layered on top — the server's value wins per variable. */
	FSWGCustomizationVariables GetEffectiveCustomization() const;

	TSWGBaselineList<int32> VisibleComponents;
	int32          OptionsBitmask = 0;
	uint8          ObjectVisible = 0;
	bool           bHasBase3 = false;

	/**
	 * Human-readable name: CustomName if the player set one (renamed pet,
	 * crafted item), else ObjectName resolved through the .stf string tables,
	 * else the owner template's objectName (most NPCs/props leave the wire
	 * ObjectName empty). Falls back to the raw string key when no table resolves.
	 */
	FString GetDisplayName() const;

	void ApplyBase3(const FTangibleObjectBaseline& Baseline);
	void ApplyDelta3(const FTangibleObjectDelta& Delta);

	// Re-derives NameLabel's height above the root from the owner's *current*
	// capsule size — called by USWGMeshGeneratorSubsystem once the owner's
	// real mesh has resized its capsule, since UpdateNameLabel may have
	// already run (and positioned the label) against the default capsule
	// size beforehand. No-op if NameLabel doesn't exist yet.
	void RepositionNameLabel();

private:
	// Dev-visibility name tag floating above the object, showing GetDisplayName().
	void UpdateNameLabel();

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> NameLabel;
};
