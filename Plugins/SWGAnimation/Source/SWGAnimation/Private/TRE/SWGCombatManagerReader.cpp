#include "TRE/SWGCombatManagerReader.h"

#include "TRE/SWGIffReader.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGIFFChunkReader.h"
#include "TRE/SWGCrc32.h"

namespace
{
	constexpr FSWGIffTag TagCbtm = SWG_IFF_TAG('C', 'B', 'T', 'M');
	constexpr FSWGIffTag TagEntr = SWG_IFF_TAG('E', 'N', 'T', 'R');
	constexpr FSWGIffTag TagKey  = SWG_IFF_TAG('K', 'E', 'Y', ' ');
	constexpr FSWGIffTag TagDdsp = SWG_IFF_TAG('D', 'D', 'S', 'P');
	constexpr FSWGIffTag TagDeps = SWG_IFF_TAG('D', 'E', 'P', 'S');
	constexpr FSWGIffTag TagSngl = SWG_IFF_TAG('S', 'N', 'G', 'L');
	constexpr FSWGIffTag TagVars = SWG_IFF_TAG('V', 'A', 'R', 'S');
	constexpr FSWGIffTag TagStrn = SWG_IFF_TAG('S', 'T', 'R', 'N');
	constexpr FSWGIffTag TagDisp = SWG_IFF_TAG('D', 'I', 'S', 'P');
	constexpr FSWGIffTag TagPost = SWG_IFF_TAG('P', 'O', 'S', 'T');

	/** Variable names the entries use. Four characters, no separator before the value. */
	const FString VarAttackerAction = TEXT("TCAA");
	const FString VarDefenderAction = TEXT("CAHD");

	constexpr int32 VarNameLength = 4;

	/** Reads a FORM SNGL: the playback script path plus the VARS that parameterise it. */
	bool ReadSingle(const FSWGIffReader& Reader, const FSWGIffChunk& SnglForm, FSWGCombatActionAnimation& OutAnimation)
	{
		FSWGIffChunk NameChunk;
		if (Reader.FindChildChunk(SnglForm, SWGIffTags::Name, NameChunk))
		{
			FSWGIFFChunkReader NameReader(NameChunk, Reader);
			NameReader.ReadTerminiatedString(OutAnimation.PlaybackScript);
			// One trailing byte after the path. It varies per entry and
			// nothing here needs it, so it is left unread.
		}

		FSWGIffChunk VarsForm;
		if (!Reader.FindChildForm(SnglForm, TagVars, VarsForm))
		{
			return OutAnimation.IsValid();
		}

		for (const FSWGIffChunk& Strn : Reader.FindAllChildChunks(VarsForm, TagStrn))
		{
			FSWGIFFChunkReader StrnReader(Strn, Reader);
			if (!StrnReader.CanRead(VarNameLength))
			{
				continue;
			}

			// The variable name is a fixed-width four characters with no
			// terminator; the value's terminator is the first null in the
			// chunk, so the name has to be taken by length, not by string.
			ANSICHAR NameChars[VarNameLength + 1] = {};
			for (int32 i = 0; i < VarNameLength; ++i)
			{
				NameChars[i] = static_cast<ANSICHAR>(StrnReader.ReadValueLE<uint8>());
			}

			const FString VarName(ANSI_TO_TCHAR(NameChars));

			FString Value;
			StrnReader.ReadTerminiatedString(Value);

			if (VarName == VarAttackerAction)
			{
				OutAnimation.AttackerAction = MoveTemp(Value);
			}
			else if (VarName == VarDefenderAction)
			{
				OutAnimation.DefenderAction = MoveTemp(Value);
			}
			// Other variables (HSWS, speeds, timings) belong to the playback
			// script, which nothing runs yet.
		}

		return OutAnimation.IsValid();
	}

	/** Reads the uint16 selector a dispatch arm is keyed by, defaulting to the default arm when absent. */
	uint16 ReadArmSelector(const FSWGIffReader& Reader, const FSWGIffChunk& ArmForm, FSWGIffTag SelectorTag)
	{
		FSWGIffChunk SelectorChunk;
		if (!Reader.FindChildChunk(ArmForm, SelectorTag, SelectorChunk))
		{
			return FSWGCombatManagerEntry::DefaultArm;
		}

		FSWGIFFChunkReader SelectorReader(SelectorChunk, Reader);
		uint16 Value = FSWGCombatManagerEntry::DefaultArm;
		SelectorReader.ReadValueLE(Value);
		return Value;
	}

