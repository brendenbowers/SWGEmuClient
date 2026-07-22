#include "Subsystems/SWGTerrainSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "TRE/SWGWorldSnapshotReader.h"
#include "TRE/SWGFormTagMapping.h"
#include "TRE/SWGDDSTextureLoader.h"
#include "TRE/SWGShaderReader.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeDataAccess.h"
#include "Engine/Texture2D.h"
#include "Engine/DataTable.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SceneComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// Landscape's uint16 height packing represents a fixed +/-256 *local* height
	// range (LANDSCAPE_ZSCALE is hardcoded regardless of actor Z scale); the
	// actor's Z scale stretches that into final world units. Baked heights must
	// be pre-divided by this same constant before packing (see PackHeightMip) —
	// SpawnLandscapeActor uses it directly as the actor's Z scale.
	constexpr float HeightZScale = 8.0f; // +/-2048 world units of representable height
	// Terrain layers are detail textures, not a single decal for an entire
	// 2km baked tile. Raw world coordinates keep adjacent tiles in phase.
	constexpr float TerrainTextureRepeatWorldSize = 8.0f;

	// Per-2x2-quad {Min,Max,Average} stats for one mip level — see
	// LandscapeComponent.h:503-512 (MipToMipMaxDeltas) for what these feed into.
	struct FSWGQuadHeightInfo
	{
		float Min = 0.0f;
		float Max = 0.0f;
		float Average = 0.0f;
	};

	// MipHeights[0] = the base Resolution x Resolution grid; MipHeights[L] is
	// (Resolution >> L) x (Resolution >> L), built via plain 2x2 box downsampling
	// (equivalent to bilinear at clean power-of-two boundaries). A full chain
	// down to 1x1 is required — confirmed via ULandscapeComponent::GetNumRelevantMips
	// (Landscape.cpp:2119-2127): a single mip fails check(NumRelevantMips > 0).
	void BuildMipHeightPyramid(const TArray<float>& BaseHeights, int32 Resolution, int32 NumMips, TArray<TArray<float>>& OutMipHeights)
	{
		OutMipHeights.SetNum(NumMips);
		OutMipHeights[0] = BaseHeights;

		for (int32 Mip = 1; Mip < NumMips; ++Mip)
		{
			const int32 SrcDim = Resolution >> (Mip - 1);
			const int32 DstDim = Resolution >> Mip;
			const TArray<float>& Src = OutMipHeights[Mip - 1];
			TArray<float>& Dst = OutMipHeights[Mip];
			Dst.SetNumUninitialized(DstDim * DstDim);

			for (int32 Y = 0; Y < DstDim; ++Y)
			{
				for (int32 X = 0; X < DstDim; ++X)
				{
					const int32 SX = X * 2, SY = Y * 2;
					const float H00 = Src[SY * SrcDim + SX];
					const float H10 = Src[SY * SrcDim + SX + 1];
					const float H01 = Src[(SY + 1) * SrcDim + SX];
					const float H11 = Src[(SY + 1) * SrcDim + SX + 1];
					Dst[Y * DstDim + X] = (H00 + H10 + H01 + H11) * 0.25f;
				}
			}
		}
	}

	// Non-overlapping 2x2 quad stats within one mip's own height grid — the same
	// grouping BuildMipHeightPyramid uses to produce the next mip down.
	void BuildQuadInfo(const TArray<float>& MipHeightGrid, int32 Dim, TArray<FSWGQuadHeightInfo>& OutQuads)
	{
		const int32 QuadDim = Dim / 2;
		OutQuads.SetNumUninitialized(QuadDim * QuadDim);

		for (int32 Y = 0; Y < QuadDim; ++Y)
		{
			for (int32 X = 0; X < QuadDim; ++X)
			{
				const int32 SX = X * 2, SY = Y * 2;
				const float H00 = MipHeightGrid[SY * Dim + SX];
				const float H10 = MipHeightGrid[SY * Dim + SX + 1];
				const float H01 = MipHeightGrid[(SY + 1) * Dim + SX];
				const float H11 = MipHeightGrid[(SY + 1) * Dim + SX + 1];

				FSWGQuadHeightInfo Info;
				Info.Min = FMath::Min(FMath::Min(H00, H10), FMath::Min(H01, H11));
				Info.Max = FMath::Max(FMath::Max(H00, H10), FMath::Max(H01, H11));
				Info.Average = (H00 + H10 + H01 + H11) * 0.25f;
				OutQuads[Y * QuadDim + X] = Info;
			}
		}
	}

	// Layout confirmed against UE::Landscape::Private::ComputeMipToMipMaxDeltas*
	// (LandscapeUtilsPrivate.cpp:33-63, not editor-gated): for NumRelevantMips
	// mips, mip M has (NumRelevantMips - 1 - M) entries (deltas to every mip
	// above it), laid out consecutively: mip 0's block, then mip 1's, etc.
	int32 CountForMip(int32 MipIndex, int32 NumRelevantMips)
	{
		return NumRelevantMips - 1 - MipIndex;
	}

	int32 OffsetForMip(int32 MipIndex, int32 NumRelevantMips)
	{
		int32 Offset = 0;
		for (int32 i = 0; i < MipIndex; ++i)
		{
			Offset += CountForMip(i, NumRelevantMips);
		}
		return Offset;
	}

	// Reimplements the editor-only delta algorithm (LandscapeEdit.cpp:172-329,
	// #if WITH_EDITOR — cannot be called directly, only replicated): for each
	// (SourceMip, DestMip) pair, the max delta is the worst-case gap between any
	// source quad's Min/Max and the average of the corresponding (coordinate-
	// halved) quad at DestMip.
	void ComputeMipToMipMaxDeltas(const TArray<TArray<float>>& MipHeights, int32 Resolution, int32 NumRelevantMips, TArray<double>& OutDeltas)
	{
		TArray<TArray<FSWGQuadHeightInfo>> MipQuads;
		MipQuads.SetNum(NumRelevantMips);
		for (int32 Mip = 0; Mip < NumRelevantMips; ++Mip)
		{
			BuildQuadInfo(MipHeights[Mip], Resolution >> Mip, MipQuads[Mip]);
		}

		const int32 TotalCount = OffsetForMip(NumRelevantMips - 1, NumRelevantMips) + CountForMip(NumRelevantMips - 1, NumRelevantMips);
		OutDeltas.SetNumZeroed(TotalCount);

		for (int32 SourceMip = 0; SourceMip < NumRelevantMips - 1; ++SourceMip)
		{
			const int32 SourceQuadDim = (Resolution >> SourceMip) / 2;
			const TArray<FSWGQuadHeightInfo>& SourceQuads = MipQuads[SourceMip];

			for (int32 DestMip = SourceMip + 1; DestMip < NumRelevantMips; ++DestMip)
			{
				const int32 Shift = DestMip - SourceMip;
				const int32 DestQuadDim = (Resolution >> DestMip) / 2;
				const TArray<FSWGQuadHeightInfo>& DestQuads = MipQuads[DestMip];

				double MaxDelta = 0.0;
				for (int32 QY = 0; QY < SourceQuadDim; ++QY)
				{
					for (int32 QX = 0; QX < SourceQuadDim; ++QX)
					{
						const int32 DQX = QX >> Shift;
						const int32 DQY = QY >> Shift;
						const FSWGQuadHeightInfo& SrcQ = SourceQuads[QY * SourceQuadDim + QX];
						const FSWGQuadHeightInfo& DstQ = DestQuads[DQY * DestQuadDim + DQX];

						const double D1 = FMath::Abs((double)SrcQ.Min - DstQ.Average);
						const double D2 = FMath::Abs((double)SrcQ.Max - DstQ.Average);
						MaxDelta = FMath::Max(MaxDelta, FMath::Max(D1, D2));
					}
				}

				const int32 Index = OffsetForMip(SourceMip, NumRelevantMips) + (DestMip - SourceMip - 1);
				OutDeltas[Index] = MaxDelta;
			}
		}
	}

	TArray<FColor> PackHeightMip(const TArray<float>& Heights, const FColor& DefaultNormal)
	{
		TArray<FColor> Packed;
		Packed.SetNumUninitialized(Heights.Num());
		for (int32 i = 0; i < Heights.Num(); ++i)
		{
			// GetTexHeight expects a *local* height (the fixed +/-256 range) —
			// divide the real-world baked height by the actor's Z scale to get
			// there, since GetTexHeight itself has no notion of our Z scale.
			FColor P = LandscapeDataAccess::PackHeight(LandscapeDataAccess::GetTexHeight(Heights[i] / HeightZScale));
			P.B = DefaultNormal.B;
			P.A = DefaultNormal.A;
			Packed[i] = P;
		}
		return Packed;
	}
}

void USWGTerrainSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	TreSubsystem = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));
	MeshGenerator = Cast<USWGMeshGeneratorSubsystem>(Collection.InitializeDependency(USWGMeshGeneratorSubsystem::StaticClass()));

	// Diagnostic: cross-validates GetHeightAt against the server's own
	// SERVERHEIGHTCHECK log at the same (x,y) samples.
	static FAutoConsoleCommand DumpHeightCompareCmd(
		TEXT("swg.DumpHeightCompare"),
		TEXT("Logs GetHeightAt(x,y) for a fixed list of server-sampled coordinates."),
		FConsoleCommandDelegate::CreateLambda([this]()
			{
				static const TArray<FVector2D> Samples = {
					{-1382.96f, -3749.1f}, {-1389.03f, -3757.89f}, {-1390.6f, -3780.95f},
					{-1472.42f, -3759.99f}, {-1454.45f, -3825.68f}, {-1436.44f, -3834.79f},
					{-1298.25f, -3449.17f}, {-1305.12f, -3442.03f}, {-1243.86f, -3701.65f},
					{-1279.19f, -3602.41f}, {-1285.16f, -3545.52f}, {-1287.28f, -3507.51f},
					{-1432.84f, -3479.05f}, {-1498.01f, -3702.77f}, {-1493.2f, -3610.32f},
					{-1454.26f, -3517.02f}, {-1453.66f, -3512.0f}, {-1332.11f, -3728.26f},
					{-1455.19f, -3510.15f}, {-1339.8f, -3722.93f}
				};

				for (const FVector2D& Sample : Samples)
				{
					const float Z = GetHeightAt(Sample.X, Sample.Y);
					UE_LOG(LogTemp, Warning, TEXT("HEIGHTCOMPARE x=%.2f y=%.2f clientHeight=%.4f"), Sample.X, Sample.Y, Z);
				}
			}));

	// Temporary diagnostic: investigating option 3 from chat history — the
	// SFAM layer names (e.g. "rock_cliff_anza", "tatt_sand_bumpy") are clearly
	// real SWG ground-texture identifiers, but the actual TRE virtual path
	// convention (directory/extension) is still unknown. Searches the whole
	// indexed virtual filesystem for any path containing the given substring.
	static FAutoConsoleCommand FindTexturePathCmd(
		TEXT("swg.FindVirtualPaths"),
		TEXT("swg.FindVirtualPaths <substring> — lists every indexed TRE virtual path containing this substring."),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Usage: swg.FindVirtualPaths <substring>"));
					return;
				}
				if (!TreSubsystem)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.FindVirtualPaths: no TreSubsystem"));
					return;
				}

				TArray<FString> Matches = TreSubsystem->FindVirtualPaths(Args[0]);
				UE_LOG(LogTemp, Warning, TEXT("swg.FindVirtualPaths: %d match(es) for '%s'"), Matches.Num(), *Args[0]);
				for (const FString& Match : Matches)
				{
					UE_LOG(LogTemp, Warning, TEXT("  %s"), *Match);
				}
			}));

	// Temporary diagnostic: investigating option 3 from chat history (authentic
	// SWG terrain shaders/textures, not a flat placeholder) — dumps the .trn's
	// FORM SGRP > SFAM chunk list (Core3's ShadersGroup/ShaderFamily) to see
	// what "fileName" actually looks like on disk (expected: a reference to
	// SWG's own shader-template asset, which itself should point at real
	// texture(s) — this determines whether real terrain texturing is a
	// reasonably-scoped follow-up or a much bigger asset-resolution project).
	static FAutoConsoleCommand DumpShaderFamiliesCmd(
		TEXT("swg.DumpShaderFamilies"),
		TEXT("Dumps terrain/tatooine.trn's FORM SGRP shader family table (familyId, name, fileName, color)."),
		FConsoleCommandDelegate::CreateLambda([this]()
			{
				if (!TreSubsystem)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpShaderFamilies: no TreSubsystem"));
					return;
				}

				FSWGIffReader Reader = TreSubsystem->CreateIffReader(TEXT("terrain/tatooine.trn"));
				if (!Reader.IsValid())
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpShaderFamilies: failed to open terrain/tatooine.trn"));
					return;
				}

				FSWGIffChunk SgrpForm;
				if (!Reader.FindForm(SWG_IFF_TAG('S', 'G', 'R', 'P'), SgrpForm))
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpShaderFamilies: no SGRP form found"));
					return;
				}

				TArray<FSWGIffChunk> VersionForms = Reader.ReadChildren(SgrpForm);
				if (VersionForms.Num() == 0 || !VersionForms[0].IsForm())
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpShaderFamilies: SGRP has no version form"));
					return;
				}

				UE_LOG(LogTemp, Warning, TEXT("swg.DumpShaderFamilies: SGRP version form '%s'"), *VersionForms[0].FormType.ToString());

				auto ReadNullTerminated = [](const uint8* D, int32 Size, int32& Offset) -> FString
				{
					const int32 Start = Offset;
					while (Offset < Size && D[Offset] != 0) { ++Offset; }
					FString Result = FString::ConstructFromPtrSize((const ANSICHAR*)(D + Start), Offset - Start);
					++Offset; // skip null terminator
					return Result;
				};

				int32 Count = 0;
				for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForms[0]))
				{
					if (Child.IsForm() || Child.Tag != SWG_IFF_TAG('S', 'F', 'A', 'M'))
					{
						continue;
					}

					const uint8* D = Reader.GetChunkData(Child);
					const int32 Size = Reader.GetChunkSize(Child);
					int32 Offset = 0;

					if (Offset + 4 > Size) continue;
					const int32 FamilyId = (int32)(D[Offset] | (D[Offset + 1] << 8) | (D[Offset + 2] << 16) | (D[Offset + 3] << 24));
					Offset += 4;

					const FString FamilyName = ReadNullTerminated(D, Size, Offset);
					const FString FileName = ReadNullTerminated(D, Size, Offset);

					if (Offset + 3 > Size) continue;
					const uint8 R = D[Offset], G = D[Offset + 1], B = D[Offset + 2];
					Offset += 3;

					auto ReadFloat = [](const uint8* Bytes, int32 Off) -> float
					{
						uint32 Bits = Bytes[Off] | (Bytes[Off + 1] << 8) | (Bytes[Off + 2] << 16) | (Bytes[Off + 3] << 24);
						float Result;
						FMemory::Memcpy(&Result, &Bits, sizeof(float));
						return Result;
					};

					float Var7 = 0.0f, Weight = 0.0f;
					int32 NumLayers = 0;
					if (Offset + 8 <= Size)
					{
						Var7 = ReadFloat(D, Offset); Offset += 4;
						Weight = ReadFloat(D, Offset); Offset += 4;
					}
					if (Offset + 4 <= Size)
					{
						NumLayers = (int32)(D[Offset] | (D[Offset + 1] << 8) | (D[Offset + 2] << 16) | (D[Offset + 3] << 24));
						Offset += 4;
					}

					UE_LOG(LogTemp, Warning, TEXT("SFAM[%d] familyId=%d name='%s' fileName='%s' color=(%d,%d,%d) var7=%.4f weight=%.4f numLayers=%d"),
						Count, FamilyId, *FamilyName, *FileName, R, G, B, Var7, Weight, NumLayers);

					for (int32 L = 0; L < NumLayers && Offset < Size; ++L)
					{
						const FString LayerName = ReadNullTerminated(D, Size, Offset);
						float LayerWeight = 0.0f;
						if (Offset + 4 <= Size)
						{
							LayerWeight = ReadFloat(D, Offset);
							Offset += 4;
						}
						UE_LOG(LogTemp, Warning, TEXT("  layer[%d] name='%s' weight=%.4f"), L, *LayerName, LayerWeight);
					}
					++Count;
				}

				UE_LOG(LogTemp, Warning, TEXT("swg.DumpShaderFamilies: %d shader famil(y/ies) total"), Count);
			}));

	// Temporary diagnostic: generic recursive IFF tree dumper, to inspect a
	// shader-template .iff's structure (e.g. abstract/terrain_surface/grass.iff)
	// and find exactly which key holds the real texture asset reference.
	static FAutoConsoleCommand DumpIffTreeCmd(
		TEXT("swg.DumpIffTree"),
		TEXT("swg.DumpIffTree <virtual path> — dumps the full FORM/chunk tree, including leaf chunk contents as printable strings where possible."),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Usage: swg.DumpIffTree <virtual path>"));
					return;
				}

				if (!TreSubsystem)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpIffTree: no TreSubsystem"));
					return;
				}

				FSWGIffReader Reader = TreSubsystem->CreateIffReader(*Args[0]);
				if (!Reader.IsValid())
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpIffTree: failed to open %s"), *Args[0]);
					return;
				}

				TFunction<void(const FSWGIffChunk&, int32)> DumpRecursive = [&Reader, &DumpRecursive](const FSWGIffChunk& Chunk, int32 Depth)
				{
					const FString Indent = FString::ChrN(Depth * 2, ' ');

					if (Chunk.IsForm())
					{
						UE_LOG(LogTemp, Warning, TEXT("%sFORM %s (%d bytes)"), *Indent, *Chunk.FormType.ToString(), Chunk.DataSize);
						for (const FSWGIffChunk& Child : Reader.ReadChildren(Chunk))
						{
							DumpRecursive(Child, Depth + 1);
						}
					}
					else
					{
						const uint8* D = Reader.GetChunkData(Chunk);
						const int32 Size = Reader.GetChunkSize(Chunk);

						// XXXX chunks are key/flag/[typed-value] (same layout
						// FindXxxxStringValue already knows about for object
						// templates) — decode the key always, and show the
						// remainder as hex when it's not a plain string, since
						// the generic printable-guess below bails out on any
						// chunk that mixes a readable key with binary value
						// bytes (which is most of them).
						if (Chunk.Tag == SWG_IFF_TAG('X', 'X', 'X', 'X'))
						{
							int32 KeyEnd = 0;
							while (KeyEnd < Size && D[KeyEnd] != 0) { ++KeyEnd; }
							const FString Key = FString::ConstructFromPtrSize((const ANSICHAR*)D, KeyEnd);
							const int32 FlagOffset = KeyEnd + 1;
							const uint8 Flag = (FlagOffset < Size) ? D[FlagOffset] : 0;

							FString ValueHex;
							for (int32 i = FlagOffset + 1; i < Size; ++i)
							{
								ValueHex += FString::Printf(TEXT("%02X "), D[i]);
							}

							// Try it as a string too, in case the value itself is one.
							FString AsString;
							if (FlagOffset + 1 < Size)
							{
								int32 ValEnd = FlagOffset + 1;
								while (ValEnd < Size && D[ValEnd] != 0) { ++ValEnd; }
								bool bAllPrintable = ValEnd > FlagOffset + 1;
								for (int32 i = FlagOffset + 1; i < ValEnd && bAllPrintable; ++i)
								{
									if (D[i] < 0x20 || D[i] > 0x7E) bAllPrintable = false;
								}
								if (bAllPrintable)
								{
									AsString = FString::ConstructFromPtrSize((const ANSICHAR*)(D + FlagOffset + 1), ValEnd - FlagOffset - 1);
								}
							}

							UE_LOG(LogTemp, Warning, TEXT("%sXXXX key='%s' flag=%d valueHex=[%s] asString='%s'"),
								*Indent, *Key, Flag, *ValueHex, *AsString);
							return;
						}

						// Best-effort printable preview — most leaf chunks in these
						// template files are short ASCII/UTF8 strings or small
						// numeric blobs; show whichever this looks like.
						bool bPrintable = Size > 0 && Size <= 256;
						if (bPrintable)
						{
							for (int32 i = 0; i < Size; ++i)
							{
								if (D[i] != 0 && (D[i] < 0x20 || D[i] > 0x7E)) { bPrintable = false; break; }
							}
						}

						if (bPrintable)
						{
							FString Preview = FString::ConstructFromPtrSize((const ANSICHAR*)D, Size);
							UE_LOG(LogTemp, Warning, TEXT("%sCHUNK %s (%d bytes): '%s'"), *Indent, *Chunk.Tag.ToString(), Size, *Preview);
						}
						else
						{
							FString Hex;
							for (int32 i = 0; i < FMath::Min(Size, 64); ++i)
							{
								Hex += FString::Printf(TEXT("%02X "), D[i]);
							}
							UE_LOG(LogTemp, Warning, TEXT("%sCHUNK %s (%d bytes, binary): %s"), *Indent, *Chunk.Tag.ToString(), Size, *Hex);
						}
					}
				};

				for (const FSWGIffChunk& Top : Reader.ReadChunks())
				{
					DumpRecursive(Top, 0);
				}
			}));

	static FAutoConsoleCommand TraceHeightCmd(
		TEXT("swg.TraceHeight"),
		TEXT("swg.TraceHeight <x> <y> — logs every layer that changes height at that coordinate."),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
			{
				if (Args.Num() < 2)
				{
					UE_LOG(LogTemp, Warning, TEXT("Usage: swg.TraceHeight <x> <y>"));
					return;
				}

				const float X = FCString::Atof(*Args[0]);
				const float Y = FCString::Atof(*Args[1]);

				FSWGTerrainEvaluator::SetDebugTraceTarget(X, Y, true);
				const float Z = GetHeightAt(X, Y);
				FSWGTerrainEvaluator::SetDebugTraceTarget(0.0f, 0.0f, false);

				UE_LOG(LogTemp, Warning, TEXT("HEIGHTTRACE final x=%.2f y=%.2f height=%.4f"), X, Y, Z);
			}));

	// Diagnostic: isolates whether the runtime DDS->UTexture2D bridge produces a
	// renderable texture, decoupled from mesh-generator/shader-parsing logic —
	// loads one texture and displays it on a plane in front of the player.
	static FAutoConsoleCommand TestDDSTextureCmd(
		TEXT("swg.TestDDSTexture"),
		TEXT("swg.TestDDSTexture <texture virtual path> — loads a .dds directly and displays it on a plane in front of the player."),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Usage: swg.TestDDSTexture <texture virtual path>"));
					return;
				}

				if (!TreSubsystem || !TreSubsystem->FileExists(Args[0]))
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: %s not found in TRE"), *Args[0]);
					return;
				}

				const TArray<uint8> Bytes = TreSubsystem->ExtractFile(Args[0]);
				UTexture2D* Texture = FSWGDDSTextureLoader::LoadTexture2D(Bytes, FName(*Args[0]), /*bSRGB=*/true);
				if (!Texture)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: FSWGDDSTextureLoader failed for %s"), *Args[0]);
					return;
				}

				UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: %s decoded — SizeX=%d SizeY=%d PixelFormat=%d SRGB=%d HasResource=%d"),
					*Args[0], Texture->GetSizeX(), Texture->GetSizeY(), (int32)Texture->GetPixelFormat(),
					Texture->SRGB ? 1 : 0, Texture->GetResource() != nullptr ? 1 : 0);

				UWorld* World = GetWorld();
				APawn* Pawn = World ? World->GetFirstPlayerController()->GetPawn() : nullptr;
				if (!World || !Pawn)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: no world/pawn to spawn the test plane near"));
					return;
				}

				UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGObjectTextured.M_SWGObjectTextured"));
				if (!Parent)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: M_SWGObjectTextured not found"));
					return;
				}
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
				MID->SetTextureParameterValue(TEXT("Diffuse"), Texture);

				UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
				if (!PlaneMesh)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: /Engine/BasicShapes/Plane not found"));
					return;
				}

				const FVector SpawnLocation = Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 300.0f + FVector(0, 0, 100.0f);
				const FRotator SpawnRotation = (-Pawn->GetActorForwardVector()).Rotation() + FRotator(90.0f, 0.0f, 0.0f);

				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AStaticMeshActor* PlaneActor = World->SpawnActor<AStaticMeshActor>(SpawnLocation, SpawnRotation, SpawnParams);
				if (!PlaneActor)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: failed to spawn test plane actor"));
					return;
				}

				UStaticMeshComponent* MeshComponent = PlaneActor->GetStaticMeshComponent();
				MeshComponent->SetMobility(EComponentMobility::Movable);
				MeshComponent->SetStaticMesh(PlaneMesh);
				MeshComponent->SetWorldScale3D(FVector(3.0f, 3.0f, 1.0f));
				MeshComponent->SetMaterial(0, MID);

				UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: spawned test plane at %s"), *SpawnLocation.ToString());
			}));
}

