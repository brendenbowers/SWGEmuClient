// swg.CreateSpaceNebulaAssets - the Stage 2 gas-cloud assets: a tileable Perlin-Worley noise volume, the lit
// heterogeneous-volume material that samples it, and the close-range wisp sprite material.

#include "SWGSpaceNebulaBuilder.h"
#include "CoreMinimal.h"
#include "Async/ParallelFor.h"
#include "Engine/VolumeTexture.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionPerInstanceRandom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameterVolume.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

namespace
{
	constexpr int32 NoiseSize = 128;
	const TCHAR* NoisePackage = TEXT("/Game/SWGEmu/Textures/T_SWGNebulaNoise");

	uint32 HashCell(int32 CellX, int32 CellY, int32 CellZ, uint32 Salt)
	{
		uint32 Hash = uint32(CellX)*0x8da6b343u ^ uint32(CellY)*0xd8163841u ^ uint32(CellZ)*0xcb1ab31fu ^ Salt*0x9e3779b9u;
		Hash ^= Hash >> 13; Hash *= 0x85ebca6bu; Hash ^= Hash >> 16; Hash *= 0xc2b2ae35u; Hash ^= Hash >> 15;
		return Hash;
	}

	int32 WrapCell(int32 Cell, int32 Period) { return ((Cell % Period) + Period) % Period; }
	float Quintic(float Fraction) { return Fraction*Fraction*Fraction*(Fraction*(Fraction*6.f-15.f)+10.f); }

