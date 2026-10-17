#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** One node of a slot rule expression tree (SlotOccupiedRule / AndRule / OrRule / NegateRule in the retail client). */
struct FSWGSlotRuleNode
{
	enum class EKind : uint8
	{
		SlotOccupied,
		And,
		Or,
		Negate,
	};

	EKind Kind = EKind::SlotOccupied;
	FString SlotName;
	/** Indices into FSWGSlotRuleManager::Nodes. */
	TArray<int32> Children;
};

/** A customization variable driven by a rule while an item sits in a slot. */
struct FSWGSlotRuleBinding
{
	FString SlotName;
	/** Full name, e.g. "/shared_owner/blend_jacket_belt". */
	FString VariableName;
	int32 RuleNode = INDEX_NONE;
};

/**
 * customization/slot_rule_manager.iff (retail SlotRuleManager, SWGEmu.exe 0x598f30 install,
 * 0x598c00/0x598cc0 parse, 0x63f6c0 apply). Layout:
 *
 *   FORM SRMG > FORM 0000 > FORM BIND *
 *     BIND: INFO {slot\0 variable\0}, then one rule FORM:
 *       FORM SLOT > FORM 0000 > INFO {slot\0}        slot is occupied
 *       FORM ANDT / ORTS > FORM 0000 > rule FORMs    every / any child (no count, runs to the end)
 *       FORM NEGT > FORM 0000 > rule FORM            not the child
 *
 * When the owner's wearables change, the client walks the bindings and for every one whose slot is
 * occupied writes the rule's 0/1 result into the named customization variable, which drives the
 * worn item's blend shape (a jacket's belt hiding under a vest, a bandolier under a robe).
 */
class SWGTRE_API FSWGSlotRuleManager
{
public:
	TArray<FSWGSlotRuleNode> Nodes;
	TArray<FSWGSlotRuleBinding> Bindings;

	static bool Read(const FSWGIffReader& Reader, FSWGSlotRuleManager& OutResult);

	/** Whether the rule rooted at NodeIndex holds when exactly OccupiedSlots are filled (FString sets compare case-insensitively). */
	bool Evaluate(int32 NodeIndex, const TSet<FString>& OccupiedSlots) const;

	/**
	 * The bindings that apply to an item sitting in ItemSlots, as variable name -> 0/1. OccupiedSlots is
	 * everything the wearer has filled, ItemSlots included.
	 */
	void ResolveForItem(const TArray<FString>& ItemSlots, const TSet<FString>& OccupiedSlots, TMap<FString, int32>& OutVariables) const;

	/** Variable name with its "/private/" or "/shared_owner/" prefix removed. */
	static FString BareVariableName(const FString& VariableName);

private:
	int32 ReadRule(const FSWGIffReader& Reader, const FSWGIffChunk& RuleForm);
};
