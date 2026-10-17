#include "TRE/SWGClientDataFileReader.h"

#include "TRE/SWGIFFChunkReader.h"

namespace
{
	// CSSI and WCSI share one layout: [name\0][int32 LE].
	void ReadNamedInt(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, TMap<FString, int32>& OutValues)
	{
		FSWGIFFChunkReader ChunkReader(Chunk, Reader);
		ChunkReader.ReadMapToEnd<FString, int32>(OutValues,
			[&ChunkReader](FString& Name) { return ChunkReader.ReadTerminiatedString(Name) && !Name.IsEmpty(); },
			[&ChunkReader](int32& Value) { return ChunkReader.ReadValueLE(Value); });
	}

	// CSND, CEFT and EVNT share one layout: [event\0][target\0], the target possibly unterminated.
	void ReadEvent(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, TArray<FSWGClientDataEvent>& OutEvents)
	{
		FSWGIFFChunkReader ChunkReader(Chunk, Reader);
		FSWGClientDataEvent Event;
		if (ChunkReader.ReadTerminiatedString(Event.EventName) && ChunkReader.ReadLenientString(Event.Target))
		{
			Event.Target.ReplaceInline(TEXT("\\"), TEXT("/"));
			OutEvents.Add(MoveTemp(Event));
		}
	}
}

