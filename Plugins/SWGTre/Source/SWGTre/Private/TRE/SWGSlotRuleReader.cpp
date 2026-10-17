#include "TRE/SWGSlotRuleReader.h"

#include "TRE/SWGIFFChunkReader.h"
#include "TRE/SWGIffTags.h"

namespace
{
	constexpr FSWGIffTag SrmgTag = SWG_IFF_TAG('S', 'R', 'M', 'G');
	constexpr FSWGIffTag BindTag = SWG_IFF_TAG('B', 'I', 'N', 'D');
	constexpr FSWGIffTag SlotTag = SWG_IFF_TAG('S', 'L', 'O', 'T');
	constexpr FSWGIffTag AndTag = SWG_IFF_TAG('A', 'N', 'D', 'T');
	constexpr FSWGIffTag OrTag = SWG_IFF_TAG('O', 'R', 'T', 'S');
	constexpr FSWGIffTag NegateTag = SWG_IFF_TAG('N', 'E', 'G', 'T');
	constexpr FSWGIffTag Version0000 = SWG_IFF_TAG('0', '0', '0', '0');
}

int32 FSWGSlotRuleManager::ReadRule(const FSWGIffReader& Reader, const FSWGIffChunk& RuleForm)
{
	if (!RuleForm.IsForm())
	{
		return INDEX_NONE;
	}
	FSWGIffChunk Body;
	if (!Reader.FindChildForm(RuleForm, Version0000, Body))
	{
		return INDEX_NONE;
	}

	FSWGSlotRuleNode Node;
	if (RuleForm.FormType == SlotTag)
	{
		FSWGIffChunk Info;
		if (!Reader.FindChildChunk(Body, SWGIffTags::Info, Info))
		{
			return INDEX_NONE;
		}
		FSWGIFFChunkReader InfoReader(Info, Reader);
		Node.Kind = FSWGSlotRuleNode::EKind::SlotOccupied;
		if (!InfoReader.ReadTerminiatedString(Node.SlotName))
		{
			return INDEX_NONE;
		}
		return Nodes.Add(MoveTemp(Node));
	}

	if (RuleForm.FormType == AndTag || RuleForm.FormType == OrTag || RuleForm.FormType == NegateTag)
	{
		Node.Kind = RuleForm.FormType == AndTag ? FSWGSlotRuleNode::EKind::And
			: RuleForm.FormType == OrTag ? FSWGSlotRuleNode::EKind::Or
			: FSWGSlotRuleNode::EKind::Negate;
		for (const FSWGIffChunk& Child : Reader.FindChildForms(Body))
		{
			const int32 ChildIndex = ReadRule(Reader, Child);
			if (ChildIndex == INDEX_NONE)
			{
				return INDEX_NONE;
			}
			Node.Children.Add(ChildIndex);
		}
		if (Node.Kind == FSWGSlotRuleNode::EKind::Negate && Node.Children.Num() != 1)
		{
			return INDEX_NONE;
		}
		return Nodes.Add(MoveTemp(Node));
	}

	return INDEX_NONE;
}

bool FSWGSlotRuleManager::Read(const FSWGIffReader& Reader, FSWGSlotRuleManager& OutResult)
{
	OutResult.Nodes.Reset();
	OutResult.Bindings.Reset();
	if (!Reader.IsValid())
	{
		return false;
	}

	FSWGIffChunk Root, Body;
	if (!Reader.FindForm(SrmgTag, Root) || !Reader.FindChildForm(Root, Version0000, Body))
	{
		return false;
	}

	for (const FSWGIffChunk& Bind : Reader.FindChildForms(Body))
	{
		if (Bind.FormType != BindTag)
		{
			continue;
		}
		FSWGIffChunk Info;
		if (!Reader.FindChildChunk(Bind, SWGIffTags::Info, Info))
		{
			return false;
		}
		FSWGSlotRuleBinding Binding;
		FSWGIFFChunkReader InfoReader(Info, Reader);
		if (!InfoReader.ReadTerminiatedString(Binding.SlotName) || !InfoReader.ReadTerminiatedString(Binding.VariableName))
		{
			return false;
		}

		// The rule is the one FORM child that is not the INFO leaf.
		const TArray<FSWGIffChunk> RuleForms = Reader.FindChildForms(Bind);
		if (RuleForms.Num() != 1)
		{
			return false;
		}
		Binding.RuleNode = OutResult.ReadRule(Reader, RuleForms[0]);
		if (Binding.RuleNode == INDEX_NONE)
		{
			return false;
		}
		OutResult.Bindings.Add(MoveTemp(Binding));
	}
	return OutResult.Bindings.Num() > 0;
}

bool FSWGSlotRuleManager::Evaluate(int32 NodeIndex, const TSet<FString>& OccupiedSlots) const
{
	if (!Nodes.IsValidIndex(NodeIndex))
	{
		return false;
	}
	const FSWGSlotRuleNode& Node = Nodes[NodeIndex];
	switch (Node.Kind)
	{
		case FSWGSlotRuleNode::EKind::SlotOccupied:
			return OccupiedSlots.Contains(Node.SlotName); // FString hashing and equality ignore case
		case FSWGSlotRuleNode::EKind::And:
			for (int32 Child : Node.Children)
			{
				if (!Evaluate(Child, OccupiedSlots))
				{
					return false;
				}
			}
			return true;
		case FSWGSlotRuleNode::EKind::Or:
			for (int32 Child : Node.Children)
			{
				if (Evaluate(Child, OccupiedSlots))
				{
					return true;
				}
			}
			return false;
		case FSWGSlotRuleNode::EKind::Negate:
			return !Evaluate(Node.Children[0], OccupiedSlots);
	}
	return false;
}

void FSWGSlotRuleManager::ResolveForItem(const TArray<FString>& ItemSlots, const TSet<FString>& OccupiedSlots, TMap<FString, int32>& OutVariables) const
{
	for (const FSWGSlotRuleBinding& Binding : Bindings)
	{
		const bool bItemInSlot = ItemSlots.ContainsByPredicate([&Binding](const FString& Slot) { return Slot.Equals(Binding.SlotName, ESearchCase::IgnoreCase); });
		if (bItemInSlot)
		{
			OutVariables.Add(Binding.VariableName, Evaluate(Binding.RuleNode, OccupiedSlots) ? 1 : 0);
		}
	}
}

FString FSWGSlotRuleManager::BareVariableName(const FString& VariableName)
{
	int32 SlashIndex = INDEX_NONE;
	return VariableName.FindLastChar(TEXT('/'), SlashIndex) ? VariableName.Mid(SlashIndex + 1) : VariableName;
}
