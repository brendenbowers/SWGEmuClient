// Customization-driven appearance: face texture renderers, live re-skinning, slot rules and the mount covering.
// Member functions of USWGMeshGeneratorSubsystem, kept out of the main file.

#include "Subsystems/SWGMeshGeneratorSubsystem.h"

#include "Algo/Sort.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SWGTangibleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Character.h"
#include "Import/SWGSkeletalMeshImporter.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGIFFChunkReader.h"
#include "TRE/SWGIffTags.h"

namespace
{
	/** Shader texture tag "MAIN", stored byte-reversed in the files ("NIAM" read as a little-endian u32). */
	constexpr uint32 MainTextureTag = 0x4D41494E;

	constexpr double UnappliedTimeoutSeconds = 120.0;
	constexpr double RecordSweepIntervalSeconds = 5.0;

	FString BareName(const FString& FullName)
	{
		int32 SlashIndex = INDEX_NONE;
		return FullName.FindLastChar(TEXT('/'), SlashIndex) ? FullName.Mid(SlashIndex + 1) : FullName;
	}

	/** The .lmg names a .sat's MSGN chunk lists (null-terminated strings back to back). */
	TArray<FString> ReadSatMeshGenerators(const FSWGIffReader& Reader)
	{
		TArray<FString> Result;
		FSWGIffChunk Root;
		if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('S', 'M', 'A', 'T'), Root))
		{
			return Result;
		}
		const TArray<FSWGIffChunk> Versions = Reader.ReadChildren(Root);
		FSWGIffChunk Msgn;
		if (Versions.IsEmpty() || !Reader.FindChildChunk(Versions[0], SWG_IFF_TAG('M', 'S', 'G', 'N'), Msgn))
		{
			return Result;
		}
		FSWGIFFChunkReader Strings(Msgn, Reader);
		while (!Strings.AtEnd())
		{
			FString Path;
			if (!Strings.ReadTerminiatedString(Path))
			{
				break;
			}
			if (!Path.IsEmpty())
			{
				Result.Add(Path);
			}
		}
		return Result;
	}
}

const FSWGSlotRuleManager* USWGMeshGeneratorSubsystem::GetSlotRuleManager()
{
	if (!bSlotRulesLoaded)
	{
		bSlotRulesLoaded = true;
		if (TreSubsystem && !FSWGSlotRuleManager::Read(TreSubsystem->CreateIffReader(TEXT("customization/slot_rule_manager.iff")), SlotRuleManager))
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to read slot_rule_manager.iff"));
		}
	}
	return SlotRuleManager.Bindings.IsEmpty() ? nullptr : &SlotRuleManager;
}

const FSWGWearableAppearanceMap* USWGMeshGeneratorSubsystem::GetWearableAppearanceMap()
{
	if (!bWearableMapLoaded)
	{
		bWearableMapLoaded = true;
		FSWGDataTableData Table;
		if (!TreSubsystem || !FSWGDataTableReader::ReadDataTable(TreSubsystem->CreateIffReader(TEXT("datatables/appearance/wearable_appearance_map.iff")), Table)
			|| !FSWGWearableAppearanceMap::Read(Table, WearableAppearanceMap))
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to read wearable_appearance_map.iff"));
		}
	}
	return WearableAppearanceMap.Num() == 0 ? nullptr : &WearableAppearanceMap;
}

TArray<FString> USWGMeshGeneratorSubsystem::ResolveMountCoveringMeshes(const FString& MountAppearancePath)
{
	const FSWGWearableAppearanceMap* Map = GetWearableAppearanceMap();
	FString CoveringSat;
	if (!Map || !TreSubsystem || Map->Find(FSWGWearableAppearanceMap::DefaultCoveringSource(), MountAppearancePath, CoveringSat) != FSWGWearableAppearanceMap::EResult::Mapped)
	{
		return {};
	}
	return ReadSatMeshGenerators(TreSubsystem->CreateIffReader(CoveringSat));
}

TMap<FString, float> USWGMeshGeneratorSubsystem::ResolveSlotRuleMorphs(const TArray<FString>& ItemSlots, const TSet<FString>& OccupiedSlots)
{
	TMap<FString, float> Result;
	if (const FSWGSlotRuleManager* Rules = GetSlotRuleManager())
	{
		TMap<FString, int32> Variables;
		Rules->ResolveForItem(ItemSlots, OccupiedSlots, Variables);
		for (const TPair<FString, int32>& Pair : Variables)
		{
			Result.Add(FSWGSlotRuleManager::BareVariableName(Pair.Key), Pair.Value != 0 ? 1.0f : 0.0f);
		}
	}
	return Result;
}

