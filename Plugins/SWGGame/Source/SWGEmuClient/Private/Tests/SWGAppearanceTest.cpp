#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "Engine/GameInstance.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "TRE/SWGAssetCustomizationManager.h"
#include "TRE/SWGCustomizationTables.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGSlotRuleReader.h"
#include "TRE/SWGTextureRenderer.h"
#include "TRE/SWGWearableAppearanceMap.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	USWGTreSubsystem* LoadTreForAppearanceTest(FAutomationTestBase& Test)
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
		if (!Tre)
		{
			Test.AddError(TEXT("could not construct a TRE subsystem"));
			return nullptr;
		}
		Tre->AddToRoot();
		Tre->LoadConfig();
		if (!Tre->LoadArchives())
		{
			Test.AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}

	FSWGImage SolidImage(int32 Width, int32 Height, FColor Color)
	{
		FSWGImage Image;
		Image.Width = Width;
		Image.Height = Height;
		Image.Pixels.Init(Color, Width * Height);
		return Image;
	}

	FSWGTextureRenderer::FVertex Vertex(float X, float Y, float U, float V)
	{
		FSWGTextureRenderer::FVertex Out;
		Out.X = X; Out.Y = Y; Out.U = U; Out.V = V;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGAppearanceFallbackTest, "SWGEmu.Appearance.Fallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGAppearanceFallbackTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForAppearanceTest(*this);
	if (!Tre) return false;
	ON_SCOPE_EXIT { Tre->RemoveFromRoot(); };
	USWGMeshGeneratorSubsystem* Generator = NewObject<USWGMeshGeneratorSubsystem>(Tre->GetOuter());
	Generator->TreSubsystem = Tre;
	TArray<FString> Paths;
	TMap<FString, FString> LatPaths;
	TArray<FSWGLodLevel> Lods;
	FString CollisionPath;
	bool bSkeletal = true;
	TestTrue(TEXT("missing appearance uses retail placeholder"), Generator->ResolveAppearanceMeshPaths(TEXT("appearance/__missing_test.apt"), TEXT("test"), Paths, LatPaths, bSkeletal, &Lods, &CollisionPath));
	TestFalse(TEXT("placeholder is static"), bSkeletal);
	TestFalse(TEXT("placeholder has a mesh"), Paths.IsEmpty());
	Paths.Reset();
	TestFalse(TEXT("empty appearance stays invisible"), Generator->ResolveAppearanceMeshPaths(TEXT(""), TEXT("test"), Paths, LatPaths, bSkeletal));
	TestFalse(TEXT("particle appearance stays meshless"), Generator->ResolveAppearanceMeshPaths(TEXT("appearance/__missing_test.prt"), TEXT("test"), Paths, LatPaths, bSkeletal));
	TestFalse(TEXT("sprite appearance stays meshless"), Generator->ResolveAppearanceMeshPaths(TEXT("appearance/__missing_test.spr"), TEXT("test"), Paths, LatPaths, bSkeletal));
	TestFalse(TEXT("trail appearance stays meshless"), Generator->ResolveAppearanceMeshPaths(TEXT("appearance/__missing_test.swh"), TEXT("test"), Paths, LatPaths, bSkeletal));
	TestFalse(TEXT("lightning appearance stays meshless"), Generator->ResolveAppearanceMeshPaths(TEXT("appearance/__missing_test.ltn"), TEXT("test"), Paths, LatPaths, bSkeletal));
	TestFalse(TEXT("wrapper depth is bounded"), Generator->TryResolveAppearanceMeshPaths(TEXT("appearance/defaultappearance.apt"), TEXT("test"), Paths, LatPaths, bSkeletal, nullptr, nullptr, 8));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGSlotRuleSyntheticTest,
	"SWGEmu.Appearance.SlotRules.Evaluate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGSlotRuleSyntheticTest::RunTest(const FString& Parameters)
{
	// chest2 AND NOT (chest3_l OR chest3_r), the jacket bandolier rule.
	FSWGSlotRuleManager Rules;
	auto AddSlot = [&Rules](const TCHAR* Name)
	{
		FSWGSlotRuleNode Node;
		Node.SlotName = Name;
		return Rules.Nodes.Add(Node);
	};
	const int32 Chest2 = AddSlot(TEXT("chest2"));
	const int32 Chest3Left = AddSlot(TEXT("chest3_l"));
	const int32 Chest3Right = AddSlot(TEXT("chest3_r"));
	FSWGSlotRuleNode OrNode; OrNode.Kind = FSWGSlotRuleNode::EKind::Or; OrNode.Children = { Chest3Left, Chest3Right };
	const int32 Either = Rules.Nodes.Add(OrNode);
	FSWGSlotRuleNode NotNode; NotNode.Kind = FSWGSlotRuleNode::EKind::Negate; NotNode.Children = { Either };
	const int32 Neither = Rules.Nodes.Add(NotNode);
	FSWGSlotRuleNode AndNode; AndNode.Kind = FSWGSlotRuleNode::EKind::And; AndNode.Children = { Chest2, Neither };
	const int32 Root = Rules.Nodes.Add(AndNode);

	TestTrue(TEXT("chest2 alone"), Rules.Evaluate(Root, { TEXT("chest2") }));
	TestFalse(TEXT("chest2 under a left robe slot"), Rules.Evaluate(Root, { TEXT("chest2"), TEXT("chest3_l") }));
	TestFalse(TEXT("chest2 under a right robe slot"), Rules.Evaluate(Root, { TEXT("chest2"), TEXT("chest3_r") }));
	TestFalse(TEXT("nothing worn"), Rules.Evaluate(Root, {}));
	TestTrue(TEXT("slot names ignore case"), Rules.Evaluate(Root, { TEXT("CHEST2") }));
	TestFalse(TEXT("bad node index"), Rules.Evaluate(99, {}));

	Rules.Bindings.Add({ TEXT("chest2"), TEXT("/shared_owner/blend_test"), Root });
	TMap<FString, int32> Variables;
	Rules.ResolveForItem({ TEXT("chest2") }, { TEXT("chest2"), TEXT("chest3_l") }, Variables);
	TestEqual(TEXT("bound variable written"), Variables.Num(), 1);
	TestEqual(TEXT("bound variable value"), Variables.FindRef(TEXT("/shared_owner/blend_test")), 0);
	Variables.Reset();
	Rules.ResolveForItem({ TEXT("hat") }, { TEXT("chest2") }, Variables);
	TestEqual(TEXT("item in an unbound slot gets nothing"), Variables.Num(), 0);
	TestEqual(TEXT("bare name"), FSWGSlotRuleManager::BareVariableName(TEXT("/shared_owner/blend_test")), FString(TEXT("blend_test")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGAppearanceTablesTest,
	"SWGEmu.Appearance.Tables.Retail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGAppearanceTablesTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForAppearanceTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT { Tre->RemoveFromRoot(); };

	// Slot rules: four retail bindings, all on chest2 / chest3_l.
	FSWGSlotRuleManager Rules;
	if (TestTrue(TEXT("slot_rule_manager reads"), FSWGSlotRuleManager::Read(Tre->CreateIffReader(TEXT("customization/slot_rule_manager.iff")), Rules)))
	{
		TestEqual(TEXT("binding count"), Rules.Bindings.Num(), 4);
		const FSWGSlotRuleBinding* Bandolier = Rules.Bindings.FindByPredicate([](const FSWGSlotRuleBinding& Binding) { return Binding.VariableName.EndsWith(TEXT("blend_jacket_bandolier")); });
		if (TestNotNull(TEXT("jacket bandolier binding"), Bandolier))
		{
			TestFalse(TEXT("bandolier hidden under a robe"), Rules.Evaluate(Bandolier->RuleNode, { TEXT("chest2"), TEXT("chest3_r") }));
			TestTrue(TEXT("bandolier shown without one"), Rules.Evaluate(Bandolier->RuleNode, { TEXT("chest2") }));
		}
		const FSWGSlotRuleBinding* Robe = Rules.Bindings.FindByPredicate([](const FSWGSlotRuleBinding& Binding) { return Binding.VariableName.EndsWith(TEXT("blend_robe_bandolier")); });
		if (TestNotNull(TEXT("robe bandolier binding"), Robe))
		{
			TestTrue(TEXT("robe bandolier follows chest3_r"), Rules.Evaluate(Robe->RuleNode, { TEXT("chest3_r") }));
			TestFalse(TEXT("robe bandolier needs a robe"), Rules.Evaluate(Robe->RuleNode, { TEXT("chest2") }));
		}
	}

	// Wearable appearance map.
	FSWGDataTableData Table;
	FSWGWearableAppearanceMap Map;
	if (TestTrue(TEXT("wearable_appearance_map reads"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/appearance/wearable_appearance_map.iff")), Table) && FSWGWearableAppearanceMap::Read(Table, Map)))
	{
		FString Mapped;
		TestTrue(TEXT("dewback covering"), Map.Find(FSWGWearableAppearanceMap::DefaultCoveringSource(), TEXT("appearance/dewback_hue.sat"), Mapped) == FSWGWearableAppearanceMap::EResult::Mapped);
		TestEqual(TEXT("dewback covering path"), Mapped, FString(TEXT("appearance/mnt_wr_default_covering_dewback_dewback.sat")));
		TestTrue(TEXT("lookup ignores case"), Map.Find(TEXT("LOOKUP/mnt_wr_default_covering"), TEXT("Appearance/Bantha_Hue.sat"), Mapped) == FSWGWearableAppearanceMap::EResult::Mapped);
		TestTrue(TEXT("unknown wearer"), Map.Find(FSWGWearableAppearanceMap::DefaultCoveringSource(), TEXT("appearance/hum_m.sat"), Mapped) == FSWGWearableAppearanceMap::EResult::NotFound);
	}
	FSWGDataTableData HiddenTable;
	HiddenTable.ColumnNames = { TEXT("source_wearable_appearance_name"), TEXT("wearer_appearance_name"), TEXT("mapped_wearable_appearance_name") };
	HiddenTable.Rows.Add({ { TEXT("a.sat"), TEXT("b.sat"), TEXT("") } });
	FSWGWearableAppearanceMap HiddenMap;
	FString Ignored;
	TestTrue(TEXT("synthetic map reads"), FSWGWearableAppearanceMap::Read(HiddenTable, HiddenMap));
	TestTrue(TEXT("empty mapping hides"), HiddenMap.Find(TEXT("a.sat"), TEXT("b.sat"), Ignored) == FSWGWearableAppearanceMap::EResult::Hidden);

	// allow_bald and the creation tables.
	FSWGAllowBaldTable Bald;
	if (TestTrue(TEXT("allow_bald reads"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/customization/allow_bald.iff")), Table) && FSWGAllowBaldTable::Read(Table, Bald)))
	{
		TestEqual(TEXT("species rows"), Bald.GetAll().Num(), 16);
		TestTrue(TEXT("humans may be bald"), Bald.IsBaldAllowed(TEXT("human_male")));
		TestFalse(TEXT("zabraks may not"), Bald.IsBaldAllowed(TEXT("zabrak_female")));
		TestTrue(TEXT("unknown species default to allowed"), Bald.IsBaldAllowed(TEXT("nobody")));
	}

	FSWGDataTableData SharedTable, InstanceTable;
	FSWGCustomizationGroupTable Groups;
	if (TestTrue(TEXT("customization groups read"),
		FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/customization/customization_group_shared.iff")), SharedTable)
		&& FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/customization/customization_group_instance.iff")), InstanceTable)
		&& FSWGCustomizationGroupTable::Read(SharedTable, InstanceTable, Groups)))
	{
		const FSWGCustomizationGroupShared* Hair = Groups.FindShared(TEXT("hair"));
		if (TestNotNull(TEXT("hair group"), Hair))
		{
			TestTrue(TEXT("hair is hair"), Hair->bIsHair);
		}
		const FSWGCustomizationGroupShared* Markings = Groups.FindShared(TEXT("markings"));
		if (TestNotNull(TEXT("markings group"), Markings))
		{
			TestTrue(TEXT("markings are markings"), Markings->bIsMarking);
		}
		TestTrue(TEXT("humans offer several groups"), Groups.GetGroupsFor(TEXT("human_male")).Num() >= 4);
	}

	FSWGCustomizationDataTable Data;
	if (TestTrue(TEXT("customization_data reads"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/customization/customization_data.iff")), Table) && FSWGCustomizationDataTable::Read(Table, Data)))
	{
		TestTrue(TEXT("human_male has controls"), Data.GetRowsFor(TEXT("human_male")).Num() > 5);
	}

	FSWGPaletteColumnsTable Columns;
	if (TestTrue(TEXT("palette_columns reads"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/customization/palette_columns.iff")), Table) && FSWGPaletteColumnsTable::Read(Table, Columns)))
	{
		const FSWGPaletteColumns* Eye = Columns.Find(TEXT("pc_eye_bth"));
		if (TestNotNull(TEXT("pc_eye_bth"), Eye))
		{
			TestEqual(TEXT("creation columns"), Eye->CreationColumns, 5);
			TestEqual(TEXT("creation indexes"), Eye->CreationIndexes, 35);
			TestEqual(TEXT("tier count"), Eye->ImageDesignTiers.Num(), 6);
			TestEqual(TEXT("master tier indexes"), Eye->ImageDesignTiers.Last().Value, 256);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGTextureCompositorSyntheticTest,
	"SWGEmu.Appearance.TextureRenderer.Compose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGTextureCompositorSyntheticTest::RunTest(const FString& Parameters)
{
	// 4x4 target: an opaque copy of a red image tinted by palette[1], then a half-transparent
	// blue patch over the right half blended on top.
	FSWGTextureRenderer Renderer;
	Renderer.Width = 4;
	Renderer.Height = 4;
	Renderer.Shaders = { TEXT("shader/texren_copy_c1a1.sht"), TEXT("shader/texren_alphablend_c1a1.sht") };
	Renderer.Textures = { TEXT("red"), TEXT("blue_a"), TEXT("blue_b") };
	Renderer.VertexBuffers.Add({ Vertex(0, 0, 0, 0), Vertex(1, 0, 1, 0), Vertex(1, 1, 1, 1), Vertex(0, 1, 0, 1) });
	Renderer.VertexBuffers.Add({ Vertex(0.5f, 0, 0, 0), Vertex(1, 0, 1, 0), Vertex(1, 1, 1, 1), Vertex(0.5f, 1, 0, 1) });
	Renderer.Draws = { { 0, 0 }, { 1, 1 } };

	FSWGTextureRenderer::FOp SetRed;
	SetRed.Kind = FSWGTextureRenderer::EOpKind::SetTexture;
	SetRed.ShaderIndex = 0;
	SetRed.TextureBase = 0;
	FSWGTextureRenderer::FOp Tint;
	Tint.Kind = FSWGTextureRenderer::EOpKind::SetFactorFromPalette;
	Tint.ShaderIndex = 0;
	Tint.Palette = TEXT("pal");
	Tint.Variable = TEXT("index_color_skin");
	FSWGTextureRenderer::FOp PickBlue;
	PickBlue.Kind = FSWGTextureRenderer::EOpKind::SetTextureByVariable;
	PickBlue.ShaderIndex = 1;
	PickBlue.TextureBase = 1;
	PickBlue.TextureCount = 2;
	PickBlue.Variable = TEXT("index_style_patch");
	Renderer.Ops = { SetRed, Tint, PickBlue };

	const FSWGImage Red = SolidImage(2, 2, FColor(200, 0, 0, 255));
	const FSWGImage BlueA = SolidImage(2, 2, FColor(0, 0, 255, 0));
	const FSWGImage BlueB = SolidImage(2, 2, FColor(0, 0, 255, 128));
	FSWGPalette Palette;
	Palette.Colors = { FColor(255, 255, 255, 255), FColor(128, 255, 0, 255) };

	TMap<FString, int32> Values;
	FSWGTextureCompositor::FInputs Inputs;
	Inputs.LoadImage = [&](const FString& Path) -> const FSWGImage* { return Path == TEXT("red") ? &Red : Path == TEXT("blue_a") ? &BlueA : Path == TEXT("blue_b") ? &BlueB : nullptr; };
	Inputs.LoadPalette = [&](const FString&) -> const FSWGPalette* { return &Palette; };
	Inputs.GetVariable = [&](const FString& Name) { return Values.FindRef(Name); };

	Values.Add(TEXT("index_color_skin"), 1);
	Values.Add(TEXT("index_style_patch"), 0);
	FSWGImage Out;
	TestTrue(TEXT("composes"), FSWGTextureCompositor::Compose(Renderer, Inputs, Out));
	TestEqual(TEXT("width"), Out.Width, 4);
	// Left half: red * (128, 255, 0) / 255 = (100, 0, 0), alpha from the texture.
	TestEqual(TEXT("left red"), (int32)Out.Pixels[0].R, 100);
	TestEqual(TEXT("left green"), (int32)Out.Pixels[0].G, 0);
	TestEqual(TEXT("left alpha"), (int32)Out.Pixels[0].A, 255);
	// Right half with a fully transparent patch: the blend leaves the copy untouched.
	TestEqual(TEXT("transparent patch leaves red"), (int32)Out.Pixels[3].R, 100);
	TestEqual(TEXT("transparent patch leaves blue 0"), (int32)Out.Pixels[3].B, 0);

	// Style 1 is the half-transparent patch: 50% blue over (100, 0, 0).
	Values[TEXT("index_style_patch")] = 1;
	TestTrue(TEXT("composes again"), FSWGTextureCompositor::Compose(Renderer, Inputs, Out));
	TestEqual(TEXT("blended red"), (int32)Out.Pixels[3].R, 50);
	TestEqual(TEXT("blended blue"), (int32)Out.Pixels[3].B, 128);
	TestEqual(TEXT("left unaffected"), (int32)Out.Pixels[0].R, 100);

	// Out-of-range style clamps; out-of-range palette index takes the last entry.
	Values[TEXT("index_style_patch")] = 9;
	TestEqual(TEXT("style clamps high"), FSWGTextureCompositor::ResolveTextureIndex(PickBlue, 9), 2);
	TestEqual(TEXT("style clamps low"), FSWGTextureCompositor::ResolveTextureIndex(PickBlue, -3), 1);
	TestEqual(TEXT("palette index in range"), FSWGTextureCompositor::ResolvePaletteIndex(1, 2), 1);
	TestEqual(TEXT("palette index too high"), FSWGTextureCompositor::ResolvePaletteIndex(40, 2), 1);
	TestEqual(TEXT("palette index negative"), FSWGTextureCompositor::ResolvePaletteIndex(-1, 2), 1);

	// A missing texture draws nothing; the target stays transparent.
	Renderer.Textures[0] = TEXT("missing");
	TestTrue(TEXT("composes with a missing texture"), FSWGTextureCompositor::Compose(Renderer, Inputs, Out));
	TestEqual(TEXT("nothing drawn"), (int32)Out.Pixels[0].A, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGTextureRendererRetailTest,
	"SWGEmu.Appearance.TextureRenderer.Retail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGTextureRendererRetailTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForAppearanceTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT { Tre->RemoveFromRoot(); };

	FSWGTextureRenderer Renderer;
	for (const TCHAR* Name : {TEXT("hum_f_face"), TEXT("twk_m_face"), TEXT("twk_f_face"), TEXT("zab_m_face"), TEXT("zab_f_face"), TEXT("sul_f_face"), TEXT("zab_m_face_special_01")})
	{
		const FString Path = FString::Printf(TEXT("texturerenderer/%s.trt"), Name);
		FSWGTextureRenderer Other;
		TestTrue(*Path, FSWGTextureRenderer::Read(Tre->CreateIffReader(Path), Other));
		TestFalse(*FString::Printf(TEXT("%s has supported commands"), Name), Other.bHasUnsupportedOps);
	}
	if (!TestTrue(TEXT("hum_m_face.trt reads"), FSWGTextureRenderer::Read(Tre->CreateIffReader(TEXT("texturerenderer/hum_m_face.trt")), Renderer)))
	{
		return false;
	}
	TestEqual(TEXT("width"), Renderer.Width, 128);
	TestEqual(TEXT("height"), Renderer.Height, 256);
	TestEqual(TEXT("shaders"), Renderer.Shaders.Num(), 5);
	TestEqual(TEXT("textures"), Renderer.Textures.Num(), 32);
	TestEqual(TEXT("vertex buffers"), Renderer.VertexBuffers.Num(), 5);
	TestEqual(TEXT("draws"), Renderer.Draws.Num(), 5);
	TestEqual(TEXT("ops"), Renderer.Ops.Num(), 9);
	TestFalse(TEXT("no unsupported ops"), Renderer.bHasUnsupportedOps);
	TestFalse(TEXT("first shader is an opaque copy"), Renderer.IsAlphaBlended(0));
	TestTrue(TEXT("second shader blends"), Renderer.IsAlphaBlended(1));
	TestEqual(TEXT("first texture"), Renderer.Textures[0], FString(TEXT("texture/hum_m_face_brow_thick.dds")));
	TestEqual(TEXT("skin texture index"), Renderer.Textures[25], FString(TEXT("texture/hum_m_face.dds")));
	const TArray<FString> Variables = Renderer.GetVariables();
	for (const TCHAR* Expected : { TEXT("index_color_skin"), TEXT("index_style_eyebrow"), TEXT("index_color_facial_hair"), TEXT("index_style_beard"), TEXT("index_style_freckles") })
	{
		TestTrue(*FString::Printf(TEXT("reads %s"), Expected), Variables.Contains(Expected));
	}
	const FSWGTextureRenderer::FOp* Beard = Renderer.Ops.FindByPredicate([](const FSWGTextureRenderer::FOp& Op) { return Op.Variable == TEXT("index_style_beard"); });
	if (TestNotNull(TEXT("beard op"), Beard))
	{
		TestEqual(TEXT("beard base"), Beard->TextureBase, 7);
		TestEqual(TEXT("beard count"), Beard->TextureCount, 18);
		TestEqual(TEXT("beard shader"), Beard->ShaderIndex, 2);
	}
	const FSWGTextureRenderer::FOp* Skin = Renderer.Ops.FindByPredicate([](const FSWGTextureRenderer::FOp& Op) { return Op.Variable == TEXT("index_color_skin"); });
	if (TestNotNull(TEXT("skin op"), Skin))
	{
		TestEqual(TEXT("skin palette"), Skin->Palette, FString(TEXT("palette/pc_skin_hum.pal")));
	}

	// Every variable the renderer reads is a customization variable of the human male appearance.
	FSWGAssetCustomizationManager Acst;
	if (TestTrue(TEXT("asset_customization_manager reads"), Acst.Read(Tre->CreateIffReader(TEXT("customization/asset_customization_manager.iff")))))
	{
		TMap<FString, FSWGCustomizationVariableDef> AppearanceVariables;
		Acst.GetCustomizationVariables(TEXT("appearance/hum_m.sat"), false, AppearanceVariables);
		TSet<FString> BareNames;
		for (const TPair<FString, FSWGCustomizationVariableDef>& Pair : AppearanceVariables)
		{
			BareNames.Add(FSWGSlotRuleManager::BareVariableName(Pair.Value.Name));
		}
		for (const FString& Variable : Variables)
		{
			TestTrue(*FString::Printf(TEXT("hum_m.sat has %s"), *Variable), BareNames.Contains(Variable));
		}
	}

	// The head mesh binds it to the face shader's MAIN texture.
	const FSWGIffReader Head = Tre->CreateIffReader(TEXT("appearance/mesh/hum_m_head_l0.mgn"));
	const TArray<FSWGMeshTextureRendererRef> Refs = FSWGMeshTextureRendererReader::Read(Head);
	const TArray<FString> Shaders = FSWGMeshTextureRendererReader::ReadShaderNames(Head);
	if (TestEqual(TEXT("head renderer refs"), Refs.Num(), 1))
	{
		TestEqual(TEXT("renderer path"), Refs[0].RendererPath, FString(TEXT("texturerenderer/hum_m_face.trt")));
		if (TestEqual(TEXT("targets"), Refs[0].Targets.Num(), 1))
		{
			TestEqual(TEXT("target shader index"), Refs[0].Targets[0].Key, 1);
			TestTrue(TEXT("target shader is the face"), Shaders.IsValidIndex(1) && Shaders[1] == TEXT("shader/hum_m_face.sht"));
		}
	}

	// Real compose: opaque 128x256 result.
	TMap<FString, FSWGImage> Images;
	TMap<FString, FSWGPalette> Palettes;
	FSWGTextureCompositor::FInputs Inputs;
	Inputs.LoadImage = [&](const FString& Path) -> const FSWGImage*
	{
		if (!Images.Contains(Path))
		{
			FSWGImage& Image = Images.Add(Path);
			FSWGDdsDecoder::Decode(Tre->ExtractFile(Path), Image);
		}
		return Images[Path].IsValid() ? &Images[Path] : nullptr;
	};
	Inputs.LoadPalette = [&](const FString& Path) -> const FSWGPalette*
	{
		if (!Palettes.Contains(Path))
		{
			FSWGPalette::Parse(Tre->ExtractFile(Path), Palettes.Add(Path));
		}
		return Palettes[Path].Colors.IsEmpty() ? nullptr : &Palettes[Path];
	};
	Inputs.GetVariable = [](const FString&) { return 0; };

	const FSWGImage* Skin0 = Inputs.LoadImage(TEXT("texture/hum_m_face.dds"));
	const FSWGPalette* Palette0 = Inputs.LoadPalette(TEXT("palette/pc_skin_hum.pal"));
	if (TestNotNull(TEXT("face texture decodes"), Skin0) && TestNotNull(TEXT("skin palette parses"), Palette0))
	{
		TestEqual(TEXT("face texture width"), Skin0->Width, 128);
		TestEqual(TEXT("face texture height"), Skin0->Height, 256);
		FSWGImage Out;
		TestTrue(TEXT("composes"), FSWGTextureCompositor::Compose(Renderer, Inputs, Out));
		TestEqual(TEXT("output width"), Out.Width, 128);
		TestEqual(TEXT("output height"), Out.Height, 256);

		// Pixel (0, 0) is outside the brow and beard quads, and the freckle / mask layers are
		// transparent there, so it is the skin texture times skin palette entry 0.
		const FColor Texel = Skin0->Pixels[0];
		const FColor Tint = Palette0->Colors[0];
		const FColor Result = Out.Pixels[0];
		TestTrue(TEXT("skin pixel is texture times palette"),
			FMath::Abs((int32)Result.R - (int32)FMath::RoundToInt(Texel.R * Tint.R / 255.f)) <= 2
			&& FMath::Abs((int32)Result.G - (int32)FMath::RoundToInt(Texel.G * Tint.G / 255.f)) <= 2
			&& FMath::Abs((int32)Result.B - (int32)FMath::RoundToInt(Texel.B * Tint.B / 255.f)) <= 2);
	}
	return true;
}

#endif
