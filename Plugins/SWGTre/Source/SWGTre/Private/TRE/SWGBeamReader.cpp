#include "TRE/SWGBeamReader.h"

#include "TRE/SWGFxCommon.h"
#include "TRE/SWGIFFChunkReader.h"

bool FSWGBeamReader::Read(const FSWGIffReader& Reader, FSWGBeam& OutBeam)
{
	OutBeam = FSWGBeam();

	FSWGIffChunk BeamForm, DataChunk;
	if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('B', 'E', 'A', 'M'), BeamForm)
		|| !Reader.FindChildChunk(BeamForm, SWG_IFF_TAG('0', '0', '0', '0'), DataChunk))
	{
		return false;
	}

	FSWGIFFChunkReader Chunk(DataChunk, Reader);
	if (!Chunk.ReadValueLE(OutBeam.Length) || !Chunk.ReadValueLE(OutBeam.Width)
		|| !Chunk.ReadValueLE(OutBeam.Color.R) || !Chunk.ReadValueLE(OutBeam.Color.G)
		|| !Chunk.ReadValueLE(OutBeam.Color.B) || !Chunk.ReadValueLE(OutBeam.Color.A)
		|| !Chunk.ReadTerminiatedString(OutBeam.ShaderPath) || !Chunk.AtEnd())
	{
		return false;
	}
	FSWGFxCommonReader::NormalizePath(OutBeam.ShaderPath);
	return !OutBeam.ShaderPath.IsEmpty();
}