void USWGTerrainSubsystem::Deinitialize()
{
}

void USWGTerrainSubsystem::BeginLoadTerrain(const FString TerrainVirtualPath, const FVector& SpawnPosition)
{
	Async(EAsyncExecution::Thread, [this, TerrainVirtualPath, SpawnPosition]()
		{
			LoadTerrain(TerrainVirtualPath, SpawnPosition);
		});
}

void USWGTerrainSubsystem::Error(const FString& ErrorMessage)
{
	if (!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [this, ErrorMessage]()
			{
				Error(ErrorMessage);
			});
		return;
	}
	else
	{
		OnTerrainError.Broadcast(ErrorMessage);
	}
}

void USWGTerrainSubsystem::LoadTerrain(const FString& TerrainVirtualPath, const FVector& SpawnPosition)
{
	UE_LOG(LogTemp, Verbose, TEXT("USWGTerrainSubsystem: Begin loading terrain: %s"), *TerrainVirtualPath);

	FSWGTerrainData TerrainData;
	if (!ParseTerrain(TerrainVirtualPath, TerrainData))
	{
		Error(FString::Printf(TEXT("Failed to parse terrain: %s"), *TerrainVirtualPath));
		return;
	}

	const int32 ComponentVerts = HeightmapResolution;
	const float ComponentExtent = HeightmapWorldExtent; // world size of one component
	const float Spacing = ComponentExtent / (ComponentVerts - 1);
	const float GridExtent = ComponentExtent * ComponentGridSize;

	// Grid's min corner, not its center — SpawnPosition sits in the middle of the whole grid.
	const FVector GridOrigin(SpawnPosition.X - GridExtent * 0.5f, SpawnPosition.Y - GridExtent * 0.5f, 0.0f);

	TArray<FSWGBakedHeightmap> Grid;
	Grid.SetNum(ComponentGridSize * ComponentGridSize);

	for (int32 GridY = 0; GridY < ComponentGridSize; ++GridY)
	{
		for (int32 GridX = 0; GridX < ComponentGridSize; ++GridX)
		{
			const FVector RegionOrigin(GridOrigin.X + GridX * ComponentExtent, GridOrigin.Y + GridY * ComponentExtent, 0.0f);
			FSWGBakedHeightmap& Heightmap = Grid[GridY * ComponentGridSize + GridX];

			if (!FindCachedHeightmap(TerrainVirtualPath, RegionOrigin, Heightmap))
			{
				Heightmap = BakeHeightmap(TerrainData, RegionOrigin);
				BakeShaderWeights(TerrainData, Heightmap);
			}
		}
	}

	TArray<FSWGWorldSnapshotSpawnInfo> SnapshotObjects = LoadWorldSnapshotObjects(TerrainVirtualPath, SpawnPosition);

	// SpawnLandscapeGrid touches actors/components/textures — all game-thread-only
	// — but LoadTerrain itself runs on the background thread BeginLoadTerrain
	// dispatched onto. Marshal back before touching any of that. Caching
	// TerrainData here too (rather than back on the background thread) avoids a
	// write/read race with GetHeightAt, which is only ever called from the game thread.
	AsyncTask(ENamedThreads::GameThread, [this, TerrainVirtualPath, Grid = MoveTemp(Grid), GridOrigin, Spacing, TerrainData = MoveTemp(TerrainData), SnapshotObjects = MoveTemp(SnapshotObjects)]()
		{
			CachedTerrainData = TerrainData;
			bTerrainDataCached = true;

			SetupPlanetLighting(TerrainVirtualPath);
			SpawnDynamicMeshTerrainGrid(Grid, GridOrigin, Spacing);
			SpawnWorldSnapshotObjects(SnapshotObjects);
			OnTerrainReady.Broadcast();
		});
}

