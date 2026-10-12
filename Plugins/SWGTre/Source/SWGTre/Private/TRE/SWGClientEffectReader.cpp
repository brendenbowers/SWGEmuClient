#include "TRE/SWGClientEffectReader.h"

#include "TRE/SWGIFFChunkReader.h"

bool FSWGClientEffectReader::Read(const FSWGIffReader& Reader, FSWGClientEffect& OutEffect)
{
	OutEffect = FSWGClientEffect();

	FSWGIffChunk ClefForm;
	if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('C', 'L', 'E', 'F'), ClefForm))
	{
		return false;
	}

	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(ClefForm);
	if (VersionForms.IsEmpty())
	{
		return false;
	}

	const FSWGIffChunk& VersionForm = VersionForms[0];
	OutEffect.Version = FCString::Atoi(*VersionForm.FormType.ToString());
	if (OutEffect.Version < 1 || OutEffect.Version > 3)
	{
		return false;
	}

	for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForm))
	{
		if (Child.IsForm())
		{
			continue;
		}

		FSWGIFFChunkReader Chunk(Child, Reader);
		bool bOk = true;

		if (Child.Tag == SWG_IFF_TAG('P', 'S', 'N', 'D'))
		{
			FString Path;
			bOk = Chunk.ReadLenientString(Path);
			Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			if (bOk)
			{
				OutEffect.SoundPaths.Add(MoveTemp(Path));
			}
		}
		else if (Child.Tag == SWG_IFF_TAG('C', 'P', 'A', 'P'))
		{
			FSWGClientEffectParticle Particle;
			bOk = Chunk.ReadLenientString(Particle.Path) && Chunk.ReadValueLE(Particle.Scale);
			if (bOk && OutEffect.Version >= 2)
			{
				bOk = Chunk.ReadValueLE(Particle.Flag);
			}
			if (bOk && OutEffect.Version >= 3)
			{
				for (float& TintValue : Particle.Tint)
				{
					bOk = bOk && Chunk.ReadValueLE(TintValue);
				}
			}
			OutEffect.Particles.Add(MoveTemp(Particle));
		}
		else if (Child.Tag == SWG_IFF_TAG('C', 'L', 'G', 'T'))
		{
			FSWGClientEffectLight Light;
			for (uint8& ColorByte : Light.Color)
			{
				bOk = bOk && Chunk.ReadValueLE(ColorByte);
			}
			for (float& Param : Light.Params)
			{
				bOk = bOk && Chunk.ReadValueLE(Param);
			}
			OutEffect.Lights.Add(Light);
		}
		else if (Child.Tag == SWG_IFF_TAG('C', 'A', 'M', 'S'))
		{
			FSWGClientEffectCameraShake Shake;
			bOk = Chunk.ReadValueLE(Shake.Amplitude) && Chunk.ReadValueLE(Shake.Frequency)
				&& Chunk.ReadValueLE(Shake.Duration) && Chunk.ReadValueLE(Shake.Range);
			OutEffect.CameraShakes.Add(Shake);
		}
		else if (Child.Tag == SWG_IFF_TAG('F', 'F', 'B', 'K'))
		{
			FSWGClientEffectForceFeedback Feedback;
			bOk = Chunk.ReadLenientString(Feedback.Path) && Chunk.ReadValueLE(Feedback.Flag) && Chunk.ReadValueLE(Feedback.Range);
			OutEffect.ForceFeedbacks.Add(MoveTemp(Feedback));
		}

		if (!bOk)
		{
			return false;
		}
	}

	return true;
}