TMap<FString, int32> USWGMeshGeneratorSubsystem::ResolveCustomizationIntValues(const FSWGCustomizationVariables& Customization, const FString& AppearancePath)
{
	TMap<FString, int32> Result;
	EnsureCustomizationManagersLoaded();

	TMap<FString, FSWGCustomizationVariableDef> Variables;
	AssetCustomizationManager.GetCustomizationVariables(AppearancePath, false, Variables);
	for (const TPair<FString, FSWGCustomizationVariableDef>& Pair : Variables)
	{
		int32 Value = Pair.Value.DefaultValue;
		if (const uint8* TypeId = CustomizationIdManager.NameToId.Find(Pair.Value.Name))
		{
			if (const int16* Set = Customization.Values.Find(*TypeId))
			{
				Value = *Set;
			}
		}
		Result.Add(BareName(Pair.Value.Name), Value);
	}
	return Result;
}

const FSWGImage* USWGMeshGeneratorSubsystem::GetOrDecodeImage(const FString& TexturePath)
{
	if (FSWGImage* Existing = DecodedImages.Find(TexturePath))
	{
		return Existing->IsValid() ? Existing : nullptr;
	}
	FSWGImage& Image = DecodedImages.Add(TexturePath);
	if (TreSubsystem && TreSubsystem->FileExists(TexturePath))
	{
		FSWGDdsDecoder::Decode(TreSubsystem->ExtractFile(TexturePath), Image);
	}
	if (!Image.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: could not decode texture '%s' for a texture renderer"), *TexturePath);
		return nullptr;
	}
	return &Image;
}

const FSWGPalette* USWGMeshGeneratorSubsystem::GetOrLoadPalette(const FString& PalettePath)
{
	if (FSWGPalette* Existing = LoadedPalettes.Find(PalettePath))
	{
		return Existing->Colors.IsEmpty() ? nullptr : Existing;
	}
	FSWGPalette& Palette = LoadedPalettes.Add(PalettePath);
	if (TreSubsystem && TreSubsystem->FileExists(PalettePath))
	{
		FSWGPalette::Parse(TreSubsystem->ExtractFile(PalettePath), Palette);
	}
	return Palette.Colors.IsEmpty() ? nullptr : &Palette;
}

const FSWGTextureRenderer* USWGMeshGeneratorSubsystem::GetOrReadTextureRenderer(const FString& RendererPath)
{
	if (FSWGTextureRenderer* Existing = TextureRenderers.Find(RendererPath))
	{
		return Existing->Draws.IsEmpty() ? nullptr : Existing;
	}
	FSWGTextureRenderer& Renderer = TextureRenderers.Add(RendererPath);
	if (!TreSubsystem || !FSWGTextureRenderer::Read(TreSubsystem->CreateIffReader(RendererPath), Renderer))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to read texture renderer '%s'"), *RendererPath);
		return nullptr;
	}
	if (Renderer.bHasUnsupportedOps)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: texture renderer '%s' uses ops that are not implemented"), *RendererPath);
	}
	return &Renderer;
}

UTexture2D* USWGMeshGeneratorSubsystem::GetOrBuildTextureRendererOutput(const FString& RendererPath, const TMap<FString, int32>& Values)
{
	const FSWGTextureRenderer* Renderer = GetOrReadTextureRenderer(RendererPath);
	if (!Renderer)
	{
		return nullptr;
	}

	FString Key = RendererPath;
	for (const FString& Variable : Renderer->GetVariables())
	{
		const int32* Value = Values.Find(Variable);
		Key += FString::Printf(TEXT("|%s=%d"), *Variable, Value ? *Value : 0);
	}
	if (TObjectPtr<UTexture2D>* Cached = RendererOutputCache.Find(Key))
	{
		return *Cached;
	}

	FSWGTextureCompositor::FInputs Inputs;
	Inputs.LoadImage = [this](const FString& Path) { return GetOrDecodeImage(Path); };
	Inputs.LoadPalette = [this](const FString& Path) { return GetOrLoadPalette(Path); };
	Inputs.GetVariable = [&Values](const FString& Name) { const int32* Value = Values.Find(Name); return Value ? *Value : 0; };

	FSWGImage Composite;
	UTexture2D* Texture = nullptr;
	if (FSWGTextureCompositor::Compose(*Renderer, Inputs, Composite))
	{
		Texture = FSWGImageTexture::Create(Composite, FName(*FPaths::GetBaseFilename(RendererPath)), /*bSRGB=*/true);
	}
	RendererOutputCache.Add(Key, Texture);
	return Texture;
}