	const FVector3f EdgeGradients[12] = {
		{1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};

	/** Gradient noise that repeats every Period cells; Position is in cell units. Range about [-1, 1]. */
	float PerlinPeriodic(const FVector3f& Position, int32 Period)
	{
		const FIntVector Base(FMath::FloorToInt(Position.X), FMath::FloorToInt(Position.Y), FMath::FloorToInt(Position.Z));
		const FVector3f Fraction = Position - FVector3f(Base.X, Base.Y, Base.Z);
		const FVector3f Fade(Quintic(Fraction.X), Quintic(Fraction.Y), Quintic(Fraction.Z));
		float Corner[2][2][2];
		for (int32 OffsetZ = 0; OffsetZ < 2; ++OffsetZ)
			for (int32 OffsetY = 0; OffsetY < 2; ++OffsetY)
				for (int32 OffsetX = 0; OffsetX < 2; ++OffsetX)
				{
					const uint32 Hash = HashCell(WrapCell(Base.X+OffsetX,Period), WrapCell(Base.Y+OffsetY,Period), WrapCell(Base.Z+OffsetZ,Period), 1);
					Corner[OffsetZ][OffsetY][OffsetX] = FVector3f::DotProduct(EdgeGradients[Hash % 12], Fraction - FVector3f(OffsetX, OffsetY, OffsetZ));
				}
		float AlongY[2][2];
		for (int32 OffsetZ = 0; OffsetZ < 2; ++OffsetZ)
			for (int32 OffsetY = 0; OffsetY < 2; ++OffsetY)
				AlongY[OffsetZ][OffsetY] = FMath::Lerp(Corner[OffsetZ][OffsetY][0], Corner[OffsetZ][OffsetY][1], Fade.X);
		const float AlongZ0 = FMath::Lerp(AlongY[0][0], AlongY[0][1], Fade.Y);
		const float AlongZ1 = FMath::Lerp(AlongY[1][0], AlongY[1][1], Fade.Y);
		return FMath::Lerp(AlongZ0, AlongZ1, Fade.Z);
	}

	/** Inverted nearest-feature distance of a Worley lattice that repeats every Cells cells: 1 at a feature point, falling to 0. */
	float WorleyBillow(const FVector3f& UnitPosition, int32 Cells, uint32 Salt)
	{
		const FVector3f Scaled = UnitPosition * float(Cells);
		const FIntVector Base(FMath::FloorToInt(Scaled.X), FMath::FloorToInt(Scaled.Y), FMath::FloorToInt(Scaled.Z));
		float NearestSquared = 4.f;
		for (int32 OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
			for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
				for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
				{
					const int32 CellX = Base.X+OffsetX, CellY = Base.Y+OffsetY, CellZ = Base.Z+OffsetZ;
					const uint32 Hash = HashCell(WrapCell(CellX,Cells), WrapCell(CellY,Cells), WrapCell(CellZ,Cells), Salt);
					const FVector3f Feature(CellX + float(Hash & 1023)/1024.f, CellY + float((Hash >> 10) & 1023)/1024.f, CellZ + float((Hash >> 20) & 1023)/1024.f);
					NearestSquared = FMath::Min(NearestSquared, FVector3f::DistSquared(Feature, Scaled));
				}
		return FMath::Clamp(1.f - FMath::Sqrt(NearestSquared)*1.15f, 0.f, 1.f);
	}

	/** R: Perlin-Worley body shape. G, B, A: Worley billows at 8, 16 and 32 cells, for erosion. */
	FColor SampleNoise(int32 VoxelX, int32 VoxelY, int32 VoxelZ)
	{
		const FVector3f Unit((VoxelX+.5f)/NoiseSize, (VoxelY+.5f)/NoiseSize, (VoxelZ+.5f)/NoiseSize);
		float Perlin = 0, Weight = .5f, WeightSum = 0;
		for (int32 Octave = 0; Octave < 4; ++Octave)
		{
			const int32 Period = 4 << Octave;
			Perlin += PerlinPeriodic(Unit*float(Period), Period)*Weight; WeightSum += Weight; Weight *= .5f;
		}
		const float PerlinUnit = FMath::Clamp(Perlin/WeightSum*.5f+.5f, 0.f, 1.f);
		const float WorleyBody = WorleyBillow(Unit, 4, 11)*.625f + WorleyBillow(Unit, 8, 12)*.25f + WorleyBillow(Unit, 16, 13)*.125f;
		const float PerlinWorley = WorleyBody + PerlinUnit*(1.f-WorleyBody);
		auto ToByte = [](float Value) { return uint8(FMath::Clamp(FMath::RoundToInt(Value*255.f), 0, 255)); };
		return FColor(ToByte(PerlinWorley), ToByte(WorleyBillow(Unit, 8, 21)), ToByte(WorleyBillow(Unit, 16, 22)), ToByte(WorleyBillow(Unit, 32, 23)));
	}

	void SavePackageOf(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Asset, *FileName, Args);
		UE_LOG(LogTemp, Log, TEXT("swg.CreateSpaceNebulaAssets: saved %s"), *Package->GetName());
	}

	void BuildNoiseVolume(bool bRebuild)
	{
		const FString AssetPath = FString::Printf(TEXT("%s.T_SWGNebulaNoise"), NoisePackage);
		UVolumeTexture* Texture = LoadObject<UVolumeTexture>(nullptr, *AssetPath);
		if (Texture && !bRebuild)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.CreateSpaceNebulaAssets: T_SWGNebulaNoise already exists - pass 'rebuild' to regenerate"));
			return;
		}
		if (!Texture)
		{
			UPackage* Package = CreatePackage(NoisePackage);
			Texture = NewObject<UVolumeTexture>(Package, TEXT("T_SWGNebulaNoise"), RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Texture);
		}
		const double Started = FPlatformTime::Seconds();
		TArray<FColor> Voxels; Voxels.SetNumUninitialized(NoiseSize*NoiseSize*NoiseSize);
		ParallelFor(NoiseSize*NoiseSize, [&Voxels](int32 RowIndex)
		{
			const int32 VoxelY = RowIndex % NoiseSize, VoxelZ = RowIndex / NoiseSize;
			for (int32 VoxelX = 0; VoxelX < NoiseSize; ++VoxelX)
				Voxels[(VoxelZ*NoiseSize + VoxelY)*NoiseSize + VoxelX] = SampleNoise(VoxelX, VoxelY, VoxelZ);
		});
		UE_LOG(LogTemp, Log, TEXT("swg.CreateSpaceNebulaAssets: noise generated in %.1f s"), FPlatformTime::Seconds()-Started);
		// Uncompressed RGBA8 (8 MB) keeps the wispy detail free of block artefacts; the volume tiles, so wrap addressing.
		Texture->SRGB = false; Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->MipGenSettings = TMGS_NoMipmaps; Texture->NeverStream = true; Texture->AddressMode = TA_Wrap;
		Texture->UpdateSourceFromFunction([&Voxels](int32 VoxelX, int32 VoxelY, int32 VoxelZ, void* Out)
		{
			*static_cast<FColor*>(Out) = Voxels[(VoxelZ*NoiseSize + VoxelY)*NoiseSize + VoxelX];
		}, NoiseSize, NoiseSize, NoiseSize, TSF_BGRA8);
		Texture->UpdateResource();
		Texture->MarkPackageDirty();
		SavePackageOf(Texture);
	}