void USWGTerrainSubsystem::SetupPlanetLighting(const FString& TerrainVirtualPath)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Terrain reloads are allowed during zone travel. Remove only the actors we
	// created, leaving level-authored lighting untouched.
	static const FName PlanetLightingTag(TEXT("SWGPlanetLighting"));
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(PlanetLightingTag))
		{
			It->Destroy();
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ADirectionalLight* Sun = nullptr;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		Sun = *It;
		break;
	}
	if (!Sun)
	{
		Sun = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-38.0f, -35.0f, 0.0f), SpawnParams);
		if (Sun)
		{
			Sun->Tags.Add(PlanetLightingTag);
		}
	}
	if (Sun)
	{
		Sun->SetActorRotation(FRotator(-38.0f, -35.0f, 0.0f));
		UDirectionalLightComponent* SunComponent = Sun->GetComponent();
		SunComponent->SetMobility(EComponentMobility::Movable);
		SunComponent->SetAtmosphereSunLight(true);
		SunComponent->SetUseTemperature(true);
		SunComponent->SetTemperature(5600.0f);
		SunComponent->SetIntensity(3.0f);
	}

	ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (SkyLight)
	{
		SkyLight->Tags.Add(PlanetLightingTag);
		USkyLightComponent* SkyLightComponent = SkyLight->GetLightComponent();
		SkyLightComponent->SetMobility(EComponentMobility::Movable);
		SkyLightComponent->SetIntensity(1.0f);
		SkyLightComponent->SetRealTimeCaptureEnabled(true);
	}

	// A SkyAtmosphere is the UE equivalent of SWG's gradient-sky backdrop. The
	// selected Naboo texture is retained as the planet's data source while the
	// original effect's proprietary shader is still being ported.
	AActor* AtmosphereActor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (AtmosphereActor)
	{
		AtmosphereActor->Tags.Add(PlanetLightingTag);
		USceneComponent* Root = NewObject<USceneComponent>(AtmosphereActor, TEXT("SWGSkyRoot"));
		AtmosphereActor->SetRootComponent(Root);
		Root->RegisterComponent();
		USkyAtmosphereComponent* Atmosphere = NewObject<USkyAtmosphereComponent>(AtmosphereActor, TEXT("SWGSkyAtmosphere"));
		Atmosphere->SetupAttachment(Root);
		Atmosphere->RegisterComponent();
	}

	AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (Fog)
	{
		Fog->Tags.Add(PlanetLightingTag);
		Fog->GetComponent()->SetFogDensity(0.0015f);
		Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(0.60f, 0.72f, 0.78f));
	}

	// TODO: fix this so it determines based on what is in the trn/ws file
	const FString ZoneName = FPaths::GetBaseFilename(TerrainVirtualPath).ToLower();
	const FString GradientPath = ZoneName == TEXT("naboo")
		? TEXT("texture/grad_sky_nboo.dds")
		: FString::Printf(TEXT("texture/grad_sky_%s.dds"), *ZoneName.Left(4));
	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: %s outdoor lighting active (SWG gradient source: %s; server sun direction not currently present in scene messages)"),
		*ZoneName, *GradientPath);
}