void USWGMeshGeneratorSubsystem::RegisterCharacterAppearance(AActor& Actor, const FString& AppearancePath, const TArray<FString>& MeshVirtualPaths)
{
	FCharacterAppearanceRecord* Record = CharacterRecords.FindByPredicate([&Actor](const FCharacterAppearanceRecord& Candidate) { return Candidate.Actor.Get() == &Actor; });
	if (!Record)
	{
		Record = &CharacterRecords.AddDefaulted_GetRef();
		Record->Actor = &Actor;
	}
	Record->AppearancePath = AppearancePath;
	Record->MeshVirtualPaths = MeshVirtualPaths;
	Record->bApplied = false;
	Record->RegisteredSeconds = FPlatformTime::Seconds();
}

void USWGMeshGeneratorSubsystem::RefreshCharacterAppearance(AActor& Actor)
{
	if (FCharacterAppearanceRecord* Record = CharacterRecords.FindByPredicate([&Actor](const FCharacterAppearanceRecord& Candidate) { return Candidate.Actor.Get() == &Actor; }))
	{
		if (Record->bApplied)
		{
			ApplyCharacterAppearance(*Record, /*bForce=*/false);
		}
	}
}

void USWGMeshGeneratorSubsystem::TickCharacterRecords()
{
	if (CharacterRecords.IsEmpty())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	for (FCharacterAppearanceRecord& Record : CharacterRecords)
	{
		if (!Record.bApplied && Record.Actor.IsValid())
		{
			if (ApplyCharacterAppearance(Record, /*bForce=*/false) || Now - Record.RegisteredSeconds > UnappliedTimeoutSeconds)
			{
				Record.bApplied = true;
			}
		}
	}
	if (Now - LastRecordSweepSeconds > RecordSweepIntervalSeconds)
	{
		LastRecordSweepSeconds = Now;
		CharacterRecords.RemoveAll([](const FCharacterAppearanceRecord& Record) { return !Record.Actor.IsValid(); });
	}
}

bool USWGMeshGeneratorSubsystem::ApplyCharacterAppearance(FCharacterAppearanceRecord& Record, bool bForce)
{
	AActor* Actor = Record.Actor.Get();
	ACharacter* Character = Cast<ACharacter>(Actor);
	USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	if (!Body || !TreSubsystem)
	{
		return true;
	}
	USkeletalMesh* Asset = Body->GetSkeletalMeshAsset();
	if (!Asset)
	{
		return false; // the generated mesh has not landed yet
	}

	const USWGTangibleComponent* Tangible = Actor->FindComponentByClass<USWGTangibleComponent>();
	const FSWGCustomizationVariables Customization = Tangible ? Tangible->GetEffectiveCustomization() : FSWGCustomizationVariables();
	const TMap<FString, int32> Values = ResolveCustomizationIntValues(Customization, Record.AppearancePath);

	TArray<FString> Names;
	Values.GetKeys(Names);
	Algo::Sort(Names);
	uint32 Signature = GetTypeHash(Asset);
	for (const FString& Name : Names)
	{
		Signature = HashCombineFast(Signature, HashCombineFast(GetTypeHash(Name), (uint32)Values[Name]));
	}
	if (Record.bApplied && !bForce && Signature == Record.AppliedSignature)
	{
		return true;
	}
	const bool bFirstApply = !Record.bApplied;
	Record.AppliedSignature = Signature;

	// Face composites, keyed by the shader whose MAIN texture they replace.
	TMap<FString, UTexture2D*> CompositeByShader;
	for (const FString& MeshPath : Record.MeshVirtualPaths)
	{
		const FSWGIffReader Reader = TreSubsystem->CreateIffReader(MeshPath);
		const TArray<FSWGMeshTextureRendererRef> Refs = FSWGMeshTextureRendererReader::Read(Reader);
		if (Refs.IsEmpty())
		{
			continue;
		}
		const TArray<FString> Shaders = FSWGMeshTextureRendererReader::ReadShaderNames(Reader);
		for (const FSWGMeshTextureRendererRef& Ref : Refs)
		{
			UTexture2D* Composite = GetOrBuildTextureRendererOutput(Ref.RendererPath, Values);
			for (const TPair<int32, uint32>& Target : Ref.Targets)
			{
				if (Composite && Target.Value == MainTextureTag && Shaders.IsValidIndex(Target.Key))
				{
					CompositeByShader.Add(Shaders[Target.Key], Composite);
				}
			}
		}
	}

	// The mesh pipeline already skinned a fresh build from the same customization; only a delta or a composite changes anything.
	if (bFirstApply && CompositeByShader.IsEmpty())
	{
		return true;
	}

	const TMap<FString, FLinearColor> Tints = ResolveCustomizationPaletteTints(Customization, Record.AppearancePath);
	const TMap<FString, int32> TextureIndices = ResolveCustomizationTextureIndices(Customization, Record.AppearancePath);
	const TMap<FString, float> MorphWeights = ResolveCustomizationMorphWeights(Customization, Record.AppearancePath);
	const TMap<FString, FLinearColor>* TintsPtr = Tints.IsEmpty() ? nullptr : &Tints;
	const TMap<FString, int32>* IndicesPtr = TextureIndices.IsEmpty() ? nullptr : &TextureIndices;

	for (int32 SlotIndex = 0; SlotIndex < Asset->GetMaterials().Num(); ++SlotIndex)
	{
		const FString ShaderPath = ExtractShaderPathFromMaterialSlotName(Asset->GetMaterials()[SlotIndex].MaterialSlotName.ToString());
		UTexture2D* const* Composite = CompositeByShader.Find(ShaderPath);
		if (UMaterialInterface* Material = GetOrBuildObjectMaterial(ShaderPath, TintsPtr, IndicesPtr, FString(), Composite ? *Composite : nullptr))
		{
			Body->SetMaterial(SlotIndex, Material);
		}
	}
	for (const TPair<FString, float>& Morph : MorphWeights)
	{
		Body->SetMorphTarget(FName(*Morph.Key), Morph.Value);
	}
	UE_LOG(LogTemp, Log, TEXT("USWGMeshGeneratorSubsystem: %s re-skinned (%d composite(s), %d tint(s), %d morph(s))"),
		*Actor->GetName(), CompositeByShader.Num(), Tints.Num(), MorphWeights.Num());
	return true;
}

