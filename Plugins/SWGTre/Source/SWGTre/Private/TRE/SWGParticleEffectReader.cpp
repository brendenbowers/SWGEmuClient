#include "TRE/SWGParticleEffectReader.h"

#include "TRE/SWGIFFChunkReader.h"

namespace
{
	constexpr FSWGIffTag TagForm0000 = SWG_IFF_TAG('0', '0', '0', '0');
	constexpr FSWGIffTag TagPtim = SWG_IFF_TAG('P', 'T', 'I', 'M');
	constexpr FSWGIffTag TagWvfm = SWG_IFF_TAG('W', 'V', 'F', 'M');
	constexpr FSWGIffTag TagClrr = SWG_IFF_TAG('C', 'L', 'R', 'R');
	constexpr FSWGIffTag TagPtex = SWG_IFF_TAG('P', 'T', 'E', 'X');
	constexpr FSWGIffTag TagPtcl = SWG_IFF_TAG('P', 'T', 'C', 'L');
	constexpr FSWGIffTag TagPtqd = SWG_IFF_TAG('P', 'T', 'Q', 'D');
	constexpr FSWGIffTag TagPtmh = SWG_IFF_TAG('P', 'T', 'M', 'H');
	constexpr FSWGIffTag TagPatt = SWG_IFF_TAG('P', 'A', 'T', 'T');
	constexpr FSWGIffTag TagEmgp = SWG_IFF_TAG('E', 'M', 'G', 'P');
	constexpr FSWGIffTag TagEmtr = SWG_IFF_TAG('E', 'M', 'T', 'R');