TArray<FSWGWorldSnapshotSpawnInfo> USWGTerrainSubsystem::LoadWorldSnapshotObjects(const FString& TerrainVirtualPath, const FVector& SpawnPosition)
{
	TArray<FSWGWorldSnapshotSpawnInfo> Result;

	// "terrain/tatooine.trn" -> "tatooine" -> "snapshot/tatooine.ws".
	FString ZoneName = FPaths::GetBaseFilename(TerrainVirtualPath);
	const FString SnapshotPath = FString::Printf(TEXT("snapshot/%s.ws"), *ZoneName);

	FSWGIffReader SnapshotReader = TreSubsystem->CreateIffReader(SnapshotPath);
	if (!SnapshotReader.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: no world snapshot found at %s — no static world objects will spawn"), *SnapshotPath);
		return Result;
	}

	FSWGWorldSnapshotData SnapshotData;
	if (!FSWGWorldSnapshotReader::ReadWorldSnapshot(SnapshotReader, SnapshotData))
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to parse world snapshot %s"), *SnapshotPath);
		return Result;
	}

	if (!FormTagMappingTable)
	{
		// Loadable off the game thread — SWGInitializationState's own CRC map
		// generation already does exactly this from a background ThreadPool task.
		FormTagMappingTable = LoadObject<UDataTable>(nullptr,
			TEXT("/Game/SWGEmu/Data/DT_SWGFormTagMappings.DT_SWGFormTagMappings"));
	}

	const float RadiusSq = WorldSnapshotSpawnRadius * WorldSnapshotSpawnRadius;
	int32 InRangeCount = 0;

	for (const FSWGWorldSnapshotNode& Node : SnapshotData.Nodes)
	{
		if (FVector::DistSquared(Node.Position, SpawnPosition) > RadiusSq)
			continue;

		++InRangeCount;

		if (!SnapshotData.ObjectTemplateNames.IsValidIndex((int32)Node.NameID))
			continue;

		const FString& TemplateName = SnapshotData.ObjectTemplateNames[(int32)Node.NameID];

		FSWGIffReader TemplateReader = TreSubsystem->CreateIffReader(TemplateName);
		if (!TemplateReader.IsValid())
			continue;

		const FName FormType = TemplateReader.GetRootFormType();
		if (FormType == NAME_None || !FormTagMappingTable)
			continue;

		const FSWGFormTagMapping* Mapping = FormTagMappingTable->FindRow<FSWGFormTagMapping>(FormType, TEXT("USWGTerrainSubsystem"), false);
		if (!Mapping || !Mapping->ActorClass)
			continue;

		FSWGWorldSnapshotSpawnInfo Info;
		Info.ActorClass = Mapping->ActorClass;
		Info.Position = Node.Position;
		Info.Rotation = Node.Direction;
		Info.TemplateName = TemplateName;
		Result.Add(MoveTemp(Info));
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: world snapshot %s — %d/%d node(s) within %.0f units of spawn, %d resolved to an actor class"),
		*SnapshotPath, InRangeCount, SnapshotData.Nodes.Num(), WorldSnapshotSpawnRadius, Result.Num());

	return Result;
}

void USWGTerrainSubsystem::SpawnWorldSnapshotObjects(const TArray<FSWGWorldSnapshotSpawnInfo>& Objects)
{
	UWorld* World = GetWorld();
	if (!World)
		return;

	for (const FSWGWorldSnapshotSpawnInfo& Info : Objects)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		// Info.Position is raw/native space (straight from the .ws file, compared
		// against WorldSnapshotSpawnRadius in that same raw space in
		// LoadWorldSnapshotObjects) — scale to final UE space right at this
		// actor-placement boundary.
		const FTransform SpawnTransform(Info.Rotation, SWGToUnrealSpace(Info.Position));
		AActor* Actor = World->SpawnActor<AActor>(Info.ActorClass, SpawnTransform, SpawnParams);

		if (!Actor)
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: failed to spawn world snapshot object %s"), *Info.TemplateName);
			continue;
		}

		if (MeshGenerator)
		{
			MeshGenerator->RequestMeshForTemplatePath(Actor, Info.TemplateName);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: spawned %d world snapshot object(s)"), Objects.Num());
}

float USWGTerrainSubsystem::GetHeightAt(float X, float Y) const
{
	if (!bTerrainDataCached)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: GetHeightAt called before any terrain has parsed"));
		return 0.0f;
	}
	return FSWGTerrainEvaluator::GetHeight(CachedTerrainData, X, Y);
}

bool USWGTerrainSubsystem::FindCachedHeightmap(const FString& TerrainVirtualPath, const FVector& RegionOrigin, FSWGBakedHeightmap& OutHeightmap)
{
	return false;
}

bool USWGTerrainSubsystem::ParseTerrain(const FString& TerrainVirtualPath, FSWGTerrainData& OutTerrainData)
{
	FSWGIffReader Reader = TreSubsystem->CreateIffReader(TerrainVirtualPath);
	if (!Reader.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: Failed to create IFF reader for terrain: %s"), *TerrainVirtualPath);
		return false;
	}

	if (!FSWGTerrainReader::ReadTerrain(Reader, OutTerrainData))
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: Failed to read terrain data from IFF: %s"), *TerrainVirtualPath);
		return false;
	}

	return true;
}

FSWGBakedHeightmap USWGTerrainSubsystem::BakeHeightmap(const FSWGTerrainData& TerrainData, const FVector& RegionOrigin)
{
	const int32 Resolution = HeightmapResolution;
	const float Spacing = HeightmapWorldExtent / (Resolution - 1);

	FSWGBakedHeightmap Heightmap;
	Heightmap.Origin = RegionOrigin;
	Heightmap.Spacing = Spacing;
	Heightmap.Heights.SetNumUninitialized(Resolution * Resolution);

	// Tracks min/max/out-of-range height values from this component's evaluator
	// calls — catches GetHeight diverging to huge values, which GetTexHeight's
	// Clamp would otherwise silently flatten to the +/-2048 ceiling/floor.
	float MinHeight = TNumericLimits<float>::Max();
	float MaxHeight = TNumericLimits<float>::Lowest();
	int32 OutOfRangeCount = 0;
	constexpr float RepresentableHeightLimit = 2048.0f;

	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		const float WorldY = RegionOrigin.Y + Row * Spacing;

		for (int32 Col = 0; Col < Resolution; ++Col)
		{
			const float WorldX = RegionOrigin.X + Col * Spacing;
			float Height = FSWGTerrainEvaluator::GetHeight(TerrainData, WorldX, WorldY);
			// Safety net: a NaN/Inf height here bakes straight into the landscape's
			// uint16 heightmap and comes out as an extreme spike (this is what was
			// actually happening — see FSWGMapFractal::GetNoise's Pow() fix for the
			// specific cause found). Guard at the source too, since other affector
			// math could in principle produce the same failure mode.
			if (!FMath::IsFinite(Height))
			{
				UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: non-finite height at (%f, %f) — clamping to 0"), WorldX, WorldY);
				Height = 0.0f;
			}

			if (FMath::Abs(Height) > RepresentableHeightLimit)
			{
				++OutOfRangeCount;
			}
			MinHeight = FMath::Min(MinHeight, Height);
			MaxHeight = FMath::Max(MaxHeight, Height);

			Heightmap.Heights[Row * Resolution + Col] = Height;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: BakeHeightmap region origin=(%.1f,%.1f) min=%.1f max=%.1f outOfRange(+/-%.0f)=%d/%d"),
		RegionOrigin.X, RegionOrigin.Y, MinHeight, MaxHeight, RepresentableHeightLimit, OutOfRangeCount, Resolution * Resolution);

	return Heightmap;
}

