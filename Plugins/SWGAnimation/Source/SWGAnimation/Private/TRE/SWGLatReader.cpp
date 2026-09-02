#include "TRE/SWGLatReader.h"

#include "TRE/SWGIffReader.h"
#include "TRE/SWGIFFChunkReader.h"

namespace
{
	constexpr FSWGIffTag TagLatt = SWG_IFF_TAG('L', 'A', 'T', 'T');
	constexpr FSWGIffTag TagAnim = SWG_IFF_TAG('A', 'N', 'I', 'M');
	constexpr FSWGIffTag TagPxat = SWG_IFF_TAG('P', 'X', 'A', 'T');
	constexpr FSWGIffTag TagSpat = SWG_IFF_TAG('S', 'P', 'A', 'T');
	constexpr FSWGIffTag TagInfo = SWG_IFF_TAG('I', 'N', 'F', 'O');
	constexpr FSWGIffTag TagPunf = SWG_IFF_TAG('P', 'U', 'N', 'F');
	constexpr FSWGIffTag TagPptr = SWG_IFF_TAG('P', 'P', 'T', 'R');
	constexpr FSWGIffTag TagAnms = SWG_IFF_TAG('A', 'N', 'M', 'S');
	constexpr FSWGIffTag TagDflt = SWG_IFF_TAG('D', 'F', 'L', 'T');

	/** Reads one FORM PXAT's clip: its FORM 0000 holds INFO (the .ans path) and PUNF (the blend parameter name, stored twice). */
	bool ReadPxatClip(const FSWGIffReader& Reader, const FSWGIffChunk& PxatForm, FSWGLatClip& OutClip)
	{
		const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(PxatForm);
		if (VersionForms.Num() == 0)
		{
			return false;
		}

		FSWGIffChunk InfoChunk;
		if (!Reader.FindChildChunk(VersionForms[0], TagInfo, InfoChunk))
		{
			return false;
		}

		FSWGIFFChunkReader InfoReader(InfoChunk, Reader);
		if (!InfoReader.ReadTerminiatedString(OutClip.AnsPath) || OutClip.AnsPath.IsEmpty())
		{
			return false;
		}

		// The blend parameter name comes from either PUNF or PPTR — the same
		// leading string, but PPTR follows it with a bone-mask path (e.g.
		// "zero_speed" + "appearance/mask/mask_upper_body.iff") for clips that
		// drive only part of the body. Both must be read: a clip tagged solely
		// via PPTR would otherwise look untagged, and the idle search would
		// skip past it to some later clip that happens to use PUNF.
		//
		// Entries under a plain PXAT wrapper (single-clip logical animations)
		// legitimately carry neither.
		FSWGIffChunk ParameterChunk;
		if (Reader.FindChildChunk(VersionForms[0], TagPunf, ParameterChunk)
			|| Reader.FindChildChunk(VersionForms[0], TagPptr, ParameterChunk))
		{
			FSWGIFFChunkReader ParameterReader(ParameterChunk, Reader);
			ParameterReader.ReadTerminiatedString(OutClip.ParameterName);
		}

		return true;
	}