	void SetError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}

	int32 VersionOf(const FSWGIffChunk& VersionForm)
	{
		return FCString::Atoi(*VersionForm.FormType.ToString());
	}

	template<typename TValue>
	TValue LoadAt(const TArray<uint8>& Bytes, int32 Offset)
	{
		TValue Value{};
		if (Offset >= 0 && Offset + (int32)sizeof(TValue) <= Bytes.Num())
		{
			FMemory::Memcpy(&Value, Bytes.GetData() + Offset, sizeof(TValue));
		}
		return Value;
	}

	/** Fixed bytes after the sound string, per EMTR version. */
	int32 ParamsTailSize(int32 Version)
	{
		switch (Version)
		{
			case 9: return 39;
			case 10: return 68;
			case 11: return 72;
			case 12: return 97;
			case 13: return 98;
			case 14: return 99;
			default: return -1;
		}
	}

	constexpr int32 ParamsHeaderSize = 48;
	constexpr int32 ParamsSizeV0006 = 34;

	bool ReadParams(const FSWGIffReader& Reader, const FSWGIffChunk& ParamsChunk, int32 Version, FSWGEmitterParams& Out, FString* OutError)
	{
		Out = FSWGEmitterParams();
		Out.Version = Version;

		FSWGIFFChunkReader Cursor(ParamsChunk, Reader);
		TArray<uint8> Rest;
		if (!Cursor.ReadTerminiatedString(Out.Name) || !Cursor.ReadRemainingBytes(Rest))
		{
			SetError(OutError, TEXT("params: no name"));
			return false;
		}

		if (Version == 0)
		{
			Out.RawHeader = Rest;
			return true;
		}

		if (Version == 6)
		{
			if (Rest.Num() != ParamsSizeV0006)
			{
				SetError(OutError, FString::Printf(TEXT("params 0006: %d bytes after name, expected %d"), Rest.Num(), ParamsSizeV0006));
				return false;
			}
			Out.RawHeader = Rest;
			Out.EmissionStyle = LoadAt<int32>(Rest, 0);
			Out.Shape = LoadAt<int32>(Rest, 4);
			Out.bIgnoreLiveParticles = Rest[8] != 0;
			Out.RunTimeMin = LoadAt<float>(Rest, 9);
			Out.RunTimeMax = LoadAt<float>(Rest, 13);
			Out.bBurst = Rest[17] != 0;
			Out.bRandomInitialRotation = Rest[18] != 0;
			Out.Orientation = LoadAt<int32>(Rest, 19);
			Out.Always1A = Rest[23];
			Out.Always1B = Rest[24];
			return true;
		}

		const int32 TailSize = ParamsTailSize(Version);
		if (TailSize < 0)
		{
			SetError(OutError, FString::Printf(TEXT("params: unsupported EMTR version %d"), Version));
			return false;
		}

		int32 StringEnd = ParamsHeaderSize;
		while (StringEnd < Rest.Num() && Rest[StringEnd] != 0)
		{
			++StringEnd;
		}
		if (StringEnd >= Rest.Num() || Rest.Num() - (StringEnd + 1) != TailSize)
		{
			SetError(OutError, FString::Printf(TEXT("params %04d: %d bytes after name, expected %d + string + %d"), Version, Rest.Num(), ParamsHeaderSize, TailSize));
			return false;
		}

		Out.RawHeader.Append(Rest.GetData(), ParamsHeaderSize);
		Out.RawTail.Append(Rest.GetData() + StringEnd + 1, TailSize);
		Out.SoundPath = FString::ConstructFromPtrSize(reinterpret_cast<const ANSICHAR*>(Rest.GetData() + ParamsHeaderSize), StringEnd - ParamsHeaderSize);
		FSWGFxCommonReader::NormalizePath(Out.SoundPath);

		const TArray<uint8>& Header = Out.RawHeader;
		Out.EmissionStyle = LoadAt<int32>(Header, 0);
		Out.bGenerateByDistance = Header[4] != 0;
		Out.Shape = LoadAt<int32>(Header, 8);
		Out.bIgnoreLiveParticles = Header[12] != 0;
		Out.RunTimeMin = LoadAt<float>(Header, 13);
		Out.RunTimeMax = LoadAt<float>(Header, 17);
		Out.MaxParticles = LoadAt<float>(Header, 21);
		Out.bBurst = Header[25] != 0;
		Out.CountMin = LoadAt<int32>(Header, 26);
		Out.CountMax = LoadAt<int32>(Header, 30);
		Out.bRandomInitialRotation = Header[34] != 0;
		Out.Orientation = LoadAt<int32>(Header, 35);
		if (Out.Orientation == 3)
		{
			Out.Orientation = 2;
		}
		Out.Always1A = Header[39];
		Out.Always1B = Header[40];

		const TArray<uint8>& Tail = Out.RawTail;
		const bool bV12 = Version >= 12;
		Out.bLocalSpace = Tail[0] != 0;
		Out.bTerrainCollide = Tail[1] != 0;
		Out.bKillOnTerrainHit = Tail[2] != 0;
		if (bV12)
		{
			Out.GroundOffset = LoadAt<float>(Tail, 3);
		}

		const int32 FloatBase = bV12 ? 7 : 11;
		for (int32 FloatIndex = 0; FloatIndex < 5; ++FloatIndex)
		{
			Out.ParamFloats[FloatIndex] = LoadAt<float>(Tail, FloatBase + 4 * FloatIndex);
		}
		const int32 RangeBase = bV12 ? 27 : 31;
		Out.RangeMin = LoadAt<float>(Tail, RangeBase);
		Out.RangeMax = LoadAt<float>(Tail, RangeBase + 4);

		if (bV12)
		{
			Out.FlockMode = LoadAt<int32>(Tail, 35);
		}
		if (Version >= 10)
		{
			const int32 FlockBase = bV12 ? 39 : 40;
			for (int32 FloatIndex = 0; FloatIndex < 7; ++FloatIndex)
			{
				Out.FlockParams[FloatIndex] = LoadAt<float>(Tail, FlockBase + 4 * FloatIndex);
			}
		}
		if (Version == 11)
		{
			Out.Float0011 = LoadAt<float>(Tail, 68);
		}
		if (bV12)
		{
			Out.FlockBoundsShape = LoadAt<int32>(Tail, 67);
			Out.AmbientBlend = LoadAt<float>(Tail, 79);
			Out.bSnapToTerrain = Tail[83] != 0;
			Out.bTerrainZeroVelocity = Tail[84] != 0;
			Out.TerrainHeightOffset = LoadAt<float>(Tail, 85);
			Out.FlockTurnAngle = LoadAt<float>(Tail, 89);
			Out.Tail93 = LoadAt<float>(Tail, 93);
		}
		if (Version >= 13)
		{
			Out.bEmitOnStart = Tail[97] != 0;
		}
		if (Version >= 14)
		{
			Out.bLodDroppable = Tail[98] != 0;
		}
		return true;
	}

	bool ReadChild(const FSWGIffReader& Reader, const FSWGIffChunk& ChildForm, FSWGParticleChild& Out)
	{
		for (const FSWGIffChunk& Version : Reader.FindChildForms(ChildForm))
		{
			FSWGIffChunk Data;
			if (!Reader.FindChildChunk(Version, TagForm0000, Data))
			{
				continue;
			}
			FSWGIFFChunkReader Chunk(Data, Reader);
			uint8 Flag = 0;
			if (Chunk.ReadTerminiatedString(Out.Path) && Chunk.ReadValueLE(Out.ValueA) && Chunk.ReadValueLE(Out.ValueB)
				&& Chunk.ReadValueLE(Flag) && Chunk.ReadValueLE(Out.Mode) && Chunk.AtEnd())
			{
				Out.bFlag = Flag != 0;
				FSWGFxCommonReader::NormalizePath(Out.Path);
				return true;
			}
		}
		return false;
	}

	bool ReadParticle(const FSWGIffReader& Reader, const FSWGIffChunk& PtclForm, FSWGParticleDescription& Out, FString* OutError)
	{
		Out = FSWGParticleDescription();

		const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(PtclForm);
		if (VersionForms.IsEmpty())
		{
			SetError(OutError, TEXT("PTCL: no version form"));
			return false;
		}
		const FSWGIffChunk& VersionForm = VersionForms[0];
		Out.Version = VersionOf(VersionForm);

		TArray<FSWGWaveform> Curves;
		for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForm))
		{
			if (!Child.IsForm())
			{
				if (Child.Tag == TagForm0000)
				{
					FSWGIFFChunkReader Chunk(Child, Reader);
					if (!Chunk.ReadTerminiatedString(Out.Name))
					{
						SetError(OutError, TEXT("PTCL: bad name chunk"));
						return false;
					}
					Out.RawNameTail.Reset();
					Chunk.ReadRemainingBytes(Out.RawNameTail);
					Out.Flags[0] = Out.RawNameTail.IsValidIndex(0) ? Out.RawNameTail[0] : 0;
					Out.Flags[1] = Out.RawNameTail.IsValidIndex(1) ? Out.RawNameTail[1] : 0;
					Out.bRandomSpinDirection = Out.Flags[0] != 0;
				}
				continue;
			}

			if (Child.FormType == TagClrr)
			{
				if (!FSWGFxCommonReader::ReadColorRamp(Reader, Child, Out.Color))
				{
					SetError(OutError, TEXT("PTCL: bad CLRR"));
					return false;
				}
			}
			else if (Child.FormType == TagWvfm)
			{
				FSWGWaveform Curve;
				if (!FSWGFxCommonReader::ReadWaveform(Reader, Child, Curve))
				{
					SetError(OutError, TEXT("PTCL: bad WVFM"));
					return false;
				}
				Curves.Add(MoveTemp(Curve));
			}
			else if (Child.FormType == TagPatt)
			{
				if (!ReadChild(Reader, Child, Out.Child))
				{
					SetError(OutError, TEXT("PTCL: bad PATT"));
					return false;
				}
				Out.bHasChild = true;
			}
		}

		if (Out.Version == 2 || Out.Version == 3)
		{
			const int32 Expected = Out.Version == 2 ? 2 : 5;
			if (Curves.Num() != Expected)
			{
				SetError(OutError, FString::Printf(TEXT("PTCL %04d: %d curves, expected %d"), Out.Version, Curves.Num(), Expected));
				return false;
			}
			Out.Alpha = MoveTemp(Curves[0]);
			Out.Speed = MoveTemp(Curves[1]);
			for (int32 CurveIndex = 2; CurveIndex < Curves.Num(); ++CurveIndex)
			{
				Out.ExtraCurves.Add(MoveTemp(Curves[CurveIndex]));
			}
		}
		else
		{
			Out.ExtraCurves = MoveTemp(Curves);
		}
		return true;
	}

	void AddUnique(TArray<FString>& Paths, const FString& Path)
	{
		if (!Path.IsEmpty())
		{
			Paths.AddUnique(Path.ToLower());
		}
	}
}