void USWGTerrainSubsystem::BakeShaderWeights(const FSWGTerrainData& TerrainData, FSWGBakedHeightmap& Heightmap)
{
	const int32 Resolution = HeightmapResolution;
	const int32 SampleCount = Resolution * Resolution;

	// One pass, keeping every vertex's full weight map around — re-evaluating
	// the layer tree a second time (rather than storing this) would double
	// the bake cost for no real memory win at this resolution (16384 samples).
	TArray<TMap<int32, float>> PerVertexWeights;
	PerVertexWeights.SetNum(SampleCount);

	TMap<int32, float> TotalWeightByFamily;

	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		const float WorldY = Heightmap.Origin.Y + Row * Heightmap.Spacing;
		for (int32 Col = 0; Col < Resolution; ++Col)
		{
			const float WorldX = Heightmap.Origin.X + Col * Heightmap.Spacing;
			TMap<int32, float>& Weights = PerVertexWeights[Row * Resolution + Col];
			FSWGTerrainEvaluator::GetShaderWeights(TerrainData, WorldX, WorldY, Weights);

			for (const TPair<int32, float>& Pair : Weights)
			{
				TotalWeightByFamily.FindOrAdd(Pair.Key) += Pair.Value;
			}
		}
	}

	TArray<TPair<int32, float>> SortedFamilies;
	for (const TPair<int32, float>& Pair : TotalWeightByFamily)
	{
		if (Pair.Value > 0.0f)
		{
			SortedFamilies.Add(Pair);
		}
	}
	SortedFamilies.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B) { return A.Value > B.Value; });

	constexpr int32 MaxLayers = 4;
	// One baked tile covers roughly 2km, while roads, plazas, and other hard
	// surface details may occupy only a small part of it. Keep the families
	// that actually paint the tile centre (where this one-tile grid spawns the
	// player) before using the whole-tile totals for the remaining slots.
	const int32 CentreIndex = (Resolution / 2) * Resolution + (Resolution / 2);
	TArray<TPair<int32, float>> CentreFamilies;
	for (const TPair<int32, float>& Pair : PerVertexWeights[CentreIndex])
	{
		if (Pair.Value > 0.0f)
		{
			CentreFamilies.Add(Pair);
		}
	}
	CentreFamilies.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B) { return A.Value > B.Value; });
	for (const TPair<int32, float>& Pair : CentreFamilies)
	{
		if (Heightmap.ChosenShaderFamilyIds.Num() >= MaxLayers) break;
		Heightmap.ChosenShaderFamilyIds.AddUnique(Pair.Key);
	}
	for (int32 i = 0; i < FMath::Min(SortedFamilies.Num(), MaxLayers); ++i)
	{
		if (Heightmap.ChosenShaderFamilyIds.Num() >= MaxLayers) break;
		Heightmap.ChosenShaderFamilyIds.AddUnique(SortedFamilies[i].Key);
	}

	if (Heightmap.ChosenShaderFamilyIds.Num() == 0)
	{
		// No shader affector ever painted anything at this tile — leave empty,
		// BuildTerrainTileMaterial falls back to the plain default material.
		return;
	}

	Heightmap.ShaderWeightColors.SetNumZeroed(SampleCount);
	for (int32 i = 0; i < SampleCount; ++i)
	{
		const TMap<int32, float>& Weights = PerVertexWeights[i];
		FVector3f Color(0.0f, 0.0f, 0.0f);
		for (int32 Channel = 1; Channel < Heightmap.ChosenShaderFamilyIds.Num(); ++Channel)
		{
			const float* Weight = Weights.Find(Heightmap.ChosenShaderFamilyIds[Channel]);
			Color.Component(Channel - 1) = Weight ? FMath::Clamp(*Weight, 0.0f, 1.0f) : 0.0f;
		}
		Heightmap.ShaderWeightColors[i] = Color;
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: BakeShaderWeights region origin=(%.1f,%.1f) chosen %d family(ies): %s"),
		Heightmap.Origin.X, Heightmap.Origin.Y, Heightmap.ChosenShaderFamilyIds.Num(),
		*FString::JoinBy(Heightmap.ChosenShaderFamilyIds, TEXT(","), [](int32 Id) { return FString::FromInt(Id); }));
}

UTexture2D* USWGTerrainSubsystem::GetOrLoadShaderTexture(const FString& LayerName, bool bNormalMap)
{
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *LayerName, bNormalMap ? TEXT("normal") : TEXT("diffuse"));
	if (TObjectPtr<UTexture2D>* Existing = LoadedShaderTextures.Find(CacheKey))
	{
		return *Existing;
	}

	// Cache the miss too (as nullptr) so a bad/missing texture doesn't retry
	// (and re-log) on every tile that happens to reference the same family.
	UTexture2D* Result = nullptr;

	FString VirtualPath;
	const FString ShaderPath = FString::Printf(TEXT("shader/%s.sht"), *LayerName);
	if (TreSubsystem && TreSubsystem->FileExists(ShaderPath))
	{
		FSWGShaderData ShaderData;
		if (FSWGShaderReader::ReadShader(TreSubsystem->CreateIffReader(ShaderPath), ShaderData))
		{
			const ESWGShaderTextureUsage Usage = bNormalMap
				? ESWGShaderTextureUsage::Normal
				: ESWGShaderTextureUsage::Diffuse;
			if (const FSWGShaderTexture* Texture = ShaderData.FindTexture(Usage))
			{
				VirtualPath = Texture->VirtualPath;
			}
		}
	}

	// Older terrain families without a .sht use the direct texture convention.
	if (VirtualPath.IsEmpty())
	{
		VirtualPath = FString::Printf(TEXT("texture/%s%s.dds"), *LayerName, bNormalMap ? TEXT("_n") : TEXT(""));
	}
	if (TreSubsystem && TreSubsystem->FileExists(VirtualPath))
	{
		const TArray<uint8> Bytes = TreSubsystem->ExtractFile(VirtualPath);
		Result = FSWGDDSTextureLoader::LoadTexture2D(Bytes, FName(*VirtualPath), /*bSRGB=*/!bNormalMap);
		if (Result)
		{
			// Terrain UVs use repeating world-space coordinates. Runtime DDS
			// textures are configured to wrap by FSWGDDSTextureLoader before its
			// initial resource creation.
			Result->AddressX = TA_Wrap;
			Result->AddressY = TA_Wrap;
		}
	}

	if (!Result)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: failed to load shader texture '%s'"), *VirtualPath);
	}

	LoadedShaderTextures.Add(CacheKey, Result);
	return Result;
}

UMaterialInterface* USWGTerrainSubsystem::BuildTerrainTileMaterial(const FSWGBakedHeightmap& Heightmap)
{
	if (Heightmap.ChosenShaderFamilyIds.Num() == 0)
	{
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	if (!TerrainBlendMaterial)
	{
		TerrainBlendMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/SWGEmu/Materials/M_SWGTerrainBlend.M_SWGTerrainBlend"));
	}

	if (!TerrainBlendMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: M_SWGTerrainBlend not found — falling back to plain default material"));
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(TerrainBlendMaterial, this);

	static const FName LayerParamNames[4] = { TEXT("Layer0"), TEXT("Layer1"), TEXT("Layer2"), TEXT("Layer3") };
	static const FName NormalParamNames[4] = { TEXT("Layer0Normal"), TEXT("Layer1Normal"), TEXT("Layer2Normal"), TEXT("Layer3Normal") };

	for (int32 Channel = 0; Channel < Heightmap.ChosenShaderFamilyIds.Num(); ++Channel)
	{
		const FSWGShaderFamily* Family = CachedTerrainData.FindShaderFamily(Heightmap.ChosenShaderFamilyIds[Channel]);
		if (!Family || Family->LayerNames.Num() == 0)
		{
			continue;
		}
		UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: terrain material slot %d uses family %d '%s', layer '%s'"),
			Channel, Heightmap.ChosenShaderFamilyIds[Channel], *Family->Name, *Family->LayerNames[0]);

		if (UTexture2D* Texture = GetOrLoadShaderTexture(Family->LayerNames[0]))
		{
			MID->SetTextureParameterValue(LayerParamNames[Channel], Texture);
		}
		if (UTexture2D* NormalTexture = GetOrLoadShaderTexture(Family->LayerNames[0], /*bNormalMap=*/true))
		{
			MID->SetTextureParameterValue(NormalParamNames[Channel], NormalTexture);
		}
	}

	return MID;
}

ALandscape* USWGTerrainSubsystem::SpawnLandscapeActor(const FVector& GridOrigin, float Spacing)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: no valid world to spawn landscape in"));
		return nullptr;
	}

	// A single subsection per component — SubsectionSizeQuads+1 must be a power
	// of two (LandscapeComponent.h:449); HeightmapResolution=128 samples gives
	// exactly that (127 quads). ComponentSizeQuads/SubsectionSizeQuads/NumSubsections
	// are shared by every component the grid adds to this actor.
	const int32 ComponentVerts = HeightmapResolution;
	const int32 SubsectionSizeQuads = ComponentVerts - 1;
	const int32 NumSubsections = 1;
	const int32 ComponentSizeQuads = NumSubsections * SubsectionSizeQuads;

	// XY: each quad spans Spacing world units. Z: HeightZScale (see its own
	// comment) — packed heights (via LandscapeDataAccess::GetTexHeight, the
	// exact inverse of GetLocalHeight) reconstruct in world-height units once
	// scaled by this, since GetTexHeight's own range is a fixed +/-256 *local*
	// units regardless of our chosen Z scale.
	const FVector DesiredScale(Spacing, Spacing, HeightZScale);

	// ALandscapeProxy's constructor unconditionally sets its own RootComponent
	// scale (128,128,256), which doesn't compose predictably with a pre-divided
	// spawn-transform scale. Spawn at identity scale instead, then force the
	// exact scale afterward; Static mobility blocks SetActorScale3D, so flip
	// mobility around the call (a one-time setup step, not a runtime move).
	const FTransform SpawnTransform(FQuat::Identity, GridOrigin, FVector::OneVector);
	ALandscape* Landscape = Cast<ALandscape>(World->SpawnActor(ALandscape::StaticClass(), &SpawnTransform));
	if (!Landscape)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to spawn ALandscape"));
		return nullptr;
	}

	USceneComponent* LandscapeRoot = Landscape->GetRootComponent();
	const EComponentMobility::Type OriginalMobility = LandscapeRoot->Mobility;
	LandscapeRoot->SetMobility(EComponentMobility::Movable);
	Landscape->SetActorRelativeScale3D(DesiredScale);
	LandscapeRoot->SetMobility(OriginalMobility);

	Landscape->ComponentSizeQuads = ComponentSizeQuads;
	Landscape->SubsectionSizeQuads = SubsectionSizeQuads;
	Landscape->NumSubsections = NumSubsections;

	// Placeholder so the landscape isn't simply invisible — a real terrain
	// material (and the shader/flora work it implies) is explicitly deferred,
	// see world-object-plan.html "Shader/flora — no server-side ground truth".
	Landscape->LandscapeMaterial = UMaterial::GetDefaultMaterial(MD_Surface);

	return Landscape;
}

