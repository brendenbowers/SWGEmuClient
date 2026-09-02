#include "TRE/SWGAshReader.h"

#include "TRE/SWGIffReader.h"
#include "TRE/SWGIFFChunkReader.h"

namespace
{
	constexpr FSWGIffTag TagAsht = SWG_IFF_TAG('A', 'S', 'H', 'T');
	constexpr FSWGIffTag TagStat = SWG_IFF_TAG('S', 'T', 'A', 'T');
	constexpr FSWGIffTag TagChld = SWG_IFF_TAG('C', 'H', 'L', 'D');
	constexpr FSWGIffTag TagLnks = SWG_IFF_TAG('L', 'N', 'K', 'S');
	constexpr FSWGIffTag TagLink = SWG_IFF_TAG('L', 'I', 'N', 'K');
	constexpr FSWGIffTag TagInfo = SWG_IFF_TAG('I', 'N', 'F', 'O');

	/** Adds StatForm and everything beneath it to OutHierarchy.States, returning the new node's index (INDEX_NONE if the STAT has no readable INFO). */
	int32 ReadState(const FSWGIffReader& Reader, const FSWGIffChunk& StatForm, int32 ParentIndex, FSWGAnimationStateHierarchy& OutHierarchy)
	{
		FSWGIffChunk InfoChunk;
		if (!Reader.FindChildChunk(StatForm, TagInfo, InfoChunk))
		{
			return INDEX_NONE;
		}

		FSWGAnimationState State;
		FSWGIFFChunkReader InfoReader(InfoChunk, Reader);
		if (!InfoReader.ReadTerminiatedString(State.Name) || State.Name.IsEmpty())
		{
			return INDEX_NONE;
		}
		// A state with no loop animation of its own is legal (it inherits the
		// parent's), so an empty second string isn't a parse failure.
		InfoReader.ReadTerminiatedString(State.LoopAnimationName);
		State.ParentIndex = ParentIndex;

		// LNKS: an INFO holding the count, then one LINK chunk per transition.
		// Each LINK is [pathDepth:uint16][pathDepth null-terminated names]
		// [hasTransition:uint8][transition animation name]. The path names the
		// destination state from the root down, so "root/combat/kneeling"
		// arrives as three separate strings.
		FSWGIffChunk LinksForm;
		if (Reader.FindChildForm(StatForm, TagLnks, LinksForm))
		{
			for (const FSWGIffChunk& Link : Reader.FindAllChildChunks(LinksForm, TagLink))
			{
				FSWGIFFChunkReader LinkReader(Link, Reader);

				uint16 PathDepth = 0;
				if (!LinkReader.ReadValueLE(PathDepth) || PathDepth == 0)
				{
					continue;
				}

				FSWGAnimationStateLink Parsed;
				Parsed.DestinationPath.Reserve(PathDepth);
				bool bPathComplete = true;
				for (uint16 Component = 0; Component < PathDepth; ++Component)
				{
					FString Name;
					if (!LinkReader.ReadTerminiatedString(Name))
					{
						bPathComplete = false;
						break;
					}
					Parsed.DestinationPath.Add(MoveTemp(Name));
				}

				if (!bPathComplete)
				{
					continue;
				}

				// The flag byte distinguishes "there is a clip" from a link
				// that only authorises the change; the name is empty either
				// way when there's nothing to play.
				uint8 bHasTransition = 0;
				LinkReader.ReadValueLE(bHasTransition);
				LinkReader.ReadTerminiatedString(Parsed.TransitionAnimationName);

				State.Links.Add(MoveTemp(Parsed));
			}
		}

		const int32 Index = OutHierarchy.States.Add(MoveTemp(State));

		FSWGIffChunk ChildrenForm;
		if (Reader.FindChildForm(StatForm, TagChld, ChildrenForm))
		{
			for (const FSWGIffChunk& Child : Reader.FindChildForms(ChildrenForm))
			{
				if (Child.FormType != TagStat)
				{
					continue;
				}

				const int32 ChildIndex = ReadState(Reader, Child, Index, OutHierarchy);
				if (ChildIndex != INDEX_NONE)
				{
					// Re-fetched rather than held across the recursive call:
					// ReadState appends to the same array, which can reallocate.
					OutHierarchy.States[Index].ChildIndices.Add(ChildIndex);
				}
			}
		}

		return Index;
	}
}

bool FSWGAshReader::ReadHierarchy(const FSWGIffReader& Reader, FSWGAnimationStateHierarchy& OutHierarchy)
{
	OutHierarchy.States.Reset();
	OutHierarchy.ByName.Reset();

	FSWGIffChunk AshtForm;
	if (!Reader.IsValid() || !Reader.FindForm(TagAsht, AshtForm))
	{
		return false;
	}

	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(AshtForm);
	if (VersionForms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGAshReader: ASHT has no version FORM"));
		return false;
	}

	FSWGIffChunk RootStat;
	if (!Reader.FindChildForm(VersionForms[0], TagStat, RootStat))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGAshReader: ASHT version form has no root STAT"));
		return false;
	}

	if (ReadState(Reader, RootStat, INDEX_NONE, OutHierarchy) == INDEX_NONE)
	{
		return false;
	}

	// Breadth-first so ByName lands on the shallowest occurrence of a repeated
	// name — "kneeling" appears once under root and again under every weapon
	// and combat subtree, and the root one is the right default.
	TArray<int32> Queue;
	Queue.Add(0);
	for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
	{
		const FSWGAnimationState& State = OutHierarchy.States[Queue[QueueIndex]];
		if (!OutHierarchy.ByName.Contains(State.Name))
		{
			OutHierarchy.ByName.Add(State.Name, Queue[QueueIndex]);
		}
		Queue.Append(State.ChildIndices);
	}

	return true;
}