bool FSWGParticleEffectReader::ReadEmitter(const FSWGIffReader& Reader, const FSWGIffChunk& EmitterForm, FSWGParticleEmitter& OutEmitter, FString* OutError)
{
	OutEmitter = FSWGParticleEmitter();

	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(EmitterForm);
	if (VersionForms.IsEmpty())
	{
		SetError(OutError, TEXT("EMTR: no version form"));
		return false;
	}
	const FSWGIffChunk& VersionForm = VersionForms[0];
	OutEmitter.Version = VersionOf(VersionForm);

	const int32 CurveCount = (int32)ESWGEmitterCurve::Count;
	int32 CurveIndex = 0;
	bool bHasParams = false;
	bool bHasShape = false;
	for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForm))
	{
		if (!Child.IsForm())
		{
			if (Child.Tag == TagForm0000 && !bHasParams)
			{
				if (!ReadParams(Reader, Child, OutEmitter.Version, OutEmitter.Params, OutError))
				{
					return false;
				}
				bHasParams = true;
			}
			continue;
		}

		if (Child.FormType == TagPtim)
		{
			if (!FSWGFxCommonReader::ReadTiming(Reader, Child, OutEmitter.Timing))
			{
				SetError(OutError, TEXT("EMTR: bad PTIM"));
				return false;
			}
		}
		else if (Child.FormType == TagWvfm)
		{
			if (CurveIndex >= CurveCount)
			{
				SetError(OutError, TEXT("EMTR: more than 16 curves"));
				return false;
			}
			if (!FSWGFxCommonReader::ReadWaveform(Reader, Child, OutEmitter.Curves[CurveIndex++]))
			{
				SetError(OutError, TEXT("EMTR: bad emitter WVFM"));
				return false;
			}
		}
		else if (Child.FormType == TagPtcl)
		{
			// The 0000 test file holds the particle directly, with no PTQD/PTMH around it.
			OutEmitter.Kind = ESWGParticleKind::Bare;
			bHasShape = true;
			if (!ReadParticle(Reader, Child, OutEmitter.Particle, OutError))
			{
				return false;
			}
		}
		else if (Child.FormType == TagPtqd || Child.FormType == TagPtmh)
		{
			bHasShape = true;
			const bool bQuad = Child.FormType == TagPtqd;
			OutEmitter.Kind = bQuad ? ESWGParticleKind::Quad : ESWGParticleKind::Mesh;
			bool bHasParticle = false;
			bool bHasTexture = false;
			for (const FSWGIffChunk& ShapeChild : Reader.ReadChildren(Child))
			{
				if (!ShapeChild.IsForm())
				{
					if (!bQuad && ShapeChild.Tag == TagForm0000)
					{
						FSWGIFFChunkReader Chunk(ShapeChild, Reader);
						if (!Chunk.ReadLenientString(OutEmitter.MeshPath))
						{
							SetError(OutError, TEXT("PTMH: bad mesh path"));
							return false;
						}
						FSWGFxCommonReader::NormalizePath(OutEmitter.MeshPath);
					}
					continue;
				}

				if (ShapeChild.FormType == TagPtcl)
				{
					if (!ReadParticle(Reader, ShapeChild, OutEmitter.Particle, OutError))
					{
						return false;
					}
					bHasParticle = true;
				}
				else if (ShapeChild.FormType == TagWvfm && !bQuad)
				{
					FSWGWaveform Curve;
					if (!FSWGFxCommonReader::ReadWaveform(Reader, ShapeChild, Curve))
					{
						SetError(OutError, TEXT("PTMH: bad WVFM"));
						return false;
					}
					OutEmitter.ShapeCurves.Add(MoveTemp(Curve));
				}
				else if (bQuad)
				{
					// PTQD body: FORM 0000|0001 { WVFM x3, PTEX, [0000: 1 byte] }.
					for (const FSWGIffChunk& BodyChild : Reader.ReadChildren(ShapeChild))
					{
						if (!BodyChild.IsForm())
						{
							if (BodyChild.Tag == TagForm0000 && BodyChild.DataSize == 1)
							{
								OutEmitter.bWidthFromLength = Reader.GetChunkData(BodyChild)[0] != 0;
							}
							continue;
						}

						if (BodyChild.FormType == TagWvfm)
						{
							FSWGWaveform Curve;
							if (!FSWGFxCommonReader::ReadWaveform(Reader, BodyChild, Curve))
							{
								SetError(OutError, TEXT("PTQD: bad WVFM"));
								return false;
							}
							OutEmitter.ShapeCurves.Add(MoveTemp(Curve));
						}
						else if (BodyChild.FormType == TagPtex)
						{
							if (!FSWGFxCommonReader::ReadTexture(Reader, BodyChild, OutEmitter.Texture))
							{
								SetError(OutError, TEXT("PTQD: bad PTEX"));
								return false;
							}
							bHasTexture = true;
						}
					}
				}
			}

			if (!bHasParticle || (bQuad && (!bHasTexture || OutEmitter.ShapeCurves.Num() != (int32)ESWGQuadCurve::Count))
				|| (!bQuad && (OutEmitter.ShapeCurves.Num() != (int32)ESWGMeshCurve::Count || OutEmitter.MeshPath.IsEmpty())))
			{
				SetError(OutError, bQuad ? TEXT("PTQD: missing PTCL, PTEX or 3 curves") : TEXT("PTMH: missing PTCL, mesh path or 4 curves"));
				return false;
			}
		}
	}

	if (CurveIndex != CurveCount || !bHasParams || !bHasShape)
	{
		SetError(OutError, FString::Printf(TEXT("EMTR %04d: %d curves, params=%d shape=%d"), OutEmitter.Version, CurveIndex, bHasParams, bHasShape));
		return false;
	}
	return true;
}