	/**
	 * Reads whatever sits under a dispatch arm — either a FORM DEPS of
	 * defender-posture arms, or a bare SNGL that applies to every posture.
	 */
	void ReadPostureArms(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, TMap<uint16, FSWGCombatActionAnimation>& OutByPosture)
	{
		FSWGIffChunk DepsForm;
		if (Reader.FindChildForm(Parent, TagDeps, DepsForm))
		{
			for (const FSWGIffChunk& Arm : Reader.FindChildForms(DepsForm))
			{
				if (Arm.FormType != TagEntr)
				{
					continue;
				}

				FSWGIffChunk SnglForm;
				if (!Reader.FindChildForm(Arm, TagSngl, SnglForm))
				{
					continue;
				}

				FSWGCombatActionAnimation Animation;
				ReadSingle(Reader, SnglForm, Animation);
				OutByPosture.Add(ReadArmSelector(Reader, Arm, TagPost), MoveTemp(Animation));
			}
			return;
		}

		FSWGIffChunk SnglForm;
		if (Reader.FindChildForm(Parent, TagSngl, SnglForm))
		{
			FSWGCombatActionAnimation Animation;
			ReadSingle(Reader, SnglForm, Animation);
			OutByPosture.Add(FSWGCombatManagerEntry::DefaultArm, MoveTemp(Animation));
		}
	}
}

const FSWGCombatActionAnimation* FSWGCombatManagerEntry::Resolve(uint8 HitResult, uint8 DefenderPosture) const
{
	// Both levels fall back to their default arm, and an entry written as a
	// bare SNGL is stored as default/default — so a lookup that misses on
	// both axes still lands on the one animation such an entry has.
	const TMap<uint16, FSWGCombatActionAnimation>* ByPosture = ByHitThenPosture.Find(HitResult);
	if (!ByPosture)
	{
		ByPosture = ByHitThenPosture.Find(DefaultArm);
	}

	if (!ByPosture)
	{
		return nullptr;
	}

	if (const FSWGCombatActionAnimation* Exact = ByPosture->Find(DefenderPosture))
	{
		return Exact;
	}

	return ByPosture->Find(DefaultArm);
}

const FSWGCombatManagerEntry* FSWGCombatManagerData::FindByName(const FString& Key) const
{
	return FindByCrc(FSWGCrc32::HashString(Key));
}

bool FSWGCombatManagerReader::ReadCombatManager(const FSWGIffReader& Reader, FSWGCombatManagerData& OutData)
{
	OutData.Entries.Reset();
	OutData.ByKeyCrc.Reset();

	FSWGIffChunk CbtmForm;
	if (!Reader.IsValid() || !Reader.FindForm(TagCbtm, CbtmForm))
	{
		return false;
	}

	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(CbtmForm);
	if (VersionForms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCombatManagerReader: CBTM has no version FORM"));
		return false;
	}

	for (const FSWGIffChunk& EntryForm : Reader.FindChildForms(VersionForms[0]))
	{
		if (EntryForm.FormType != TagEntr)
		{
			continue;
		}

		FSWGIffChunk KeyChunk;
		if (!Reader.FindChildChunk(EntryForm, TagKey, KeyChunk))
		{
			continue;
		}

		FSWGCombatManagerEntry Entry;
		FSWGIFFChunkReader KeyReader(KeyChunk, Reader);
		if (!KeyReader.ReadTerminiatedString(Entry.Key) || Entry.Key.IsEmpty())
		{
			continue;
		}

		FSWGIffChunk DdspForm;
		if (Reader.FindChildForm(EntryForm, TagDdsp, DdspForm))
		{
			for (const FSWGIffChunk& Arm : Reader.FindChildForms(DdspForm))
			{
				if (Arm.FormType != TagEntr)
				{
					continue;
				}

				TMap<uint16, FSWGCombatActionAnimation> ByPosture;
				ReadPostureArms(Reader, Arm, ByPosture);
				if (ByPosture.Num() > 0)
				{
					Entry.ByHitThenPosture.Add(ReadArmSelector(Reader, Arm, TagDisp), MoveTemp(ByPosture));
				}
			}
		}
		else
		{
			// No hit-result dispatch: the entry is one animation for every
			// outcome, stored under both default arms.
			TMap<uint16, FSWGCombatActionAnimation> ByPosture;
			ReadPostureArms(Reader, EntryForm, ByPosture);
			if (ByPosture.Num() > 0)
			{
				Entry.ByHitThenPosture.Add(FSWGCombatManagerEntry::DefaultArm, MoveTemp(ByPosture));
			}
		}

		if (Entry.ByHitThenPosture.Num() == 0)
		{
			continue;
		}

		const uint32 KeyCrc = FSWGCrc32::HashString(Entry.Key);
		const int32 Index = OutData.Entries.Add(MoveTemp(Entry));

		// Later duplicates would be the patched-over version of an earlier
		// key; keep the last, matching how the TRE layering resolves files.
		OutData.ByKeyCrc.Add(KeyCrc, Index);
	}

	UE_LOG(LogTemp, Log, TEXT("FSWGCombatManagerReader: read %d combat action entries"), OutData.Entries.Num());
	return OutData.Entries.Num() > 0;
}
