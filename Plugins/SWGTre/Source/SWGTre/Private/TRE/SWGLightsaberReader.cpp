#include "TRE/SWGLightsaberReader.h"

#include "TRE/SWGFxCommon.h"
#include "TRE/SWGIFFChunkReader.h"

namespace
{
	bool ReadPath(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, FString& OutPath)
	{
		FSWGIFFChunkReader Cursor(Chunk, Reader);
		if (!Cursor.ReadLenientString(OutPath))
		{
			return false;
		}
		FSWGFxCommonReader::NormalizePath(OutPath);
		return true;
	}

	bool ReadFloat(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, float& OutValue)
	{
		FSWGIFFChunkReader Cursor(Chunk, Reader);
		return Cursor.ReadValueLE(OutValue);
	}
}

bool FSWGLightsaberReader::Read(const FSWGIffReader& Reader, FSWGLightsaber& OutSaber)
{
	OutSaber = FSWGLightsaber();

	FSWGIffChunk LsatForm, Form0000;
	if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('L', 'S', 'A', 'T'), LsatForm)
		|| !Reader.FindChildForm(LsatForm, SWG_IFF_TAG('0', '0', '0', '0'), Form0000))
	{
		return false;
	}

	for (const FSWGIffChunk& Child : Reader.ReadChildren(Form0000))
	{
		if (!Child.IsForm())
		{
			if (Child.Tag == SWG_IFF_TAG('B', 'A', 'S', 'E') && !ReadPath(Reader, Child, OutSaber.HiltAppearancePath))
			{
				return false;
			}
			if (Child.Tag == SWG_IFF_TAG('A', 'S', 'N', 'D') && !ReadPath(Reader, Child, OutSaber.HumSoundPath))
			{
				return false;
			}
			continue;
		}

		if (Child.FormType == SWG_IFF_TAG('L', 'G', 'H', 'T'))
		{
			OutSaber.bHasLight = true;
			FSWGLightsaberLight& Light = OutSaber.Light;
			for (const FSWGIffChunk& Part : Reader.ReadChildren(Child))
			{
				FSWGIFFChunkReader Cursor(Part, Reader);
				bool bOk = true;
				if (Part.Tag == SWG_IFF_TAG('C', 'O', 'L', 'R'))
				{
					bOk = Cursor.ReadValueLE(Light.Color.R) && Cursor.ReadValueLE(Light.Color.G) && Cursor.ReadValueLE(Light.Color.B) && Cursor.ReadValueLE(Light.Color.A);
				}
				else if (Part.Tag == SWG_IFF_TAG('R', 'A', 'N', 'G'))
				{
					bOk = Cursor.ReadValueLE(Light.RangeMin) && Cursor.ReadValueLE(Light.RangeMax);
				}
				else if (Part.Tag == SWG_IFF_TAG('T', 'I', 'M', 'E'))
				{
					bOk = Cursor.ReadValueLE(Light.TimeMin) && Cursor.ReadValueLE(Light.TimeMax);
				}
				else if (Part.Tag == SWG_IFF_TAG('D', 'A', 'Y', 'N'))
				{
					bOk = Cursor.ReadValueLE(Light.DayNight);
				}
				if (!bOk)
				{
					return false;
				}
			}
		}
		else if (Child.FormType == SWG_IFF_TAG('B', 'L', 'A', 'D'))
		{
			FSWGLightsaberBlade Blade;
			for (const FSWGIffChunk& Part : Reader.ReadChildren(Child))
			{
				bool bOk = true;
				if (Part.Tag == SWG_IFF_TAG('S', 'H', 'D', 'R'))
				{
					bOk = ReadPath(Reader, Part, Blade.ShaderPath);
				}
				else if (Part.Tag == SWG_IFF_TAG('L', 'G', 'T', 'H'))
				{
					bOk = ReadFloat(Reader, Part, Blade.Length);
				}
				else if (Part.Tag == SWG_IFF_TAG('W', 'D', 'T', 'H'))
				{
					bOk = ReadFloat(Reader, Part, Blade.Width);
				}
				else if (Part.Tag == SWG_IFF_TAG('O', 'P', 'E', 'N'))
				{
					bOk = ReadFloat(Reader, Part, Blade.OpenSeconds);
				}
				else if (Part.Tag == SWG_IFF_TAG('C', 'L', 'O', 'S'))
				{
					bOk = ReadFloat(Reader, Part, Blade.CloseSeconds);
				}
				if (!bOk)
				{
					return false;
				}
			}
			OutSaber.Blades.Add(MoveTemp(Blade));
		}
	}

	return !OutSaber.Blades.IsEmpty() && !OutSaber.HiltAppearancePath.IsEmpty();
}