bool FSWGParticleEffectReader::Read(const FSWGIffReader& Reader, FSWGParticleEffect& OutEffect, FString* OutError)
{
	OutEffect = FSWGParticleEffect();

	FSWGIffChunk PeftForm;
	if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('P', 'E', 'F', 'T'), PeftForm))
	{
		SetError(OutError, TEXT("no PEFT form"));
		return false;
	}

	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(PeftForm);
	if (VersionForms.IsEmpty())
	{
		SetError(OutError, TEXT("PEFT: no version form"));
		return false;
	}
	const FSWGIffChunk& VersionForm = VersionForms[0];
	OutEffect.Version = VersionOf(VersionForm);
	if (OutEffect.Version > 2)
	{
		SetError(OutError, FString::Printf(TEXT("PEFT: unsupported version %d"), OutEffect.Version));
		return false;
	}

	int32 DeclaredGroups = -1;
	for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForm))
	{
		if (!Child.IsForm())
		{
			if (Child.Tag == TagForm0000)
			{
				FSWGIFFChunkReader Chunk(Child, Reader);
				bool bOk = Chunk.ReadValueLE(DeclaredGroups);
				if (bOk && OutEffect.Version == 2)
				{
					for (float& Value : OutEffect.Header0002)
					{
						bOk = bOk && Chunk.ReadValueLE(Value);
					}
				}
				if (!bOk || !Chunk.AtEnd())
				{
					SetError(OutError, TEXT("PEFT: bad 0000 chunk"));
					return false;
				}
			}
			continue;
		}

		if (Child.FormType == TagPtim)
		{
			if (!FSWGFxCommonReader::ReadTiming(Reader, Child, OutEffect.Timing))
			{
				SetError(OutError, TEXT("PEFT: bad PTIM"));
				return false;
			}
		}
		else if (Child.FormType == TagEmgp)
		{
			FSWGEmitterGroup Group;
			const TArray<FSWGIffChunk> GroupVersions = Reader.FindChildForms(Child);
			if (GroupVersions.IsEmpty())
			{
				SetError(OutError, TEXT("EMGP: no version form"));
				return false;
			}
			Group.Version = VersionOf(GroupVersions[0]);

			int32 DeclaredEmitters = -1;
			for (const FSWGIffChunk& GroupChild : Reader.ReadChildren(GroupVersions[0]))
			{
				if (!GroupChild.IsForm())
				{
					if (GroupChild.Tag == TagForm0000)
					{
						FSWGIFFChunkReader Chunk(GroupChild, Reader);
						if (!Chunk.ReadValueLE(DeclaredEmitters) || !Chunk.AtEnd())
						{
							SetError(OutError, TEXT("EMGP: bad 0000 chunk"));
							return false;
						}
					}
				}
				else if (GroupChild.FormType == TagPtim)
				{
					if (!FSWGFxCommonReader::ReadTiming(Reader, GroupChild, Group.Timing))
					{
						SetError(OutError, TEXT("EMGP: bad PTIM"));
						return false;
					}
				}
				else if (GroupChild.FormType == TagEmtr)
				{
					FSWGParticleEmitter Emitter;
					if (!ReadEmitter(Reader, GroupChild, Emitter, OutError))
					{
						return false;
					}
					Group.Emitters.Add(MoveTemp(Emitter));
				}
			}

			if (DeclaredEmitters != Group.Emitters.Num())
			{
				SetError(OutError, FString::Printf(TEXT("EMGP: declares %d emitters, holds %d"), DeclaredEmitters, Group.Emitters.Num()));
				return false;
			}
			OutEffect.Groups.Add(MoveTemp(Group));
		}
	}

	if (DeclaredGroups != OutEffect.Groups.Num())
	{
		SetError(OutError, FString::Printf(TEXT("PEFT: declares %d groups, holds %d"), DeclaredGroups, OutEffect.Groups.Num()));
		return false;
	}
	return true;
}

int32 FSWGParticleEffect::GetEmitterCount() const
{
	int32 Count = 0;
	for (const FSWGEmitterGroup& Group : Groups)
	{
		Count += Group.Emitters.Num();
	}
	return Count;
}

void FSWGParticleEffect::CollectReferences(TArray<FString>& OutShaders, TArray<FString>& OutMeshes, TArray<FString>& OutChildEffects, TArray<FString>& OutSounds) const
{
	for (const FSWGEmitterGroup& Group : Groups)
	{
		for (const FSWGParticleEmitter& Emitter : Group.Emitters)
		{
			AddUnique(OutSounds, Emitter.Params.SoundPath);
			AddUnique(OutMeshes, Emitter.MeshPath);
			if (Emitter.IsQuad())
			{
				AddUnique(OutShaders, Emitter.Texture.ShaderPath);
			}
			if (Emitter.Particle.bHasChild)
			{
				AddUnique(OutChildEffects, Emitter.Particle.Child.Path);
			}
		}
	}
}