TArray<FString> USWGMeshGeneratorSubsystem::DescribeCharacterCustomization(AActor& Actor)
{
	TArray<FString> Lines;
	const FCharacterAppearanceRecord* Record = CharacterRecords.FindByPredicate([&Actor](const FCharacterAppearanceRecord& Candidate) { return Candidate.Actor.Get() == &Actor; });
	if (!Record)
	{
		Lines.Add(FString::Printf(TEXT("%s: no registered skeletal appearance"), *Actor.GetName()));
		return Lines;
	}

	const USWGTangibleComponent* Tangible = Actor.FindComponentByClass<USWGTangibleComponent>();
	const FSWGCustomizationVariables Customization = Tangible ? Tangible->GetEffectiveCustomization() : FSWGCustomizationVariables();
	const TMap<FString, int32> Values = ResolveCustomizationIntValues(Customization, Record->AppearancePath);
	Lines.Add(FString::Printf(TEXT("%s appearance '%s' applied=%d, %d wire value(s), %d resolved variable(s)"),
		*Actor.GetName(), *Record->AppearancePath, Record->bApplied ? 1 : 0, Customization.Values.Num(), Values.Num()));

	EnsureCustomizationManagersLoaded();
	TArray<FString> Names;
	Values.GetKeys(Names);
	Algo::Sort(Names);
	for (const FString& Name : Names)
	{
		Lines.Add(FString::Printf(TEXT("  var %s = %d"), *Name, Values[Name]));
	}

	for (const FString& MeshPath : Record->MeshVirtualPaths)
	{
		const FSWGIffReader Reader = TreSubsystem->CreateIffReader(MeshPath);
		const TArray<FString> Shaders = FSWGMeshTextureRendererReader::ReadShaderNames(Reader);
		for (const FSWGMeshTextureRendererRef& Ref : FSWGMeshTextureRendererReader::Read(Reader))
		{
			const FSWGTextureRenderer* Renderer = GetOrReadTextureRenderer(Ref.RendererPath);
			Lines.Add(FString::Printf(TEXT("  renderer %s from %s: %s"), *Ref.RendererPath, *MeshPath,
				Renderer ? *FString::Printf(TEXT("%dx%d, %d draw(s), %d op(s)%s"), Renderer->Width, Renderer->Height, Renderer->Draws.Num(), Renderer->Ops.Num(), Renderer->bHasUnsupportedOps ? TEXT(", UNSUPPORTED OPS") : TEXT("")) : TEXT("unreadable")));
			for (const TPair<int32, uint32>& Target : Ref.Targets)
			{
				Lines.Add(FString::Printf(TEXT("    replaces MAIN of shader #%d %s"), Target.Key, Shaders.IsValidIndex(Target.Key) ? *Shaders[Target.Key] : TEXT("?")));
			}
			if (Renderer)
			{
				for (const FString& Variable : Renderer->GetVariables())
				{
					const int32* Value = Values.Find(Variable);
					Lines.Add(FString::Printf(TEXT("    reads %s = %d%s"), *Variable, Value ? *Value : 0, Value ? TEXT("") : TEXT(" (not an ACST variable of this appearance, reads 0)")));
				}
			}
		}
	}
	return Lines;
}
