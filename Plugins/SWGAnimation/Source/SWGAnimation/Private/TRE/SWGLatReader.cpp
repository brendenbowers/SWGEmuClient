#include "TRE/SWGLatReader.h"

#include "TRE/SWGIffReader.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGIFFChunkReader.h"

namespace
{
	constexpr FSWGIffTag TagLatt = SWG_IFF_TAG('L', 'A', 'T', 'T');
	constexpr FSWGIffTag TagAnim = SWG_IFF_TAG('A', 'N', 'I', 'M');
	constexpr FSWGIffTag TagPxat = SWG_IFF_TAG('P', 'X', 'A', 'T');
	constexpr FSWGIffTag TagSpat = SWG_IFF_TAG('S', 'P', 'A', 'T');
	constexpr FSWGIffTag TagPunf = SWG_IFF_TAG('P', 'U', 'N', 'F');
	constexpr FSWGIffTag TagPptr = SWG_IFF_TAG('P', 'P', 'T', 'R');
	constexpr FSWGIffTag TagAnms = SWG_IFF_TAG('A', 'N', 'M', 'S');
	constexpr FSWGIffTag TagDflt = SWG_IFF_TAG('D', 'F', 'L', 'T');
	constexpr FSWGIffTag TagSsat = SWG_IFF_TAG('S', 'S', 'A', 'T');
	constexpr FSWGIffTag TagVal  = SWG_IFF_TAG('V', 'A', 'L', ' ');
	constexpr FSWGIffTag TagTscl = SWG_IFF_TAG('T', 'S', 'C', 'L');
	constexpr FSWGIffTag TagDrat = SWG_IFF_TAG('D', 'R', 'A', 'T');
	constexpr FSWGIffTag TagDir  = SWG_IFF_TAG('D', 'I', 'R', ' ');
	constexpr FSWGIffTag TagYwat = SWG_IFF_TAG('Y', 'W', 'A', 'T');
	constexpr FSWGIffTag TagYneg = SWG_IFF_TAG('Y', 'N', 'E', 'G');
	constexpr FSWGIffTag TagYpos = SWG_IFF_TAG('Y', 'P', 'O', 'S');
	constexpr FSWGIffTag TagNone = SWG_IFF_TAG('N', 'O', 'N', 'E');
	constexpr FSWGIffTag TagAgat = SWG_IFF_TAG('A', 'G', 'A', 'T');
	constexpr FSWGIffTag TagActn = SWG_IFF_TAG('A', 'C', 'T', 'N');
	constexpr uint8 ForwardDirectionCode = 4;