	/**
	 * Depth-first collect of every PXAT under Node, in file order. Returns
	 * true once a SPAT subtree has been consumed, which stops the walk from
	 * continuing into that SPAT's siblings.
	 *
	 * That stop matters because some logical animations wrap several complete
	 * SPATs in a selector — loop_standing is a "gender" selector over a male
	 * SPAT and a female SPAT, each holding its own idle/walk/run set. Walking
	 * both would interleave two species of clip into one speed ramp, so the
	 * first branch wins, the same way the previous substring-matching clip
	 * search resolved "_loc_walk" to the male clip.
	 */
	bool CollectClips(const FSWGIffReader& Reader, const FSWGIffChunk& Node, TArray<FSWGLatClip>& OutClips)
	{
		if (!Node.IsForm())
		{
			return false;
		}

		if (Node.FormType == TagPxat)
		{
			FSWGLatClip Clip;
			if (ReadPxatClip(Reader, Node, Clip))
			{
				OutClips.Add(MoveTemp(Clip));
			}
			return false;
		}

		// A selector: FORM ANMS lists the branches, and the sibling DFLT names
		// which one applies when nothing has chosen otherwise. Only that branch
		// is real for a default-state creature — loop_sitting_ground nests a
		// "gender" selector over a "mood" selector, so walking every branch
		// would fold the female clips and the meditating mood into one clip
		// list. Runtime selector inputs (gender, mood, rider pose) aren't
		// modelled yet, so the default branch is the honest choice.
		FSWGIffChunk AnimsForm;
		if (Reader.FindChildForm(Node, TagAnms, AnimsForm))
		{
			int32 BranchIndex = 0;
			FSWGIffChunk DefaultChunk;
			if (Reader.FindChildChunk(Node, TagDflt, DefaultChunk))
			{
				FSWGIFFChunkReader DefaultReader(DefaultChunk, Reader);
				BranchIndex = (int32)DefaultReader.ReadValueLE<uint16>();
			}

			const TArray<FSWGIffChunk> Branches = Reader.FindChildForms(AnimsForm);
			if (Branches.Num() > 0)
			{
				return CollectClips(Reader, Branches[FMath::Clamp(BranchIndex, 0, Branches.Num() - 1)], OutClips);
			}
			return false;
		}

		bool bConsumedSpat = false;
		for (const FSWGIffChunk& Child : Reader.ReadChildren(Node))
		{
			if (CollectClips(Reader, Child, OutClips))
			{
				bConsumedSpat = true;
				break;
			}
		}

		return bConsumedSpat || Node.FormType == TagSpat;
	}
}

bool FSWGLatReader::ReadLat(const FSWGIffReader& Reader, FSWGLatData& OutData)
{
	OutData.AshPath.Reset();
	OutData.Entries.Reset();

	FSWGIffChunk LattForm;
	if (!Reader.IsValid() || !Reader.FindForm(TagLatt, LattForm))
	{
		return false;
	}

	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(LattForm);
	if (VersionForms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGLatReader: LATT has no version FORM"));
		return false;
	}
	const FSWGIffChunk& VersionForm = VersionForms[0]; // FORM 0000

	// The table-level INFO leads with the .ash path. It has a few trailing
	// bytes after the terminator (a gender/skeleton discriminator, not needed
	// here) — reading just the first string is deliberate.
	FSWGIffChunk InfoChunk;
	if (Reader.FindChildChunk(VersionForm, TagInfo, InfoChunk))
	{
		FSWGIFFChunkReader InfoReader(InfoChunk, Reader);
		InfoReader.ReadTerminiatedString(OutData.AshPath);
	}

	for (const FSWGIffChunk& Child : Reader.FindChildForms(VersionForm))
	{
		if (Child.FormType != TagAnim)
		{
			continue;
		}

		FSWGIffChunk AnimInfo;
		if (!Reader.FindChildChunk(Child, TagInfo, AnimInfo))
		{
			continue;
		}

		FSWGLatEntry Entry;
		FSWGIFFChunkReader AnimInfoReader(AnimInfo, Reader);
		if (!AnimInfoReader.ReadTerminiatedString(Entry.LogicalName) || Entry.LogicalName.IsEmpty())
		{
			continue;
		}

		for (const FSWGIffChunk& Wrapper : Reader.FindChildForms(Child))
		{
			if (CollectClips(Reader, Wrapper, Entry.Clips))
			{
				break;
			}
		}

		if (Entry.Clips.Num() > 0)
		{
			const FString Key = Entry.LogicalName;
			OutData.Entries.Add(Key, MoveTemp(Entry));
		}
	}

	return OutData.Entries.Num() > 0;
}