	UMaterial* CreateOrLoadMaterial(const FString& Name, bool bRebuild)
	{
		const FString PackagePath = TEXT("/Game/SWGEmu/Materials/") + Name;
		if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *Name)))
		{
			if (!bRebuild)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.CreateSpaceNebulaAssets: %s already exists - pass 'rebuild' to regenerate"), *Name);
				return nullptr;
			}
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(Existing);
			return Existing;
		}
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		return Cast<UMaterial>(AssetTools.CreateAsset(Name, TEXT("/Game/SWGEmu/Materials"), UMaterial::StaticClass(), Factory));
	}

	template <typename T>
	T* AddExpression(UMaterial* Material, int32 PositionX, int32 PositionY)
	{
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(Material, T::StaticClass(), PositionX, PositionY));
	}

	UMaterialExpressionCustom* AddCustom(UMaterial* Material, int32 PositionX, int32 PositionY, const TCHAR* Description, const TCHAR* Code,
		ECustomMaterialOutputType OutputType, const TArray<FName>& InputNames)
	{
		UMaterialExpressionCustom* Custom = AddExpression<UMaterialExpressionCustom>(Material, PositionX, PositionY);
		Custom->Description = Description; Custom->Code = Code; Custom->OutputType = OutputType;
		Custom->Inputs.Reset();
		for (const FName InputName : InputNames) Custom->Inputs.AddDefaulted_GetRef().InputName = InputName;
		return Custom;
	}

	UMaterialExpressionScalarParameter* AddScalar(UMaterial* Material, const TCHAR* Name, float DefaultValue, int32 PositionX, int32 PositionY)
	{
		auto* Parameter = AddExpression<UMaterialExpressionScalarParameter>(Material, PositionX, PositionY);
		Parameter->ParameterName = Name; Parameter->DefaultValue = DefaultValue;
		return Parameter;
	}

	UMaterialExpressionVectorParameter* AddColor(UMaterial* Material, const TCHAR* Name, const FLinearColor& DefaultValue, int32 PositionX, int32 PositionY)
	{
		auto* Parameter = AddExpression<UMaterialExpressionVectorParameter>(Material, PositionX, PositionY);
		Parameter->ParameterName = Name; Parameter->DefaultValue = DefaultValue;
		return Parameter;
	}

	void Connect(UMaterialExpression* From, const TCHAR* OutputName, UMaterialExpression* To, const TCHAR* InputName)
	{
		UMaterialEditingLibrary::ConnectMaterialExpressions(From, OutputName, To, InputName);
	}

	bool SaveMaterial(UMaterial* Material)
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
		bool bClean = true;
		if (const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
			for (const FString& Error : Resource->GetCompileErrors())
			{
				UE_LOG(LogTemp, Error, TEXT("swg.CreateSpaceNebulaAssets: %s: %s"), *Material->GetName(), *Error);
				bClean = false;
			}
		SavePackageOf(Material);
		return bClean;
	}

	// Position is the nebula-local position scaled so the authored radius is 1 (the volume is a +/-1 cube).
	const TCHAR* WarpCode = TEXT("return Position + (Warp.gba * 2.0 - 1.0) * WarpStrength;");
	const TCHAR* ShapeCoordCode = TEXT("return WarpedPosition * ShapeFrequency + Seed;");
	// Perlin-Worley body, radially thinned toward the warped edge, then eroded by the finer Worley billows so the
	// silhouette frays instead of following the sphere. The cap keeps the cube bounds from clipping the cloud.
	const TCHAR* DensityCode = TEXT(
		"float EdgeCap = 1.0 - smoothstep(0.6, 1.0, length(Position));\n"
		"float Falloff = pow(saturate(1.0 - length(WarpedPosition)), 0.6);\n"
		"float Body = saturate((Shape.r * Falloff - Coverage) / max(1.0 - Coverage, 0.01));\n"
		"float Billow = Shape.g * 0.625 + Shape.b * 0.25 + Shape.a * 0.125;\n"
		"float Cut = (1.0 - Billow) * Erosion;\n"
		"return saturate((Body - Cut) / max(1.0 - Cut, 0.01)) * EdgeCap;\n");
	// Lit gas keeps the oriented colour; thin gas and gas seen against the light drift to the facing colour. The renderer's own
	// self-shadow ray is a fixed 100 m, useless across a kilometre-scale cloud, so the shadow is analytic: the path through the
	// unit sphere toward the light, scaled by the cloud's mean density.
	const TCHAR* AlbedoCode = TEXT(
		"float3 View = normalize(WorldPosition - CameraPosition);\n"
		"float BackLit = saturate(-dot(View, LightDirection));\n"
		"float ThinEdge = 1.0 - saturate(CloudDensity * 3.0);\n"
		"float Mix = saturate(BackLit * 0.6 + ThinEdge * 0.4) * saturate(FacingBlend * 2.0);\n"
		"float3 ToLight = -LightDirection;\n"
		"float Along = dot(Position, ToLight);\n"
		"float PathLength = max(-Along + sqrt(max(Along * Along - dot(Position, Position) + 1.0, 0.0)), 0.0);\n"
		"float Optical = (PathLength * 8.0) * DensityScale * ShadowStrength;\n"
		"float Lit = lerp(0.12, 1.0, exp(-Optical));\n"
		"return lerp(OrientedColor, FacingColor, Mix) * Lit;\n");
	// A faint glow, strongest at the core, so the unlit side of a cloud still reads.
	const TCHAR* EmissiveCode = TEXT(
		"float Core = pow(saturate(1.0 - length(Position)), 1.5);\n"
		"float3 Glow = lerp(OrientedColor, FacingColor, 0.5);\n"
		"return Glow * CloudDensity * DensityScale * (BaseEmission + CoreEmission * Core);\n");

	void BuildNebulaVolume(bool bRebuild)
	{
		UMaterial* Material = CreateOrLoadMaterial(TEXT("M_SWGSpaceNebula"), bRebuild);
		if (!Material) return;
		Material->MaterialDomain = MD_Volume; Material->BlendMode = BLEND_Additive;
		Material->SetMaterialUsage(MATUSAGE_HeterogeneousVolumes);

		auto* World = AddExpression<UMaterialExpressionWorldPosition>(Material, -1800, 0);
		auto* Local = AddExpression<UMaterialExpressionTransformPosition>(Material, -1600, 0);
		Local->TransformSourceType = TRANSFORMPOSSOURCE_World; Local->TransformType = TRANSFORMPOSSOURCE_Local;
		auto* Position = AddExpression<UMaterialExpressionMultiply>(Material, -1400, 0); Position->ConstB = 1.f/32;
		Connect(World, TEXT(""), Local, TEXT(""));
		Connect(Local, TEXT(""), Position, TEXT("A"));

		auto* Seed = AddColor(Material, TEXT("Seed"), FLinearColor::Black, -1800, 300);
		auto* WarpFrequency = AddScalar(Material, TEXT("WarpFrequency"), .45f, -1800, 450);
		auto* WarpStrength = AddScalar(Material, TEXT("WarpStrength"), .35f, -1800, 550);
		auto* ShapeFrequency = AddScalar(Material, TEXT("ShapeFrequency"), .8f, -1800, 650);
		auto* Coverage = AddScalar(Material, TEXT("Coverage"), .35f, -1800, 750);
		auto* Erosion = AddScalar(Material, TEXT("Erosion"), .45f, -1800, 850);
		auto* DensityScale = AddScalar(Material, TEXT("Density"), 1.e-5f, -1800, 950);
		auto* BaseEmission = AddScalar(Material, TEXT("BaseEmission"), .02f, -1800, 1050);
		auto* CoreEmission = AddScalar(Material, TEXT("CoreEmission"), .15f, -1800, 1150);
		auto* ShadowStrength = AddScalar(Material, TEXT("ShadowStrength"), 1.f, -1800, 1200);
		auto* FacingBlend = AddScalar(Material, TEXT("FacingBlend"), .5f, -1800, 1250);
		auto* OrientedColor = AddColor(Material, TEXT("OrientedColor"), FLinearColor(.8f, .8f, .65f), -1800, 1350);
		auto* FacingColor = AddColor(Material, TEXT("FacingColor"), FLinearColor(.5f, .55f, .8f), -1800, 1500);
		auto* LightDirection = AddColor(Material, TEXT("LightDirection"), FLinearColor(0, 0, -1), -1800, 1650);
		auto* CameraPosition = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -1800, 1800);

		// Domain warp: a first sample, at a coarse offset, bends the lookup of the second.
		auto* WarpCoordinate = AddCustom(Material, -1200, 300, TEXT("WarpCoord"), TEXT("return Position * Frequency + Seed;"), CMOT_Float3, {TEXT("Position"), TEXT("Frequency"), TEXT("Seed")});
		Connect(Position, TEXT(""), WarpCoordinate, TEXT("Position")); Connect(WarpFrequency, TEXT(""), WarpCoordinate, TEXT("Frequency")); Connect(Seed, TEXT("RGB"), WarpCoordinate, TEXT("Seed"));
		auto* WarpSample = AddExpression<UMaterialExpressionTextureSampleParameterVolume>(Material, -1000, 300);
		auto* ShapeSample = AddExpression<UMaterialExpressionTextureSampleParameterVolume>(Material, -400, 300);
		UVolumeTexture* Noise = LoadObject<UVolumeTexture>(nullptr, *FString::Printf(TEXT("%s.T_SWGNebulaNoise"), NoisePackage));
		if (!Noise) UE_LOG(LogTemp, Error, TEXT("swg.CreateSpaceNebulaAssets: T_SWGNebulaNoise missing; the nebula material will sample the default volume"));
		for (UMaterialExpressionTextureSampleParameterVolume* Sample : {WarpSample, ShapeSample})
		{
			Sample->ParameterName = TEXT("NoiseVolume"); Sample->Texture = Noise; Sample->SamplerType = SAMPLERTYPE_LinearColor;
			// The ray marcher has no screen-space derivatives, so sample mip 0 explicitly.
			Sample->MipValueMode = TMVM_MipLevel; Sample->ConstMipValue = 0;
		}
		Connect(WarpCoordinate, TEXT(""), WarpSample, TEXT("UVs"));
		auto* WarpedPosition = AddCustom(Material, -800, 300, TEXT("WarpedPosition"), WarpCode, CMOT_Float3, {TEXT("Position"), TEXT("Warp"), TEXT("WarpStrength")});
		Connect(Position, TEXT(""), WarpedPosition, TEXT("Position")); Connect(WarpSample, TEXT("RGBA"), WarpedPosition, TEXT("Warp")); Connect(WarpStrength, TEXT(""), WarpedPosition, TEXT("WarpStrength"));
		auto* ShapeCoordinate = AddCustom(Material, -600, 300, TEXT("ShapeCoord"), ShapeCoordCode, CMOT_Float3, {TEXT("WarpedPosition"), TEXT("ShapeFrequency"), TEXT("Seed")});
		Connect(WarpedPosition, TEXT(""), ShapeCoordinate, TEXT("WarpedPosition")); Connect(ShapeFrequency, TEXT(""), ShapeCoordinate, TEXT("ShapeFrequency")); Connect(Seed, TEXT("RGB"), ShapeCoordinate, TEXT("Seed"));
		Connect(ShapeCoordinate, TEXT(""), ShapeSample, TEXT("UVs"));

		auto* CloudDensity = AddCustom(Material, -200, 300, TEXT("CloudDensity"), DensityCode, CMOT_Float1,
			{TEXT("Position"), TEXT("WarpedPosition"), TEXT("Shape"), TEXT("Coverage"), TEXT("Erosion")});
		Connect(Position, TEXT(""), CloudDensity, TEXT("Position")); Connect(WarpedPosition, TEXT(""), CloudDensity, TEXT("WarpedPosition"));
		Connect(ShapeSample, TEXT("RGBA"), CloudDensity, TEXT("Shape")); Connect(Coverage, TEXT(""), CloudDensity, TEXT("Coverage")); Connect(Erosion, TEXT(""), CloudDensity, TEXT("Erosion"));

		auto* Extinction = AddExpression<UMaterialExpressionMultiply>(Material, 100, 300);
		Connect(CloudDensity, TEXT(""), Extinction, TEXT("A")); Connect(DensityScale, TEXT(""), Extinction, TEXT("B"));

		auto* Albedo = AddCustom(Material, 100, 600, TEXT("Albedo"), AlbedoCode, CMOT_Float3,
			{TEXT("CloudDensity"), TEXT("WorldPosition"), TEXT("CameraPosition"), TEXT("LightDirection"), TEXT("OrientedColor"), TEXT("FacingColor"), TEXT("FacingBlend"), TEXT("Position"), TEXT("DensityScale"), TEXT("ShadowStrength")});
		Connect(CloudDensity, TEXT(""), Albedo, TEXT("CloudDensity")); Connect(World, TEXT(""), Albedo, TEXT("WorldPosition")); Connect(CameraPosition, TEXT(""), Albedo, TEXT("CameraPosition"));
		Connect(LightDirection, TEXT("RGB"), Albedo, TEXT("LightDirection")); Connect(OrientedColor, TEXT("RGB"), Albedo, TEXT("OrientedColor"));
		Connect(FacingColor, TEXT("RGB"), Albedo, TEXT("FacingColor")); Connect(FacingBlend, TEXT(""), Albedo, TEXT("FacingBlend"));
		Connect(Position, TEXT(""), Albedo, TEXT("Position"));
		Connect(DensityScale, TEXT(""), Albedo, TEXT("DensityScale")); Connect(ShadowStrength, TEXT(""), Albedo, TEXT("ShadowStrength"));

		auto* Emission = AddCustom(Material, 100, 900, TEXT("Emission"), EmissiveCode, CMOT_Float3,
			{TEXT("CloudDensity"), TEXT("Position"), TEXT("OrientedColor"), TEXT("FacingColor"), TEXT("DensityScale"), TEXT("BaseEmission"), TEXT("CoreEmission")});
		Connect(CloudDensity, TEXT(""), Emission, TEXT("CloudDensity")); Connect(Position, TEXT(""), Emission, TEXT("Position"));
		Connect(OrientedColor, TEXT("RGB"), Emission, TEXT("OrientedColor")); Connect(FacingColor, TEXT("RGB"), Emission, TEXT("FacingColor"));
		Connect(DensityScale, TEXT(""), Emission, TEXT("DensityScale")); Connect(BaseEmission, TEXT(""), Emission, TEXT("BaseEmission")); Connect(CoreEmission, TEXT(""), Emission, TEXT("CoreEmission"));

		UMaterialEditingLibrary::ConnectMaterialProperty(Albedo, TEXT(""), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Extinction, TEXT(""), MP_SubsurfaceColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Emission, TEXT(""), MP_EmissiveColor);
		SaveMaterial(Material);
	}

	// Billboard in WPO, in translated world space. The quad's width comes from the instance scale so culling bounds stay right.
	const TCHAR* WispBillboardCode = TEXT(
		"float2 Quad = float2(UV.x - 0.5, 0.5 - UV.y);\n"
		"float3 Forward = normalize(InstancePosition);\n"
		"float3 Right = cross(float3(0, 0, 1), Forward);\n"
		"float RightLength = length(Right);\n"
		"Right = RightLength > 0.001 ? Right / RightLength : float3(0, 1, 0);\n"
		"float3 Up = cross(Forward, Right);\n"
		"float RollSin;\n"
		"float RollCos;\n"
		"sincos(Random * 6.2831853, RollSin, RollCos);\n"
		"float3 RolledRight = Right * RollCos + Up * RollSin;\n"
		"Up = Up * RollCos - Right * RollSin;\n"
		"float Width = length(VertexPosition - InstancePosition) * 1.41421356;\n"
		"return InstancePosition + RolledRight * (Quad.x * Width) + Up * (Quad.y * Width) - VertexPosition;\n");
	// Soft round sprite broken up by the nebula billows, faded near the camera and out toward the edge of the wisp field.
	const TCHAR* WispAlphaCode = TEXT(
		"float Radial = saturate(1.0 - length(UV - 0.5) * 2.0);\n"
		"float Soft = Radial * Radial * (3.0 - 2.0 * Radial);\n"
		"float Wisp = saturate((Noise.g * 0.6 + Noise.b * 0.4 - 0.3) * 2.5);\n"
		"float Distance = length(InstancePosition);\n"
		"float Fade = smoothstep(100.0, 400.0, Distance) * (1.0 - smoothstep(1500.0, 2000.0, Distance));\n"
		"return Soft * Wisp * Fade * Opacity * (0.5 + 0.5 * Random);\n");
	// A different slice of the tiling noise volume per wisp: a small patch, stretched so it reads as streaks rather than cells.
	const TCHAR* WispNoiseCoordCode = TEXT("return float3(UV.x * 0.35 + Random * 5.0, UV.y * 0.08 + Random * 7.0, Random * 13.0);");

	void BuildWisp(bool bRebuild)
	{
		UMaterial* Material = CreateOrLoadMaterial(TEXT("M_SWGSpaceWisp"), bRebuild);
		if (!Material) return;
		Material->MaterialDomain = MD_Surface; Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit); Material->TwoSided = true;
		// Composites with the stars and sky, before the gas volume's late composition.
		Material->TranslucencyPass = MTP_BeforeDOF;
		Material->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);

		auto* TexCoord = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -1200, 0);
		auto* Origin = AddExpression<UMaterialExpressionConstant3Vector>(Material, -1200, 150); Origin->Constant = FLinearColor::Black;
		auto* InstancePosition = AddExpression<UMaterialExpressionTransformPosition>(Material, -1000, 150);
		// Local space on an instanced mesh is the component's; instance space gives each wisp its own centre.
		InstancePosition->TransformSourceType = TRANSFORMPOSSOURCE_Instance; InstancePosition->TransformType = TRANSFORMPOSSOURCE_TranslatedWorld;
		auto* VertexPosition = AddExpression<UMaterialExpressionWorldPosition>(Material, -1000, 300);
		VertexPosition->WorldPositionShaderOffset = WPT_CameraRelativeNoOffsets;
		auto* Random = AddExpression<UMaterialExpressionPerInstanceRandom>(Material, -1000, 450);
		Connect(Origin, TEXT(""), InstancePosition, TEXT(""));

		auto* Billboard = AddCustom(Material, -700, 150, TEXT("Billboard"), WispBillboardCode, CMOT_Float3,
			{TEXT("UV"), TEXT("InstancePosition"), TEXT("VertexPosition"), TEXT("Random")});
		Connect(TexCoord, TEXT(""), Billboard, TEXT("UV")); Connect(InstancePosition, TEXT(""), Billboard, TEXT("InstancePosition"));
		Connect(VertexPosition, TEXT(""), Billboard, TEXT("VertexPosition")); Connect(Random, TEXT(""), Billboard, TEXT("Random"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Billboard, TEXT(""), MP_WorldPositionOffset);

		auto* OpacityScale = AddScalar(Material, TEXT("Opacity"), 1.f, -1000, 600);
		auto* NoiseCoordinate = AddCustom(Material, -1000, 750, TEXT("WispNoiseCoord"), WispNoiseCoordCode, CMOT_Float3, {TEXT("UV"), TEXT("Random")});
		Connect(TexCoord, TEXT(""), NoiseCoordinate, TEXT("UV")); Connect(Random, TEXT(""), NoiseCoordinate, TEXT("Random"));
		auto* NoiseSample = AddExpression<UMaterialExpressionTextureSampleParameterVolume>(Material, -850, 750);
		NoiseSample->ParameterName = TEXT("NoiseVolume"); NoiseSample->SamplerType = SAMPLERTYPE_LinearColor;
		NoiseSample->Texture = LoadObject<UVolumeTexture>(nullptr, *FString::Printf(TEXT("%s.T_SWGNebulaNoise"), NoisePackage));
		Connect(NoiseCoordinate, TEXT(""), NoiseSample, TEXT("UVs"));
		auto* Alpha = AddCustom(Material, -700, 450, TEXT("WispAlpha"), WispAlphaCode, CMOT_Float1, {TEXT("UV"), TEXT("InstancePosition"), TEXT("Random"), TEXT("Opacity"), TEXT("Noise")});
		Connect(TexCoord, TEXT(""), Alpha, TEXT("UV")); Connect(InstancePosition, TEXT(""), Alpha, TEXT("InstancePosition"));
		Connect(Random, TEXT(""), Alpha, TEXT("Random")); Connect(OpacityScale, TEXT(""), Alpha, TEXT("Opacity"));
		Connect(NoiseSample, TEXT("RGBA"), Alpha, TEXT("Noise"));
		// Keeps a wisp from slicing into the hull or other opaque geometry.
		auto* DepthFade = AddExpression<UMaterialExpressionDepthFade>(Material, -400, 450); DepthFade->FadeDistanceDefault = 200.f;
		Connect(Alpha, TEXT(""), DepthFade, TEXT(""));

		auto* Tint = AddColor(Material, TEXT("Tint"), FLinearColor(.8f, .8f, .8f), -700, 0);
		auto* Glow = AddScalar(Material, TEXT("Glow"), .25f, -700, -100);
		auto* Emissive = AddExpression<UMaterialExpressionMultiply>(Material, -400, 0);
		Connect(Tint, TEXT("RGB"), Emissive, TEXT("A")); Connect(Glow, TEXT(""), Emissive, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Emissive, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(DepthFade, TEXT(""), MP_Opacity);
		SaveMaterial(Material);
	}
}

namespace SWGSpaceNebulaAssets
{
	void BuildAll(bool bRebuild)
	{
		BuildNoiseVolume(bRebuild);
		BuildNebulaVolume(bRebuild);
		BuildWisp(bRebuild);
	}
}