	/** Reads one FORM PXAT's clip: its FORM 0000 holds INFO (the .ans path) and PUNF (the blend parameter name, stored twice). */
	bool ReadPxatClip(const FSWGIffReader& Reader, const FSWGIffChunk& PxatForm, FSWGLatClip& OutClip, float TimeScale)
	{
		OutClip.TimeScale = TimeScale;
		const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(PxatForm);
		if (VersionForms.Num() == 0)
		{
			return false;
		}

		FSWGIffChunk InfoChunk;
		if (!Reader.FindChildChunk(VersionForms[0], SWGIffTags::Info, InfoChunk))
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
			if (ParameterChunk.Tag == TagPptr)
			{
				ParameterReader.ReadTerminiatedString(OutClip.MaskPath);
			}
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
	bool CollectClips(const FSWGIffReader& Reader, const FSWGIffChunk& Node, TArray<FSWGLatClip>& OutClips, float TimeScale = 1.0f, FSWGLatEntry* Extras = nullptr);

	/** Reads the INFO + ACTN of an AGAT's version form into Extras (the first AGAT of an entry wins). */
	void ReadActionGenerator(const FSWGIffReader& Reader, const FSWGIffChunk& AgatForm, FSWGLatEntry& Extras)
	{
		const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(AgatForm);
		if (VersionForms.Num() == 0 || Extras.IdleActions.Num() > 0)
		{
			return;
		}

		FSWGIffChunk InfoChunk;
		if (Reader.FindChildChunk(VersionForms[0], SWGIffTags::Info, InfoChunk))
		{
			FSWGIFFChunkReader InfoReader(InfoChunk, Reader);
			InfoReader.ReadValueLE(Extras.IdleActionMinSeconds);
			InfoReader.ReadValueLE(Extras.IdleActionMaxSeconds);
		}

		FSWGIffChunk ActionChunk;
		if (!Reader.FindChildChunk(VersionForms[0], TagActn, ActionChunk))
		{
			return;
		}
		FSWGIFFChunkReader ActionReader(ActionChunk, Reader);
		const uint16 ActionCount = ActionReader.ReadValueLE<uint16>();
		for (uint16 Index = 0; Index < ActionCount && !ActionReader.AtEnd(); ++Index)
		{
			FSWGLatIdleAction Action;
			if (!ActionReader.ReadTerminiatedString(Action.ActionName) || !ActionReader.ReadValueLE(Action.Weight))
			{
				break;
			}
			Extras.IdleActions.Add(MoveTemp(Action));
		}
	}

	/** The first clip list a DRAT/YWAT branch's child template yields. */
	TArray<FSWGLatClip> CollectBranchClips(const FSWGIffReader& Reader, const FSWGIffChunk& BranchForm, float TimeScale)
	{
		TArray<FSWGLatClip> BranchClips;
		for (const FSWGIffChunk& Template : Reader.FindChildForms(BranchForm))
		{
			if (CollectClips(Reader, Template, BranchClips, TimeScale, nullptr))
			{
				break;
			}
		}
		return BranchClips;
	}

	bool CollectClips(const FSWGIffReader& Reader, const FSWGIffChunk& Node, TArray<FSWGLatClip>& OutClips, float TimeScale, FSWGLatEntry* Extras)
	{
		if (!Node.IsForm())
		{
			return false;
		}

		if (Node.FormType == TagPxat)
		{
			FSWGLatClip Clip;
			if (ReadPxatClip(Reader, Node, Clip, TimeScale))
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
		// list. Gender and mood aren't modelled at runtime, so the default
		// branch is the honest choice here; an entry whose own template is a
		// string selector (rider pose) also keeps every branch, see
		// ReadStringSelector.
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
				return CollectClips(Reader, Branches[FMath::Clamp(BranchIndex, 0, Branches.Num() - 1)], OutClips, TimeScale, Extras);
			}
			return false;
		}

		// TSCL scales every clip beneath it; its INFO is the float multiplier.
		if (Node.FormType == TagTscl)
		{
			const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(Node);
			FSWGIffChunk ScaleChunk;
			if (VersionForms.Num() > 0 && Reader.FindChildChunk(VersionForms[0], SWGIffTags::Info, ScaleChunk))
			{
				FSWGIFFChunkReader ScaleReader(ScaleChunk, Reader);
				float Scale = 1.0f;
				if (ScaleReader.ReadValueLE(Scale) && Scale > KINDA_SMALL_NUMBER)
				{
					TimeScale *= Scale;
				}
			}
		}

		if (Node.FormType == TagAgat && Extras)
		{
			ReadActionGenerator(Reader, Node, *Extras);
		}

		// DRAT: one child template per movement direction. The entry's own clips
		// are the Forward branch — taking the first authored one used to pick
		// DIR 1, a right strafe, so a creature in a pistol stance held the
		// strafe-right idle.
		if (Node.FormType == TagDrat)
		{
			TArray<uint8> Codes;
			TMap<uint8, TArray<FSWGLatClip>> ByDirection;
			for (const FSWGIffChunk& VersionForm : Reader.FindChildForms(Node))
			{
				for (const FSWGIffChunk& DirectionForm : Reader.FindChildForms(VersionForm))
				{
					FSWGIffChunk CodeChunk;
					if (DirectionForm.FormType != TagDir || !Reader.FindChildChunk(DirectionForm, SWGIffTags::Info, CodeChunk))
					{
						continue;
					}
					FSWGIFFChunkReader CodeReader(CodeChunk, Reader);
					const uint8 Code = CodeReader.ReadValueLE<uint8>();

					TArray<FSWGLatClip> DirectionClips = CollectBranchClips(Reader, DirectionForm, TimeScale);
					if (DirectionClips.Num() > 0)
					{
						Codes.Add(Code);
						ByDirection.Add(Code, MoveTemp(DirectionClips));
					}
				}
			}

			const TArray<FSWGLatClip>* ChosenClips = ByDirection.Find(ForwardDirectionCode);
			if (!ChosenClips && Codes.Num() > 0)
			{
				ChosenClips = ByDirection.Find(Codes[0]);
			}
			if (ChosenClips)
			{
				OutClips.Append(*ChosenClips);
			}
			if (Extras && Extras->ClipsByDirection.Num() == 0)
			{
				Extras->ClipsByDirection = MoveTemp(ByDirection);
			}
			return true;
		}

		// YWAT: turn-in-place variants. Clips follow the NONE branch; the turning
		// ones go to the extras.
		if (Node.FormType == TagYwat)
		{
			for (const FSWGIffChunk& VersionForm : Reader.FindChildForms(Node))
			{
				for (const FSWGIffChunk& Branch : Reader.FindChildForms(VersionForm))
				{
					TArray<FSWGLatClip> BranchClips = CollectBranchClips(Reader, Branch, TimeScale);
					if (Branch.FormType == TagNone)
					{
						OutClips.Append(BranchClips);
					}
					else if (Extras && Branch.FormType == TagYneg)
					{
						Extras->YawNegativeClips = MoveTemp(BranchClips);
					}
					else if (Extras && Branch.FormType == TagYpos)
					{
						Extras->YawPositiveClips = MoveTemp(BranchClips);
					}
				}
			}
			// Not "consumed": the walk goes on to the sibling clips of an enclosing
			// speed ramp (a creature's walk and run follow its idle's YWAT).
			return false;
		}

		bool bConsumedSpat = false;
		for (const FSWGIffChunk& Child : Reader.ReadChildren(Node))
		{
			if (CollectClips(Reader, Child, OutClips, TimeScale, Extras))
			{
				bConsumedSpat = true;
				break;
			}
		}

		return bConsumedSpat || Node.FormType == TagSpat;
	}