void USWGTerrainSubsystem::AddLandscapeComponent(ALandscape* Landscape, const FSWGBakedHeightmap& Heightmap, const FIntPoint& SectionBase)
{
	if (Heightmap.Heights.Num() != HeightmapResolution * HeightmapResolution)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: heightmap has %d samples, expected %d — refusing to add component"),
			Heightmap.Heights.Num(), HeightmapResolution * HeightmapResolution);
		return;
	}

	const int32 ComponentVerts = HeightmapResolution;
	const int32 SubsectionSizeQuads = ComponentVerts - 1;
	const int32 NumSubsections = 1;
	const int32 ComponentSizeQuads = NumSubsections * SubsectionSizeQuads;

	ULandscapeComponent* Component = NewObject<ULandscapeComponent>(Landscape, NAME_None, RF_Transactional);
	Component->Init(SectionBase.X, SectionBase.Y, ComponentSizeQuads, NumSubsections, SubsectionSizeQuads);

	// Confirmed via ULandscapeComponent::GetNumRelevantMips (Landscape.cpp:2119-2127):
	// NumRelevantMips = NumTextureMips - (NumSubsections > 1 ? 2 : 1), and it must be
	// > 0 or the component asserts/crashes — a single mip (what we shipped originally)
	// fails this outright. Build the full chain down to 1x1.
	const int32 NumTextureMips = FMath::CeilLogTwo(ComponentVerts) + 1; // 128 -> 8 (128,64,...,2,1)
	const int32 NumRelevantMips = (NumSubsections > 1) ? (NumTextureMips - 2) : (NumTextureMips - 1);

	TArray<TArray<float>> MipHeights;
	BuildMipHeightPyramid(Heightmap.Heights, ComponentVerts, NumTextureMips, MipHeights);

	// Real normals derived from the heightmap are a follow-up — a flat default
	// (matching GetDefaultPackedHeightColor's convention) is enough to get this rendering.
	const FColor DefaultNormal = LandscapeDataAccess::GetDefaultPackedHeightColor();

	// Mip 0 goes through CreateTransient; additional mips are appended the same
	// way CreateTransient builds its own (new FTexture2DMipMap + BulkData
	// lock/realloc/unlock). Name must be unique per component, or every
	// component in the grid resolves to (and overwrites) the same transient object.
	const TArray<FColor> Mip0Pixels = PackHeightMip(MipHeights[0], DefaultNormal);
	const FName TextureName(*FString::Printf(TEXT("SWGTerrainHeightmap_%d_%d"), SectionBase.X, SectionBase.Y));
	UTexture2D* HeightmapTexture = UTexture2D::CreateTransient(
		ComponentVerts, ComponentVerts, PF_B8G8R8A8, TextureName,
		TConstArrayView64<uint8>(reinterpret_cast<const uint8*>(Mip0Pixels.GetData()), Mip0Pixels.Num() * sizeof(FColor)));

	if (!HeightmapTexture)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to create heightmap texture"));
		return;
	}

#if WITH_EDITOR
	// Editor-only: ULandscapeTextureHash::GetHash asserts if Source is
	// unpopulated when no hash asset user data is attached (ours never has any),
	// and CreateTransient only builds PlatformData, never Source. Only mip 0
	// needs populating since CalculateTextureHash64 only reads mip 0.
	HeightmapTexture->Source.Init(ComponentVerts, ComponentVerts, /*NewNumSlices=*/1, /*NewNumMips=*/1, TSF_BGRA8,
		reinterpret_cast<const uint8*>(Mip0Pixels.GetData()));
#endif

	for (int32 Mip = 1; Mip < NumTextureMips; ++Mip)
	{
		const int32 MipDim = ComponentVerts >> Mip;
		const TArray<FColor> MipPixels = PackHeightMip(MipHeights[Mip], DefaultNormal);
		const int64 MipBytes = (int64)MipPixels.Num() * sizeof(FColor);

		FTexture2DMipMap* MipMap = new FTexture2DMipMap(MipDim, MipDim, 1);
		HeightmapTexture->GetPlatformData()->Mips.Add(MipMap);
		MipMap->BulkData.Lock(LOCK_READ_WRITE);
		void* DestData = MipMap->BulkData.Realloc(MipBytes);
		FMemory::Memcpy(DestData, MipPixels.GetData(), MipBytes);
		MipMap->BulkData.Unlock();
	}

	HeightmapTexture->UpdateResource();

	Component->SetHeightmap(HeightmapTexture);

	// Single dedicated texture per component (no shared atlas) — confirmed formula
	// from LandscapeEdit.cpp's own (editor-only) single-component-texture case.
	Component->HeightmapScaleBias = FVector4(1.0f / (float)ComponentVerts, 1.0f / (float)ComponentVerts, 0.0f, 0.0f);

	// Reimplements the editor-only MipToMipMaxDeltas computation (see helper
	// comments above) — required for Landscape's LOD morphing; leaving this empty
	// is what caused the "-93 into an array of size 1" crash.
	TArray<double> MipToMipMaxDeltas;
	ComputeMipToMipMaxDeltas(MipHeights, ComponentVerts, NumRelevantMips, MipToMipMaxDeltas);
	Component->MipToMipMaxDeltas = MoveTemp(MipToMipMaxDeltas);

	// One entry per relevant LOD (FLandscapeComponentSceneProxy asserts
	// LODIndexToMaterialIndex.Num() == MaxLOD + 1, LandscapeRender.cpp:1480, where
	// MaxLOD = CeilLogTwo(SubsectionSizeQuads + 1) - 1 == NumRelevantMips - 1 for
	// our single-subsection case). We only ever assign one material, so every LOD
	// points at slot 0.
	Component->LODIndexToMaterialIndex.Init(0, NumRelevantMips);

	Component->RegisterComponent();

	// Collision is explicitly deferred (see world-object-plan.html "Collision-data
	// research pass") — this landscape renders but is not yet walkable.
	if (UE_LOG_ACTIVE(LogTemp, Log))
	{
		FString OriginStr = Heightmap.Origin.ToString();
		UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: added component at %s (SectionBase %d,%d) with %d mips (no collision yet)"),
			*OriginStr, SectionBase.X, SectionBase.Y, NumTextureMips);
	}
}

void USWGTerrainSubsystem::SpawnLandscapeGrid(const TArray<FSWGBakedHeightmap>& Grid, const FVector& GridOrigin, float Spacing)
{
	check(IsInGameThread());

	ALandscape* Landscape = SpawnLandscapeActor(GridOrigin, Spacing);
	if (!Landscape)
	{
		return;
	}

	const int32 ComponentSizeQuads = HeightmapResolution - 1;

	for (int32 GridY = 0; GridY < ComponentGridSize; ++GridY)
	{
		for (int32 GridX = 0; GridX < ComponentGridSize; ++GridX)
		{
			const FSWGBakedHeightmap& Heightmap = Grid[GridY * ComponentGridSize + GridX];
			AddLandscapeComponent(Landscape, Heightmap, FIntPoint(GridX * ComponentSizeQuads, GridY * ComponentSizeQuads));
		}
	}

	FString Name = TEXT("UNKNOWN");
	if (ULevel* Level = Landscape->GetLevel())
	{
		Name = Level->GetName();
	}
	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: spawned %dx%d landscape grid in level: %s"), ComponentGridSize, ComponentGridSize, *Name);
}