bool FSWGClientDataFileReader::Read(const FSWGIffReader& Reader, FSWGClientDataFile& OutData)
{
	OutData = FSWGClientDataFile();

	FSWGIffChunk CldfForm, Form0000;
	if (!Reader.IsValid()
		|| !Reader.FindForm(SWG_IFF_TAG('C', 'L', 'D', 'F'), CldfForm)
		|| !Reader.FindChildForm(CldfForm, SWG_IFF_TAG('0', '0', '0', '0'), Form0000))
	{
		return false;
	}

	for (const FSWGIffChunk& Child : Reader.ReadChildren(Form0000))
	{
		if (Child.Tag == SWG_IFF_TAG('C', 'S', 'S', 'I'))
		{
			ReadNamedInt(Reader, Child, OutData.Customization);
			continue;
		}

		if (Child.Tag == SWG_IFF_TAG('A', 'S', 'N', 'D'))
		{
			FSWGIFFChunkReader ChunkReader(Child, Reader);
			FString SoundPath;
			if (ChunkReader.ReadLenientString(SoundPath) && !SoundPath.IsEmpty())
			{
				SoundPath.ReplaceInline(TEXT("\\"), TEXT("/"));
				OutData.AttachedSounds.Add(MoveTemp(SoundPath));
			}
			continue;
		}

		if (Child.Tag == SWG_IFF_TAG('C', 'S', 'N', 'D'))
		{
			ReadEvent(Reader, Child, OutData.SoundEvents);
			continue;
		}

		if (Child.Tag == SWG_IFF_TAG('C', 'E', 'F', 'T'))
		{
			ReadEvent(Reader, Child, OutData.EffectEvents);
			continue;
		}

		if (Child.Tag == SWG_IFF_TAG('E', 'V', 'N', 'T'))
		{
			ReadEvent(Reader, Child, OutData.PlayerEvents);
			continue;
		}

		if (Child.Tag == SWG_IFF_TAG('H', 'O', 'B', 'J'))
		{
			FSWGIFFChunkReader ChunkReader(Child, Reader);
			FSWGClientDataHardpointObject Object;
			if (ChunkReader.ReadLenientString(Object.AppearancePath))
			{
				// One chunk has no hardpoint at all.
				ChunkReader.ReadLenientString(Object.Hardpoint);
				Object.AppearancePath.ReplaceInline(TEXT("\\"), TEXT("/"));
				OutData.HardpointObjects.Add(MoveTemp(Object));
			}
			continue;
		}

		if (Child.Tag == SWG_IFF_TAG('L', 'O', 'B', 'J'))
		{
			FSWGIFFChunkReader ChunkReader(Child, Reader);
			FSWGClientDataLight Light;
			if (ChunkReader.ReadValueLE(Light.Type) && ChunkReader.ReadTerminiatedString(Light.EffectPath)
				&& ChunkReader.GetRemaining() == (int32)sizeof(Light.Params))
			{
				for (float& Param : Light.Params)
				{
					ChunkReader.ReadValueLE(Param);
				}
				Light.EffectPath.ReplaceInline(TEXT("\\"), TEXT("/"));
				Light.Color = FLinearColor(Light.Params[0], Light.Params[1], Light.Params[2]);
				Light.SecondColor = FLinearColor(Light.Params[3], Light.Params[4], Light.Params[5]);
				Light.Range = Light.Params[6];
				Light.SecondRange = Light.Params[7];
				Light.Offset = FVector3f(Light.Params[14], Light.Params[15], Light.Params[16]);
				int32 NightFlag = 0;
				FMemory::Memcpy(&NightFlag, &Light.Params[13], sizeof(NightFlag));
				Light.bNightOnly = NightFlag != 0;
				OutData.Lights.Add(MoveTemp(Light));
			}
			continue;
		}

		if (Child.Tag == SWG_IFF_TAG('C', 'H', 'L', 'D') || Child.Tag == SWG_IFF_TAG('C', 'H', 'L', '2'))
		{
			FSWGIFFChunkReader ChunkReader(Child, Reader);
			FSWGClientDataChild Placed;
			Placed.bObjectTemplate = Child.Tag == SWG_IFF_TAG('C', 'H', 'L', '2');
			if (ChunkReader.ReadTerminiatedString(Placed.Path) && ChunkReader.GetRemaining() == 17 * (int32)sizeof(float))
			{
				ChunkReader.ReadValueLE(Placed.Offset.X);
				ChunkReader.ReadValueLE(Placed.Offset.Y);
				ChunkReader.ReadValueLE(Placed.Offset.Z);
				ChunkReader.ReadValueLE(Placed.HeadingDegrees);
				for (float& Extra : Placed.Extra)
				{
					ChunkReader.ReadValueLE(Extra);
				}
				Placed.Path.ReplaceInline(TEXT("\\"), TEXT("/"));
				OutData.Children.Add(MoveTemp(Placed));
			}
			continue;
		}

		if (Child.IsForm() && Child.FormType == SWG_IFF_TAG('V', 'T', 'H', 'R'))
		{
			FSWGClientDataThrust Thrust;
			for (const FSWGIffChunk& ThrustChild : Reader.ReadChildren(Child))
			{
				FSWGIFFChunkReader ChunkReader(ThrustChild, Reader);
				if (ThrustChild.Tag == SWG_IFF_TAG('I', 'N', 'F', 'O'))
				{
					ChunkReader.ReadValueLE(Thrust.Level);
				}
				else if (ThrustChild.Tag == SWG_IFF_TAG('H', 'O', 'B', 'J'))
				{
					FSWGClientDataHardpointObject Thruster;
					if (ChunkReader.ReadLenientString(Thruster.AppearancePath))
					{
						ChunkReader.ReadLenientString(Thruster.Hardpoint);
						Thruster.AppearancePath.ReplaceInline(TEXT("\\"), TEXT("/"));
						Thrust.Thrusters.Add(MoveTemp(Thruster));
					}
				}
				else if (ThrustChild.Tag == SWG_IFF_TAG('V', 'S', 'N', 'D'))
				{
					// Hardpoint, then four vehicle sounds, then the damage fire sounds; empties are kept to hold position.
					FString* Slots[] = { &Thrust.SoundHardpoint, &Thrust.IdleSound, &Thrust.AccelSound, &Thrust.DecelSound, &Thrust.RunSound };
					for (FString* Slot : Slots)
					{
						ChunkReader.ReadLenientString(*Slot);
						Slot->ReplaceInline(TEXT("\\"), TEXT("/"));
					}
					FString FireSound;
					while (!ChunkReader.AtEnd() && ChunkReader.ReadLenientString(FireSound))
					{
						FireSound.ReplaceInline(TEXT("\\"), TEXT("/"));
						if (!FireSound.IsEmpty())
						{
							Thrust.FireSounds.Add(FireSound);
						}
					}
				}
			}
			OutData.Thrusts.Add(MoveTemp(Thrust));
			continue;
		}

		if (Child.IsForm() && Child.FormType == SWG_IFF_TAG('V', 'G', 'E', 'F'))
		{
			for (const FSWGIffChunk& EffectChild : Reader.ReadChildren(Child))
			{
				FSWGIFFChunkReader ChunkReader(EffectChild, Reader);
				FSWGClientDataGroundEffect Effect;
				uint8 Surface = 0;
				if (EffectChild.Tag == SWG_IFF_TAG('I', 'N', 'F', 'O') && ChunkReader.ReadValueLE(Surface)
					&& ChunkReader.ReadLenientString(Effect.Sound) && ChunkReader.ReadLenientString(Effect.Hardpoint)
					&& ChunkReader.ReadLenientString(Effect.AppearancePath))
				{
					Effect.bWater = Surface != 0;
					Effect.Sound.ReplaceInline(TEXT("\\"), TEXT("/"));
					Effect.AppearancePath.ReplaceInline(TEXT("\\"), TEXT("/"));
					OutData.GroundEffects.Add(MoveTemp(Effect));
				}
			}
			continue;
		}

		if (Child.IsForm() && (Child.FormType == SWG_IFF_TAG('F', 'L', 'A', 'G') || Child.FormType == SWG_IFF_TAG('B', 'A', 'N', 'R')))
		{
			FSWGClientDataFlag Flag;
			Flag.bBanner = Child.FormType == SWG_IFF_TAG('B', 'A', 'N', 'R');
			for (const FSWGIffChunk& FlagChild : Reader.ReadChildren(Child))
			{
				FSWGIFFChunkReader ChunkReader(FlagChild, Reader);
				if (FlagChild.Tag == SWG_IFF_TAG('A', 'P', 'P', 'R'))
				{
					ChunkReader.ReadLenientString(Flag.AppearancePath);
					Flag.AppearancePath.ReplaceInline(TEXT("\\"), TEXT("/"));
				}
				else if (FlagChild.Tag == SWG_IFF_TAG('H', 'P', 'N', 'T'))
				{
					ChunkReader.ReadLenientString(Flag.Hardpoint);
				}
				else if (FlagChild.Tag == SWG_IFF_TAG('W', 'S', 'C', 'L'))
				{
					ChunkReader.ReadValueLE(Flag.WindScale);
				}
				else if (FlagChild.Tag == SWG_IFF_TAG('M', 'A', 'S', 'P'))
				{
					ChunkReader.ReadValueLE(Flag.MaxWindSpeed);
				}
				else if (FlagChild.Tag == SWG_IFF_TAG('L', 'I', 'F', 'T'))
				{
					ChunkReader.ReadValueLE(Flag.Lift);
				}
			}
			if (!Flag.AppearancePath.IsEmpty())
			{
				OutData.Flags.Add(MoveTemp(Flag));
			}
			continue;
		}

		if (!Child.IsForm() || Child.FormType != SWG_IFF_TAG('W', 'E', 'A', 'R'))
		{
			continue;
		}

		FSWGClientDataWearable Wearable;
		for (const FSWGIffChunk& WearChild : Reader.ReadChildren(Child))
		{
			if (WearChild.Tag == SWG_IFF_TAG('M', 'E', 'S', 'H'))
			{
				FSWGIFFChunkReader ChunkReader(WearChild, Reader);
				FString MeshPath;
				if (ChunkReader.ReadTerminiatedString(MeshPath) && !MeshPath.IsEmpty())
				{
					Wearable.MeshPaths.Add(MoveTemp(MeshPath));
				}
			}
			else if (WearChild.Tag == SWG_IFF_TAG('W', 'C', 'S', 'I'))
			{
				ReadNamedInt(Reader, WearChild, Wearable.Customization);
			}
		}

		if (!Wearable.MeshPaths.IsEmpty())
		{
			OutData.Wearables.Add(MoveTemp(Wearable));
		}
	}

	return true;
}