	/**
	 * Decodes a string selector's value table onto Entry, so a caller that
	 * knows the variable's runtime value can pick the right branch instead of
	 * the default CollectClips settles for. Layout, from all_m.lat's
	 * loop_riding: FORM SSAT > FORM 0000 > INFO (variable name), FORM ANMS
	 * (INFO u16 branch count, then one template per branch), VAL (u16 count,
	 * then that many {cstring value, u16 branch index}), DFLT (u16 branch).
	 */
	void ReadStringSelector(const FSWGIffReader& Reader, const FSWGIffChunk& SsatForm, FSWGLatEntry& Entry)
	{
		const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(SsatForm);
		if (VersionForms.Num() == 0)
		{
			return;
		}
		const FSWGIffChunk& VersionForm = VersionForms[0];

		FSWGIffChunk InfoChunk, AnimsForm, ValueChunk;
		if (!Reader.FindChildChunk(VersionForm, SWGIffTags::Info, InfoChunk)
			|| !Reader.FindChildForm(VersionForm, TagAnms, AnimsForm)
			|| !Reader.FindChildChunk(VersionForm, TagVal, ValueChunk))
		{
			return;
		}

		FString Variable;
		FSWGIFFChunkReader InfoReader(InfoChunk, Reader);
		if (!InfoReader.ReadTerminiatedString(Variable) || Variable.IsEmpty())
		{
			return;
		}

		const TArray<FSWGIffChunk> Branches = Reader.FindChildForms(AnimsForm);
		FSWGIFFChunkReader ValueReader(ValueChunk, Reader);
		const uint16 ValueCount = ValueReader.ReadValueLE<uint16>();
		for (uint16 Index = 0; Index < ValueCount && !ValueReader.AtEnd(); ++Index)
		{
			FString Value;
			uint16 BranchIndex = 0;
			if (!ValueReader.ReadTerminiatedString(Value) || !ValueReader.ReadValueLE(BranchIndex) || !Branches.IsValidIndex(BranchIndex))
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGLatReader: malformed VAL entry %u in selector '%s' of '%s'"), Index, *Variable, *Entry.LogicalName);
				return;
			}

			TArray<FSWGLatClip> BranchClips;
			CollectClips(Reader, Branches[BranchIndex], BranchClips, 1.0f, nullptr);
			if (BranchClips.Num() > 0)
			{
				Entry.ClipsBySelectorValue.Add(Value, MoveTemp(BranchClips));
			}
		}

		if (Entry.ClipsBySelectorValue.Num() > 0)
		{
			Entry.SelectorVariable = MoveTemp(Variable);
		}
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
	if (Reader.FindChildChunk(VersionForm, SWGIffTags::Info, InfoChunk))
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
		if (!Reader.FindChildChunk(Child, SWGIffTags::Info, AnimInfo))
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
			if (Wrapper.FormType == TagSsat)
			{
				ReadStringSelector(Reader, Wrapper, Entry);
			}
			if (CollectClips(Reader, Wrapper, Entry.Clips, 1.0f, &Entry))
			{
				break;
			}
		}

		if (Entry.Clips.Num() > 0)
		{
			Entry.TimeScale = Entry.Clips[0].TimeScale;
			const FString Key = Entry.LogicalName;
			OutData.Entries.Add(Key, MoveTemp(Entry));
		}
	}

	return OutData.Entries.Num() > 0;
}