void USWGTerrainSubsystem::SpawnDynamicMeshTerrainGrid(const TArray<FSWGBakedHeightmap>& Grid, const FVector& GridOrigin, float Spacing)
{
	using namespace UE::Geometry;

	check(IsInGameThread());

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: no valid world to spawn terrain mesh in"));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* TerrainActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FQuat::Identity, GridOrigin), SpawnParams);
	if (!TerrainActor)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to spawn terrain mesh actor"));
		return;
	}

	// A plain AActor has no root component at construction time, so SpawnActor's
	// given transform (GridOrigin) has nothing to store itself in and silently
	// no-ops. Set the location explicitly once the root actually exists.
	USceneComponent* TerrainRoot = NewObject<USceneComponent>(TerrainActor, TEXT("TerrainRoot"));
	TerrainActor->SetRootComponent(TerrainRoot);
	TerrainRoot->RegisterComponent();
	// GridOrigin is raw/native space (matches the .trn's own units, same as
	// every position vertex placement below is computed against) — scale to
	// final UE space right at this actor-placement boundary.
	TerrainActor->SetActorLocation(SWGToUnrealSpace(GridOrigin));

	const int32 Resolution = HeightmapResolution;

	for (int32 TileIndex = 0; TileIndex < Grid.Num(); ++TileIndex)
	{
		const FSWGBakedHeightmap& Heightmap = Grid[TileIndex];
		if (Heightmap.Heights.Num() != Resolution * Resolution)
		{
			UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: terrain tile %d has %d samples, expected %d — skipping"),
				TileIndex, Heightmap.Heights.Num(), Resolution * Resolution);
			continue;
		}

		// Relative to the actor (GridOrigin), same coordinate this tile's own
		// heights were baked in world-space against — no encoding/scale
		// indirection at all, just a direct offset.
		const FVector LocalOrigin = Heightmap.Origin - GridOrigin;
		// Dynamic-mesh UVs reach the GPU in a compact representation. Keeping
		// absolute SWG world coordinates here loses fractional precision once a
		// player is far from (0,0), turning detail textures into noisy mips.
		// Rebase within this tile, but retain the tile origin's modulo so adjacent
		// tiles still meet at the same world-space texture phase.
		const FVector2f TerrainUVOrigin(
			FMath::Fmod(Heightmap.Origin.X, TerrainTextureRepeatWorldSize) / TerrainTextureRepeatWorldSize,
			FMath::Fmod(Heightmap.Origin.Y, TerrainTextureRepeatWorldSize) / TerrainTextureRepeatWorldSize);

		UDynamicMeshComponent* MeshComponent = NewObject<UDynamicMeshComponent>(TerrainActor, NAME_None, RF_Transactional);
		MeshComponent->SetupAttachment(TerrainRoot);

		const bool bHasShaderWeights = Heightmap.ShaderWeightColors.Num() == Resolution * Resolution;

		MeshComponent->EditMesh([&Heightmap, Resolution, LocalOrigin, TerrainUVOrigin, bHasShaderWeights](FDynamicMesh3& EditMesh)
		{
			EditMesh.EnableAttributes();
			FDynamicMeshNormalOverlay* Normals = EditMesh.Attributes()->PrimaryNormals();
			FDynamicMeshUVOverlay* UVs = EditMesh.Attributes()->PrimaryUV();

			// Vertex colors carry this tile's shader-family blend weights (R/G/B
			// = family[1]/[2]/[3]'s paint weight; family[0]'s weight is implicit,
			// 1-R-G-B, computed in the material) — see BakeShaderWeights.
			if (bHasShaderWeights)
			{
				// UDynamicMeshComponent's VertexColor material node consumes the
				// primary color overlay, not FDynamicMesh3's legacy color buffer.
				EditMesh.Attributes()->EnablePrimaryColors();
			}
			FDynamicMeshColorOverlay* Colors = bHasShaderWeights ? EditMesh.Attributes()->PrimaryColors() : nullptr;

			TArray<int32> VertexIds, NormalIds, UVIds, ColorIds;
			VertexIds.SetNumUninitialized(Resolution * Resolution);
			NormalIds.SetNumUninitialized(Resolution * Resolution);
			UVIds.SetNumUninitialized(Resolution * Resolution);
			if (bHasShaderWeights) ColorIds.SetNumUninitialized(Resolution * Resolution);

			for (int32 Row = 0; Row < Resolution; ++Row)
			{
				for (int32 Col = 0; Col < Resolution; ++Col)
				{
					const int32 Idx = Row * Resolution + Col;
					// Everything feeding this (LocalOrigin, Spacing, baked
					// Heights) is raw/native space, matching the .trn's own
					// units — SWGWorldScale converts to final UE units right
					// here, at the actual vertex-placement boundary.
					const FVector3d Pos = FVector3d(
						LocalOrigin.X + Col * Heightmap.Spacing,
						LocalOrigin.Y + Row * Heightmap.Spacing,
						Heightmap.Heights[Idx]) * SWGWorldScale;

					VertexIds[Idx] = EditMesh.AppendVertex(Pos);
					NormalIds[Idx] = Normals->AppendElement(FVector3f(0, 0, 1));
					UVIds[Idx] = UVs->AppendElement(FVector2f(
						TerrainUVOrigin.X + Col * Heightmap.Spacing / TerrainTextureRepeatWorldSize,
						TerrainUVOrigin.Y + Row * Heightmap.Spacing / TerrainTextureRepeatWorldSize));

					if (bHasShaderWeights)
					{
						const FVector3f Weight = Heightmap.ShaderWeightColors[Idx];
						ColorIds[Idx] = Colors->AppendElement(FVector4f(Weight.X, Weight.Y, Weight.Z, 1.0f));
					}
				}
			}

			for (int32 Row = 0; Row < Resolution - 1; ++Row)
			{
				for (int32 Col = 0; Col < Resolution - 1; ++Col)
				{
					const int32 I00 = Row * Resolution + Col;
					const int32 I10 = Row * Resolution + (Col + 1);
					const int32 I01 = (Row + 1) * Resolution + Col;
					const int32 I11 = (Row + 1) * Resolution + (Col + 1);

					const int32 TriA = EditMesh.AppendTriangle(VertexIds[I00], VertexIds[I01], VertexIds[I10]);
					if (TriA >= 0)
					{
						Normals->SetTriangle(TriA, FIndex3i(NormalIds[I00], NormalIds[I01], NormalIds[I10]));
						UVs->SetTriangle(TriA, FIndex3i(UVIds[I00], UVIds[I01], UVIds[I10]));
						if (bHasShaderWeights) Colors->SetTriangle(TriA, FIndex3i(ColorIds[I00], ColorIds[I01], ColorIds[I10]));
					}

					const int32 TriB = EditMesh.AppendTriangle(VertexIds[I10], VertexIds[I01], VertexIds[I11]);
					if (TriB >= 0)
					{
						Normals->SetTriangle(TriB, FIndex3i(NormalIds[I10], NormalIds[I01], NormalIds[I11]));
						UVs->SetTriangle(TriB, FIndex3i(UVIds[I10], UVIds[I01], UVIds[I11]));
						if (bHasShaderWeights) Colors->SetTriangle(TriB, FIndex3i(ColorIds[I10], ColorIds[I01], ColorIds[I11]));
					}
				}
			}
		});

		MeshComponent->SetMaterial(0, BuildTerrainTileMaterial(Heightmap));
		// Deliberately NOT calling SetColorOverrideMode(VertexColors) — any
		// non-None mode makes the scene proxy force-substitute the engine's
		// vertex-color debug material regardless of what's assigned. Vertex
		// color still uploads to the GPU with ColorMode at its default None,
		// which is all M_SWGTerrainBlend's VertexColor node needs.

		// Terrain was previously non-collidable at all (see world-object-plan.html
		// "Collision-data research pass") — every character fell forever through
		// empty space before its mesh loaded, since there was nothing to land on
		// (worked around, not fixed, by forcing MOVE_Flying until a real position
		// update arrived). Use the terrain's own baked triangle mesh directly as
		// its collision shape (complex-as-simple) — it's already a heightfield,
		// there's no cheaper "simple" approximation worth building separately.
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionObjectType(ECC_WorldStatic);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
		MeshComponent->RegisterComponent();
		MeshComponent->EnableComplexAsSimpleCollision();
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: spawned %d dynamic mesh terrain tile(s) at origin %s"), Grid.Num(), *GridOrigin.ToString());
}
