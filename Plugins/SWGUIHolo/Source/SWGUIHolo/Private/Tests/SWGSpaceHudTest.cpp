#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SWGSpaceHudWidget.h"
#include "SWGSpaceHudAtlas.h"
#include "SWGHoloRadarActor.h"
#include "TRE/SWGCrc32.h"
#include "SWGHoloShipActor.h"
#include "SWGHudWidget.h"
#include "SWGUISubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Common/SWGWorldScale.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Objects/Player/SWGPlayer.h"
#include "Objects/World/SWGShip.h"
#include "Components/SWGTangibleComponent.h"
#include "Slate/WidgetRenderer.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Styling/CoreStyle.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "MaterialShared.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	constexpr double LayoutTolerance = 0.51;


	float ScalarOf(UMaterialInstanceDynamic* Material, const TCHAR* Name)
	{
		return Material ? Material->K2_GetScalarParameterValue(Name) : -999.f;
	}

	/** Position and size of a widget placed in a canvas, as the HUD laid it out. */
	FBox2D CanvasRect(const UWidget* Widget)
	{
		const UCanvasPanelSlot* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
		return Slot ? FBox2D(Slot->GetPosition(), Slot->GetPosition() + Slot->GetSize()) : FBox2D(ForceInit);
	}

	FVector2D Centre(const FBox2D& Box) { return (Box.Min + Box.Max) * 0.5; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceHudTest, "SWG.Space.SpaceHud", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpaceHudTest::RunTest(const FString& Parameters)
{
	// ── Pure layout, projection and intercept maths ─────────────────────────
	const FVector2D ViewSizes[] = {FVector2D(1920, 1080), FVector2D(2560, 1080), FVector2D(3440, 1440), FVector2D(3840, 2160)};
	for (const FVector2D& ViewSize : ViewSizes)
	{
		const FSWGSpaceHudLayout Layout = SWGSpaceHud::ComputeLayout(ViewSize);
		for (int32 Index = 0; Index < int32(ESWGSpaceHudSlot::Count); ++Index)
		{
			const FBox2D& Box = Layout.Slots[Index];
			const FString Label = FString::Printf(TEXT("%dx%d slot %d"), int32(ViewSize.X), int32(ViewSize.Y), Index);
			TestTrue(*(Label + TEXT(" lies inside the safe area")), Box.Min.X >= Layout.Safe.Min.X - LayoutTolerance && Box.Min.Y >= Layout.Safe.Min.Y - LayoutTolerance
				&& Box.Max.X <= Layout.Safe.Max.X + LayoutTolerance && Box.Max.Y <= Layout.Safe.Max.Y + LayoutTolerance);
			for (int32 Other = Index + 1; Other < int32(ESWGSpaceHudSlot::Count); ++Other)
			{
				TestFalse(*FString::Printf(TEXT("%s does not overlap slot %d"), *Label, Other), Box.Intersect(Layout.Slots[Other]));
			}
		}
		TestTrue(TEXT("the radar sits bottom centre"), FMath::Abs(Centre(Layout.GetSlot(ESWGSpaceHudSlot::Radar)).X - ViewSize.X * 0.5) < 1.0
			&& Layout.GetSlot(ESWGSpaceHudSlot::Radar).Min.Y > ViewSize.Y * 0.5);
		TestTrue(TEXT("the target panel is right and the status panel left"), Layout.GetSlot(ESWGSpaceHudSlot::Target).Min.X > ViewSize.X * 0.5
			&& Layout.GetSlot(ESWGSpaceHudSlot::Status).Max.X < ViewSize.X * 0.5);
	}


	// Condition colours: retail-like green / amber / red by fraction, so a healthy 95% never shows red.
	TestTrue(TEXT("95% is in the good band"), SWGSpaceHud::ClassifyCondition(0.95f) == SWGSpaceHud::EConditionBand::Good);
	TestTrue(TEXT("50% is the lowest good value"), SWGSpaceHud::ClassifyCondition(0.5f) == SWGSpaceHud::EConditionBand::Good);
	TestTrue(TEXT("49% is a warning"), SWGSpaceHud::ClassifyCondition(0.49f) == SWGSpaceHud::EConditionBand::Warning);
	TestTrue(TEXT("25% is the lowest warning"), SWGSpaceHud::ClassifyCondition(0.25f) == SWGSpaceHud::EConditionBand::Warning);
	TestTrue(TEXT("24% is critical"), SWGSpaceHud::ClassifyCondition(0.24f) == SWGSpaceHud::EConditionBand::Critical);
	const FLinearColor Healthy = SWGSpaceHud::GetConditionColor(0.95f), Worn = SWGSpaceHud::GetConditionColor(0.35f), Dying = SWGSpaceHud::GetConditionColor(0.1f);
	TestTrue(TEXT("a healthy value is green"), Healthy.G > Healthy.R * 1.5f && Healthy.G > Healthy.B * 3.f);
	TestTrue(TEXT("a worn value is amber"), Worn.R > .9f && Worn.G > .3f && Worn.G < .8f && Worn.B < .1f);
	TestTrue(TEXT("a critical value is red"), Dying.R > Dying.G * 3.f && Dying.R > Dying.B * 3.f);
	FSWGSpaceHudView View;
	const FSWGSpaceHudProjection Ahead = SWGSpaceHud::Project(View, FVector(5000, 0, 0));
	TestTrue(TEXT("a point dead ahead projects to the centre"), Ahead.bOnScreen && Ahead.Position.Equals(FVector2D(960, 540), 0.1));
	const FSWGSpaceHudProjection EdgeOfView = SWGSpaceHud::Project(View, FVector(1000, 1000, 0));
	TestTrue(TEXT("a point at half the horizontal field of view lands on the screen edge"), EdgeOfView.bOnScreen && FMath::IsNearlyEqual(float(EdgeOfView.Position.X), 1920.f, 0.5f));
	const FSWGSpaceHudProjection Behind = SWGSpaceHud::Project(View, FVector(-1000, 500, 0));
	TestTrue(TEXT("a point behind the camera is off screen and points the way to turn"), !Behind.bOnScreen && Behind.Direction.X > 0.9);
	const FSWGSpaceHudProjection Above = SWGSpaceHud::Project(View, FVector(1000, 0, 5000));
	TestTrue(TEXT("a point far above is off screen with an upward direction"), !Above.bOnScreen && Above.Direction.Y < -0.9);

	const FVector Intercept = SWGSpaceHud::InterceptPoint(FVector(10000, 0, 0), FVector(0, 1000, 0), 5000.f);
	const double InterceptTime = Intercept.Y / 1000.0;
	TestTrue(TEXT("the intercept is reached by the projectile and the target at the same time"), InterceptTime > 0.0 && FMath::IsNearlyEqual(Intercept.Size() / 5000.0, InterceptTime, 0.001));
	TestTrue(TEXT("without a projectile speed the intercept is the target itself"), SWGSpaceHud::InterceptPoint(FVector(10000, 0, 0), FVector(0, 1000, 0), 0.f).Equals(FVector(10000, 0, 0)));
	TestTrue(TEXT("a target faster than the projectile and running away has no intercept, so the target stays"),
		SWGSpaceHud::InterceptPoint(FVector(10000, 0, 0), FVector(8000, 0, 0), 1000.f).Equals(FVector(10000, 0, 0)));

	// ── Retail data and widget ──────────────────────────────────────────────
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->InitializeStandalone();
	UWorld* World = Game->GetWorld();
	ON_SCOPE_EXIT { Game->Shutdown(); GEngine->DestroyWorldContext(World); };
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	USWGObjectGraphSubsystem* Graph = Game->GetSubsystem<USWGObjectGraphSubsystem>();
	if (!Tre || !Graph) return false;
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives load"), Tre->LoadArchives())) return false;
	if (!TestTrue(TEXT("the HUD can be built: materials generated and sprite sheets present"), USWGSpaceHudWidget::CanBuild(Game))) return false;

	// Atlas crops: every sprite the HUD draws has pixels in its rectangle.
	FSWGSpaceHudAtlas Atlas(Tre);
	TArray<TPair<FString, FSWGSpaceHudSprite>> Sprites;
	for (const TCHAR* Name : {TEXT("GlowLeft"), TEXT("GlowLeftBack"), TEXT("GlowRight"), TEXT("GlowRightBack"), TEXT("OffScreenArrow"), TEXT("TargetLead"), TEXT("TargetAim")})
	{
		FSWGSpaceHudSprite Sprite;
		if (TestTrue(*FString::Printf(TEXT("sprite %s resolves"), Name), Atlas.Find(Name, Sprite))) Sprites.Emplace(Name, Sprite);
	}
	for (const TCHAR* Piece : {TEXT("nw"), TEXT("ne"), TEXT("sw"), TEXT("se"), TEXT("n"), TEXT("s"), TEXT("w"), TEXT("e")})
	{
		FSWGSpaceHudSprite Sprite;
		if (TestTrue(*FString::Printf(TEXT("reticle piece e.%s resolves from ui_styles.inc"), Piece), Atlas.FindReticlePiece(TEXT("e"), Piece, Sprite)))
		{
			// The edge strips of this style are blank in the sheet, so only its corners must have pixels.
			if (FCString::Strlen(Piece) == 2) Sprites.Emplace(FString::Printf(TEXT("reticle e.%s"), Piece), Sprite);
		}
	}
	for (const TPair<FString, FSWGSpaceHudSprite>& Entry : Sprites)
	{
		TestTrue(*FString::Printf(TEXT("crop %s is non-empty"), *Entry.Key), Atlas.CountVisibleTexels(Entry.Value) > 0);
		const FVector4f Uv = Atlas.GetUvRect(Entry.Value);
		TestTrue(*FString::Printf(TEXT("crop %s UVs are inside the texture"), *Entry.Key), Uv.X >= 0.f && Uv.Y >= 0.f && Uv.Z <= 1.f && Uv.W <= 1.f);
	}
	FSWGSpaceHudSprite RightGlow, LeftGlow;
	Atlas.Find(TEXT("GlowRight"), RightGlow); Atlas.Find(TEXT("GlowLeft"), LeftGlow);
	TestTrue(TEXT("a reversed retail rectangle is mirrored"), RightGlow.bMirrorX && !LeftGlow.bMirrorX && Atlas.GetUvRect(RightGlow).X > Atlas.GetUvRect(RightGlow).Z);
	TestTrue(TEXT("the glow crop is a real sprite, not a stray texel"), Atlas.CountVisibleTexels(LeftGlow) > 200);
	TestNotNull(TEXT("the atlas texture loads"), Atlas.LoadTexture(LeftGlow));

#if WITH_EDITOR
	// The materials need shaders for this RHI before the first draw. Only incomplete ones are compiled.
	for (const TCHAR* Path : {USWGSpaceHudWidget::SpriteMaterialPath, USWGSpaceHudWidget::ArcMaterialPath})
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Path);
		FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
		if (Resource && !Resource->IsGameThreadShaderMapComplete()) Resource->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
	}
	FAssetCompilingManager::Get().FinishAllCompilation();
#endif

	USWGSpaceHudWidget* Hud = CreateWidget<USWGSpaceHudWidget>(Game, USWGSpaceHudWidget::StaticClass());
	if (!TestNotNull(TEXT("space HUD widget builds"), Hud)) return false;
	const TSharedRef<SWidget> HudSlate = Hud->TakeWidget();
	if (!TestNotNull(TEXT("the reticle bracket drew"), Hud->GetReticleBracketImage()) || !TestNotNull(TEXT("lead mark built"), Hud->GetLeadImage()) || !TestNotNull(TEXT("aim mark built"), Hud->GetAimImage()) || !TestNotNull(TEXT("arrow built"), Hud->GetArrowImage())) return false;
	for (ESWGSpaceHudGauge Gauge : {ESWGSpaceHudGauge::Speed, ESWGSpaceHudGauge::Weapon, ESWGSpaceHudGauge::Booster})
	{
		if (!TestNotNull(TEXT("gauge arc built"), Hud->GetGaugeImage(Gauge)) || !TestNotNull(TEXT("gauge text built"), Hud->GetGaugeText(Gauge))) return false;
	}
	TestTrue(TEXT("the HUD ignores hits itself (only the comm and eject buttons take them) so CommonUI input and mouse capture are untouched"), Hud->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);

	// Layout inside the safe area at 16:9 and 21:9, for every element the HUD drew.
	for (const FVector2D& ViewSize : {FVector2D(1920, 1080), FVector2D(2560, 1080), FVector2D(3440, 1440)})
	{
		Hud->UpdateLayout(ViewSize);
		const FSWGSpaceHudLayout& Layout = Hud->GetLayout();
		const FString Size = FString::Printf(TEXT("%dx%d"), int32(ViewSize.X), int32(ViewSize.Y));
		for (int32 Index = 0; Index < int32(ESWGSpaceHudSlot::Count); ++Index)
		{
			const FBox2D PanelRect = CanvasRect(Hud->GetSlotPanel(ESWGSpaceHudSlot(Index)));
			TestTrue(*FString::Printf(TEXT("%s slot panel %d matches the layout"), *Size, Index), PanelRect.Min.Equals(Layout.Slots[Index].Min, 0.01) && PanelRect.Max.Equals(Layout.Slots[Index].Max, 0.01));
		}
		int32 Checked = 0;
		for (const FSWGSpaceHudElement& Element : Hud->GetElements())
		{
			if (!Element.RetailRect.bIsValid) continue;
			++Checked;
			const FBox2D Box = Element.ScreenRect;
			if (!(Box.Min.X >= Layout.Safe.Min.X && Box.Min.Y >= Layout.Safe.Min.Y && Box.Max.X <= Layout.Safe.Max.X && Box.Max.Y <= Layout.Safe.Max.Y))
			{
				AddError(FString::Printf(TEXT("%s: a reticle element lies outside the safe area (%.0f,%.0f)-(%.0f,%.0f)"), *Size, Box.Min.X, Box.Min.Y, Box.Max.X, Box.Max.Y));
			}
			const FBox2D& Reticle = Layout.GetSlot(ESWGSpaceHudSlot::Reticle);
			if (!(Box.Min.X >= Reticle.Min.X - LayoutTolerance && Box.Max.X <= Reticle.Max.X + LayoutTolerance && Box.Min.Y >= Reticle.Min.Y - LayoutTolerance && Box.Max.Y <= Reticle.Max.Y + LayoutTolerance))
			{
				AddError(FString::Printf(TEXT("%s: a reticle element lies outside the reticle slot"), *Size));
			}
		}
		TestTrue(*(Size + TEXT(" has reticle elements to check")), Checked > 20);
		for (const FSWGSpaceHudText& Entry : Hud->GetTexts())
		{
			const FBox2D Box = FBox2D(Layout.GetSlot(ESWGSpaceHudSlot::Reticle).Min + Entry.RetailRect.Min * Layout.GetReticleScale(), Layout.GetSlot(ESWGSpaceHudSlot::Reticle).Min + Entry.RetailRect.Max * Layout.GetReticleScale());
			TestTrue(*(Size + TEXT(" text lies inside the safe area")), Box.Min.X >= Layout.Safe.Min.X && Box.Max.X <= Layout.Safe.Max.X && Box.Min.Y >= Layout.Safe.Min.Y && Box.Max.Y <= Layout.Safe.Max.Y);
		}
		TestTrue(*(Size + TEXT(" the reticle is centred")), Centre(Layout.GetSlot(ESWGSpaceHudSlot::Reticle)).Equals(ViewSize * 0.5, 0.01));
	}
	Hud->UpdateLayout(FVector2D(1920, 1080));
	TestTrue(TEXT("the radar sphere is at least 90 px in radius at 1080p"), Hud->RadarSpherePixels >= 90.f);

	// Elements with no data stay hidden.
	Hud->SetData(FSWGSpaceHudData());
	for (ESWGSpaceHudGauge Gauge : {ESWGSpaceHudGauge::Speed, ESWGSpaceHudGauge::Weapon, ESWGSpaceHudGauge::Booster})
	{
		TestEqual(TEXT("a gauge with no data is collapsed"), Hud->GetGaugeImage(Gauge)->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("a gauge's text with no data is collapsed"), Hud->GetGaugeText(Gauge)->GetVisibility(), ESlateVisibility::Collapsed);
	}
	TestEqual(TEXT("no weapon indicators without weapon data"), Hud->GetVisibleWeaponIndicators(), 0);
	TestEqual(TEXT("no lead mark without a target"), Hud->GetLeadImage()->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("no off-screen arrow without a target"), Hud->GetArrowImage()->GetVisibility(), ESlateVisibility::Collapsed);

	// Partial data: speed and booster only.
	FSWGSpaceHudData Partial;
	Partial.Speed = 40.f; Partial.MaxSpeed = 80.f;
	Partial.Booster = 25.f; Partial.BoosterMax = 100.f;
	Hud->SetData(Partial);
	TestNotEqual(TEXT("speed shows with speed data"), Hud->GetGaugeImage(ESWGSpaceHudGauge::Speed)->GetVisibility(), ESlateVisibility::Collapsed);
	TestNotEqual(TEXT("booster shows with booster data"), Hud->GetGaugeImage(ESWGSpaceHudGauge::Booster)->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("the weapon (capacitor) gauge stays hidden without capacitor data"), Hud->GetGaugeImage(ESWGSpaceHudGauge::Weapon)->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("speed gauge fill is speed over max"), ScalarOf(Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Speed), TEXT("Fill")), .5f);
	TestEqual(TEXT("no throttle data means no tick"), ScalarOf(Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Speed), TEXT("TickAt")), -1.f);
	TestTrue(TEXT("no boost data means no overdrive region"), ScalarOf(Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Speed), TEXT("OverdriveFrom")) >= 1.f);
	TestEqual(TEXT("booster gauge fill is energy over max"), ScalarOf(Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Booster), TEXT("Fill")), .25f);

	// Boost data adds the overdrive region and puts the throttle tick on the longer scale.
	Partial.BoostMaxSpeed = 160.f; Partial.Throttle = .5f; Partial.Speed = 120.f;
	Hud->SetData(Partial);
	UMaterialInstanceDynamic* SpeedMaterial = Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Speed);
	TestEqual(TEXT("with boost the arc spans to the boost top speed"), ScalarOf(SpeedMaterial, TEXT("Fill")), .75f);
	TestEqual(TEXT("overdrive starts at max speed"), ScalarOf(SpeedMaterial, TEXT("OverdriveFrom")), .5f);
	TestEqual(TEXT("the tick marks the throttle on the same scale"), ScalarOf(SpeedMaterial, TEXT("TickAt")), .25f);

	// ── The sweep-in ────────────────────────────────────────────────────────
	Hud->StartIntro();
	Hud->TickHud(0.f);
	float BracketReveal = ScalarOf(Cast<UMaterialInstanceDynamic>(Hud->GetElements()[0].Material), TEXT("Reveal"));
	TestEqual(TEXT("everything starts unrevealed"), BracketReveal, 0.f);
	Hud->TickHud(0.25f);
	const float EarlyReveal = Hud->GetElements()[0].Reveal;
	Hud->TickHud(0.1f);
	TestTrue(TEXT("pieces sweep in over time"), EarlyReveal > 0.f && EarlyReveal < 1.f && Hud->GetElements()[0].Reveal > EarlyReveal);
	TestEqual(TEXT("a later piece has not started when the first is under way"), Hud->GetElements().Last().Reveal, 0.f);
	Hud->TickHud(3.f);
	for (const FSWGSpaceHudElement& Element : Hud->GetElements()) TestEqual(TEXT("every piece is revealed when the intro ends"), Element.Reveal, 1.f);
	Hud->PulseElement(Hud->GetGaugeImage(ESWGSpaceHudGauge::Booster));
	Hud->TickHud(0.1f);
	TestTrue(TEXT("a pulse brightens the element and then decays"), ScalarOf(Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Booster), TEXT("Pulse")) > 0.5f);
	Hud->TickHud(1.f);
	TestEqual(TEXT("the pulse has decayed"), ScalarOf(Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Booster), TEXT("Pulse")), 0.f);

	// ── Baseline-driven data from a piloted ship ────────────────────────────
	Graph->LocalPlayerObjectId = 103;
	Graph->bSpaceScene = true;
	Graph->ContainmentTypeByObjectId.Add(103, int32(ESWGContainmentType::ShipPilot));
	ASWGShip* Ship = World->SpawnActor<ASWGShip>();
	ASWGPlayer* Pilot = World->SpawnActor<ASWGPlayer>();
	ASWGShip* Target = World->SpawnActor<ASWGShip>();
	if (!TestNotNull(TEXT("ship fixture"), Ship) || !TestNotNull(TEXT("pilot fixture"), Pilot) || !TestNotNull(TEXT("target fixture"), Target)) return false;
	Graph->RegisterStaticObject(100, Ship);
	Graph->RegisterStaticObject(103, Pilot, 100);
	Graph->RegisterStaticObject(200, Target);
	Ship->bHasFlightBaseline = true;
	FShipObjectBaseline& State = Ship->ShipState;
	State.Flight.MaxSpeed = 100.f;
	Ship->SpaceVelocity = FVector(50 * SWGWorldScale, 0, 0);

	FSWGSpaceHudData Gathered;
	TestTrue(TEXT("a piloted ship gathers"), FSWGSpaceHudData::Gather(*Graph, Gathered));
	TestEqual(TEXT("speed comes from the ship's velocity in metres"), *Gathered.Speed, 50.f);
	TestEqual(TEXT("max speed comes from the flight baseline"), *Gathered.MaxSpeed, 100.f);
	TestFalse(TEXT("no capacitor data before baseline 1 and 4 arrive"), Gathered.Capacitor.IsSet());
	TestFalse(TEXT("no booster data before baseline 1 and 4 arrive"), Gathered.Booster.IsSet());
	TestFalse(TEXT("no boost top speed before baseline 1"), Gathered.BoostMaxSpeed.IsSet());
	TestTrue(TEXT("no weapons before the component baselines"), Gathered.Weapons.IsEmpty());
	TestFalse(TEXT("no target without a target id"), Gathered.bHasTarget);
	Hud->SetData(Gathered);
	TestEqual(TEXT("the speed gauge follows the baseline speed"), ScalarOf(Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Speed), TEXT("Fill")), .5f);
	TestEqual(TEXT("the capacitor gauge stays hidden without baseline data"), Hud->GetGaugeImage(ESWGSpaceHudGauge::Weapon)->GetVisibility(), ESlateVisibility::Collapsed);

	State.bHasBase1 = State.bHasBase3 = State.bHasBase4 = true;
	State.OwnerValues[2] = 1000.f;   // capacitor max energy (Core3 field 19)
	State.OwnerValues[14] = 200.f;   // booster max energy (field 31)
	State.OwnerValues[18] = 150.f;   // booster max speed (field 35)
	State.CapacitorEnergy = 250.f;
	State.BoosterEnergy = 150.f;
	State.OwnerComponentFloatMaps[4].Items = {TPair<uint32, float>(5, 12.f), TPair<uint32, float>(3, 10.f)};   // max damage: only weapons have entries
	State.HitpointsCurrent.Items = {TPair<uint32, float>(3, 50.f), TPair<uint32, float>(5, 0.f), TPair<uint32, float>(7, 10.f)};
	State.HitpointsMax.Items = {TPair<uint32, float>(3, 100.f), TPair<uint32, float>(5, 100.f), TPair<uint32, float>(7, 10.f)};
	Pilot->ShipThrottle = 0.5f;
	TestTrue(TEXT("a piloted ship with baselines gathers"), FSWGSpaceHudData::Gather(*Graph, Gathered));
	TestEqual(TEXT("capacitor energy comes from baseline 4"), *Gathered.Capacitor, 250.f);
	TestEqual(TEXT("capacitor max comes from baseline 1"), *Gathered.CapacitorMax, 1000.f);
	TestEqual(TEXT("booster max speed comes from baseline 1"), *Gathered.BoostMaxSpeed, 150.f);
	TestEqual(TEXT("two weapons are indicated, none for non-weapon components"), Gathered.Weapons.Num(), 2);
	TestTrue(TEXT("weapons are in slot order with their condition"), Gathered.Weapons[0].Slot == 3 && Gathered.Weapons[0].Condition == .5f && Gathered.Weapons[1].Slot == 5 && Gathered.Weapons[1].Condition == 0.f);
	Hud->SetData(Gathered);
	TestEqual(TEXT("the capacitor gauge follows the baseline"), ScalarOf(Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Weapon), TEXT("Fill")), .25f);
	TestEqual(TEXT("the booster gauge follows the baseline"), ScalarOf(Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Booster), TEXT("Fill")), .75f);
	TestTrue(TEXT("the capacitor text shows its percentage"), Hud->GetGaugeText(ESWGSpaceHudGauge::Weapon)->GetText().ToString().Contains(TEXT("25")));
	TestEqual(TEXT("the speed arc spans to the booster's speed"), ScalarOf(Hud->GetGaugeMaterial(ESWGSpaceHudGauge::Speed), TEXT("Fill")), 50.f / 150.f);
	TestEqual(TEXT("two weapon indicators show"), Hud->GetVisibleWeaponIndicators(), 2);
	State.BoosterEnergy = 0.f; State.OwnerValues[14] = 0.f;
	FSWGSpaceHudData NoBooster;
	FSWGSpaceHudData::Gather(*Graph, NoBooster);
	Hud->SetData(NoBooster);
	TestEqual(TEXT("a ship with no booster hides the booster gauge"), Hud->GetGaugeImage(ESWGSpaceHudGauge::Booster)->GetVisibility(), ESlateVisibility::Collapsed);
	State.OwnerValues[14] = 200.f; State.BoosterEnergy = 150.f;

	// Target marks. The camera sits at the ship looking down +X; the target is 100 m dead ahead.
	FSWGSpaceHudView Camera;
	Camera.Location = FVector::ZeroVector; Camera.Rotation = FRotator::ZeroRotator; Camera.HorizontalFovDegrees = 90.f;
	Hud->SetViewOverride(Camera);
	State.TargetId = 200;
	Target->SetActorLocation(FVector(100 * SWGWorldScale, 0, 0));
	FSWGSpaceHudData WithTarget;
	TestTrue(TEXT("gathers with a target"), FSWGSpaceHudData::Gather(*Graph, WithTarget) && WithTarget.bHasTarget);
	Hud->SetData(WithTarget);
	TestNotEqual(TEXT("the lead mark shows for an on-screen target"), Hud->GetLeadImage()->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("no arrow while the target is on screen"), Hud->GetArrowImage()->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("the lead mark sits on a target dead ahead without a projectile speed"), Centre(CanvasRect(Hud->GetLeadImage())).Equals(FVector2D(960, 540), 1.0));
	TestNotEqual(TEXT("the aim mark shows"), Hud->GetAimImage()->GetVisibility(), ESlateVisibility::Collapsed);

	// A moving target with a projectile speed: the lead mark leaves the target.
	Target->SpaceVelocity = FVector(0, 20 * SWGWorldScale, 0);
	IConsoleVariable* ProjectileSpeed = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.HudProjectileSpeed"));
	if (TestNotNull(TEXT("projectile speed cvar exists"), ProjectileSpeed))
	{
		const FString PreviousSpeed = ProjectileSpeed->GetString();
		ProjectileSpeed->Set(TEXT("200"));
		FSWGSpaceHudData Moving;
		FSWGSpaceHudData::Gather(*Graph, Moving);
		TestTrue(TEXT("the projectile speed reaches the data"), Moving.ProjectileSpeed.IsSet() && *Moving.ProjectileSpeed == 200.f);
		Hud->SetData(Moving);
		const FVector2D Moved = Centre(CanvasRect(Hud->GetLeadImage()));
		TestTrue(TEXT("the lead mark moves ahead of a crossing target"), Moved.X > 960.0 + 20.0);
		ProjectileSpeed->Set(*PreviousSpeed);
	}

	// Off-screen: far to the right, then behind.
	Target->SetActorLocation(FVector(10 * SWGWorldScale, 500 * SWGWorldScale, 0));
	Target->SpaceVelocity = FVector::ZeroVector;
	FSWGSpaceHudData OffScreen;
	FSWGSpaceHudData::Gather(*Graph, OffScreen);
	Hud->SetData(OffScreen);
	TestEqual(TEXT("the lead mark hides when the target is off screen"), Hud->GetLeadImage()->GetVisibility(), ESlateVisibility::Collapsed);
	TestNotEqual(TEXT("an arrow shows for an off-screen target"), Hud->GetArrowImage()->GetVisibility(), ESlateVisibility::Collapsed);
	const FBox2D ArrowBox = CanvasRect(Hud->GetArrowImage());
	TestTrue(TEXT("the arrow sits on the right edge of the safe area"), ArrowBox.Max.X > Hud->GetLayout().Safe.Max.X - 100.0 && ArrowBox.Max.X <= Hud->GetLayout().Safe.Max.X);
	TestTrue(TEXT("the arrow points right toward the target"), FMath::Abs(Hud->GetArrowImage()->GetRenderTransformAngle()) < 5.f);
	Target->SetActorLocation(FVector(-300 * SWGWorldScale, -10 * SWGWorldScale, 0));
	FSWGSpaceHudData Behind2;
	FSWGSpaceHudData::Gather(*Graph, Behind2);
	Hud->SetData(Behind2);
	TestNotEqual(TEXT("an arrow shows for a target behind"), Hud->GetArrowImage()->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("the arrow for a target behind and to the left points left"), FMath::Abs(Hud->GetArrowImage()->GetRenderTransformAngle()) > 150.f);

	// ── 6c-6e: radar, target panel, status and warnings ─────────────────────
	{
		Hud->SetViewOverride(Camera);
		Target->SetActorLocation(FVector(100 * SWGWorldScale, 0, 0));
		Target->SpaceVelocity = FVector::ZeroVector;
		ASWGShip* Wing = World->SpawnActor<ASWGShip>();
		Graph->RegisterStaticObject(300, Wing);
		Graph->SpaceObjectPoses.Add(200, FTransform::Identity);
		Graph->SpaceObjectPoses.Add(300, FTransform::Identity);
		// Ahead 500 m, left 300 m, above 200 m of the ship.
		Wing->SetActorLocation(FVector(500, -300, 200) * SWGWorldScale);
		Wing->ShipState.Faction = TEXT("imperial");
		State.Faction = TEXT("rebel");
		Target->ShipState.Faction = TEXT("rebel");

		// Shields, armor, chassis and components from the baselines.
		State.FrontShield = 50.f; State.MaxFrontShield = 200.f; State.RearShield = 100.f; State.MaxRearShield = 100.f;
		State.ArmorCurrent.Items = {TPair<uint32, float>(4, 30.f), TPair<uint32, float>(5, 90.f)};
		State.ArmorMax.Items = {TPair<uint32, float>(4, 60.f), TPair<uint32, float>(5, 90.f)};
		State.ChassisHealth = 500.f; State.ChassisMaxHealth = 1000.f;
		State.HitpointsCurrent.Items = {TPair<uint32, float>(0, 80.f), TPair<uint32, float>(1, 20.f), TPair<uint32, float>(6, 100.f), TPair<uint32, float>(12, 40.f), TPair<uint32, float>(13, 60.f)};
		State.HitpointsMax.Items = {TPair<uint32, float>(0, 100.f), TPair<uint32, float>(1, 100.f), TPair<uint32, float>(6, 100.f), TPair<uint32, float>(12, 100.f), TPair<uint32, float>(13, 100.f)};
		State.TargetId = 200;
		Target->ShipState.FrontShield = 25.f; Target->ShipState.MaxFrontShield = 100.f;
		Target->ShipState.TypeName = TEXT("Z-95 Headhunter"); Target->ShipState.Difficulty = TEXT("normal"); Target->ShipState.WingName = TEXT("Rogue One");
		Target->ShipState.ChassisHealth = 90.f; Target->ShipState.ChassisMaxHealth = 100.f;

		const uint32 MissileCrc = FSWGCrc32::HashString(TEXT("object/tangible/ship/components/weapon/shared_wpn_incom_seismic_missile_s01.iff"));
		const uint32 CountermeasureCrc = FSWGCrc32::HashString(TEXT("object/tangible/ship/components/weapon/shared_wpn_std_countermeasure.iff"));
		{ int32 DebugType = 0; const bool bDebugFound = Tre->FindTemplateIntParam(Tre->ResolveTemplatePath(MissileCrc), TEXT("gameObjectType"), DebugType); AddInfo(FString::Printf(TEXT("launcher gameObjectType found=%d value=%x path=%s"), bDebugFound, DebugType, *Tre->ResolveTemplatePath(MissileCrc))); }
		TestTrue(TEXT("the launcher templates resolve from their CRCs"), !Tre->ResolveTemplatePath(MissileCrc).IsEmpty() && !Tre->ResolveTemplatePath(CountermeasureCrc).IsEmpty());
		TestTrue(TEXT("the archives hold a missile launcher and a countermeasure launcher template"), MissileCrc && CountermeasureCrc);
		State.ComponentCrcs.Items = {TPair<uint32, uint32>(12, MissileCrc), TPair<uint32, uint32>(13, CountermeasureCrc)};
		State.AmmoMaps[0].Items = {TPair<uint32, uint32>(12, 7), TPair<uint32, uint32>(13, 25)};
		State.AmmoMaps[1].Items = {TPair<uint32, uint32>(12, 10), TPair<uint32, uint32>(13, 30)};

		FSWGSpaceHudData Full;
		TestTrue(TEXT("gathers the full ship"), FSWGSpaceHudData::Gather(*Graph, Full));
		TestTrue(TEXT("front shield follows the baseline"), Full.Vitals.FrontShield.IsSet() && FMath::IsNearlyEqual(*Full.Vitals.FrontShield, 0.25f));
		TestTrue(TEXT("armor comes from the armor 0 and 1 slots"), FMath::IsNearlyEqual(*Full.Vitals.FrontArmor, 0.5f) && FMath::IsNearlyEqual(*Full.Vitals.RearArmor, 1.f));
		TestTrue(TEXT("chassis follows the baseline"), FMath::IsNearlyEqual(*Full.Vitals.Chassis, 0.5f));
		TestTrue(TEXT("reactor and engine conditions"), FMath::IsNearlyEqual(*Full.Components[int32(ESWGSpaceHudComponent::Reactor)], 0.8f) && FMath::IsNearlyEqual(*Full.Components[int32(ESWGSpaceHudComponent::Engine)], 0.2f));
		TestTrue(TEXT("weapon condition aggregates the weapon slots"), FMath::IsNearlyEqual(*Full.Components[int32(ESWGSpaceHudComponent::Weapon)], 0.5f));
		TestFalse(TEXT("a component with no hitpoints entry is hidden"), Full.Components[int32(ESWGSpaceHudComponent::Shield)].IsSet());
		TestEqual(TEXT("two launchers are recognised from their templates"), Full.Launchers.Num(), 2);
		if (Full.Launchers.Num() == 2)
		{
			TestTrue(TEXT("the missile launcher carries its count"), !Full.Launchers[0].bCountermeasure && Full.Launchers[0].Ammo == 7);
			TestTrue(TEXT("the countermeasure launcher carries its count"), Full.Launchers[1].bCountermeasure && Full.Launchers[1].Ammo == 25);
		}
		TestEqual(TEXT("target name falls back to the wing name"), Full.Target.Name, FString(TEXT("Rogue One")));
		TestTrue(TEXT("target values come from the target baselines"), *Full.Target.Vitals.FrontShield == 0.25f && FMath::IsNearlyEqual(*Full.Target.Vitals.Chassis, 0.9f));
		TestEqual(TEXT("a same-faction target is friendly"), Full.Target.Relation, ESWGSpaceHudRelation::Friendly);

		// Radar blips at their true bearings.
		const FSWGSpaceHudBlip* WingBlip = Full.Blips.FindByPredicate([](const FSWGSpaceHudBlip& Blip) { return Blip.ObjectId == 300; });
		if (TestNotNull(TEXT("the wingman is on the radar"), WingBlip))
		{
			TestTrue(TEXT("relative position is ahead, left and above"), WingBlip->Relative.Equals(FVector(500, -300, 200) * SWGWorldScale, 1.0));
			TestEqual(TEXT("a different faction is hostile"), WingBlip->Relation, ESWGSpaceHudRelation::Hostile);
			const FVector Sphere = SWGSpaceHud::RadarSpherePosition(WingBlip->Relative, Full.RadarRange);
			TestTrue(TEXT("sphere position is ahead, left and above"), Sphere.X > 0.0 && Sphere.Y < 0.0 && Sphere.Z > 0.0 && Sphere.Size() <= 1.0 + KINDA_SMALL_NUMBER);
			const FVector2D Disc = SWGSpaceHud::RadarDiscPosition(WingBlip->Relative, Full.RadarRange);
			TestTrue(TEXT("disc position puts ahead up the screen and left to the left"), Disc.Y < 0.0 && Disc.X < 0.0);
		}
		const FSWGSpaceHudBlip* TargetBlip = Full.Blips.FindByPredicate([](const FSWGSpaceHudBlip& Blip) { return Blip.bTarget; });
		TestTrue(TEXT("the target is a bracketed blip"), TargetBlip && TargetBlip->ObjectId == 200);
		TestTrue(TEXT("past the range a blip sits on the surface"), SWGSpaceHud::RadarSpherePosition(FVector(1e8, 0, 0), 1000.f).Equals(FVector(1, 0, 0), 1e-4));

		// The widget in 3D mode: the radar and target holograms spawn and the panels fill.
		IConsoleVariable* FlatCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.HudFlat"));
		FlatCvar->Set(0, ECVF_SetByCode);
		ON_SCOPE_EXIT { FlatCvar->Set(0, ECVF_SetByCode); };
		Hud->SetData(Full);
		Hud->TickHud(0.016f);
		if (TestNotNull(TEXT("3D mode spawns the radar hologram"), Hud->RadarActor.Get()) && TestNotNull(TEXT("3D mode spawns the target hologram"), Hud->TargetHologram.Get()))
		{
			ASWGHoloRadarActor* Radar = Hud->RadarActor;
			TestTrue(TEXT("radar materials are ready"), Radar->AreMaterialsReady());
			int32 WingIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Full.Blips.Num(); ++Index) if (Full.Blips[Index].ObjectId == 300) WingIndex = Index;
			const FVector WingSpot = Radar->GetBlipPosition(WingIndex);
			TestTrue(TEXT("the blip hologram is ahead, left and above"), WingSpot.X > 0.0 && WingSpot.Y < 0.0 && WingSpot.Z > 0.0);
			TestEqual(TEXT("a blip off the plane has a stalk to it and one on the plane has none"), Radar->GetStalkCount(), 1);
			TestEqual(TEXT("the hostile wingman draws in the hostile class"), Radar->GetBlipCount(ESWGRadarBlipClass::Hostile), 1);
			TestEqual(TEXT("the target draws in the target class"), Radar->GetBlipCount(ESWGRadarBlipClass::Target), 1);
			TestTrue(TEXT("the radar is placed in front of the camera"), Radar->GetCentreLocation().X > 0.0 && !Radar->IsHidden());
			const FVector2D Projected = SWGSpaceHud::Project(Camera, Radar->GetCentreLocation()).Position;
			TestTrue(TEXT("the sphere centre lands in the radar slot"), Projected.Equals(Hud->RadarSphereCentre, 2.0));
			TestEqual(TEXT("the target hologram follows the target"), Hud->TargetHologram->GetSource(), (const AActor*)Target);
			const FVector2D TargetProjected = SWGSpaceHud::Project(Camera, Hud->TargetHologram->GetActorLocation()).Position;
			TestTrue(TEXT("the target hologram sits in the target slot"), TargetProjected.X > Hud->GetLayout().GetSlot(ESWGSpaceHudSlot::Target).Min.X && TargetProjected.X < Hud->GetLayout().GetSlot(ESWGSpaceHudSlot::Target).Max.X);
		}
		// Shield and armor arcs follow the baselines: front on the top quadrants, rear on the bottom ones.
		TestEqual(TEXT("front shield arcs show 25 percent"), ScalarOf(Hud->ShieldArcs[0].Material, TEXT("Fill")), .25f);
		TestEqual(TEXT("rear shield arcs show 100 percent"), ScalarOf(Hud->ShieldArcs[2].Material, TEXT("Fill")), 1.f);
		TestEqual(TEXT("front armor arcs show 50 percent"), ScalarOf(Hud->ArmorArcs[1].Material, TEXT("Fill")), .5f);
		// Target panel texts.
		TestEqual(TEXT("target name"), Hud->TargetName->GetText().ToString(), FString(TEXT("Rogue One")));
		TestEqual(TEXT("target type"), Hud->TargetType->GetText().ToString(), FString(TEXT("Z-95 Headhunter")));
		TestTrue(TEXT("target shield percentage"), Hud->TargetRowValues[0]->GetText().ToString().Contains(TEXT("25")) && Hud->TargetRowValues[0]->GetVisibility() != ESlateVisibility::Collapsed);
		TestEqual(TEXT("target armor row hidden without armor baselines"), Hud->TargetRowValues[1]->GetVisibility(), ESlateVisibility::Collapsed);
		TestTrue(TEXT("target chassis percentage"), Hud->TargetRowValues[3]->GetText().ToString().Contains(TEXT("90")));
		TestNotEqual(TEXT("the comm button shows for a ship target"), Hud->CommButton->GetVisibility(), ESlateVisibility::Collapsed);
		// Status panel.
		TestEqual(TEXT("m1 count"), Hud->AmmoValues[0]->GetText().ToString(), FString(TEXT("7")));
		TestEqual(TEXT("cm count"), Hud->AmmoValues[2]->GetText().ToString(), FString(TEXT("25")));
		TestEqual(TEXT("m2 hidden with one missile launcher"), Hud->AmmoLabels[1]->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("engine bar follows the component map"), ScalarOf(Hud->ComponentRows[int32(ESWGSpaceHudComponent::Engine)].Bar.Material, TEXT("FillX")), .2f);
		TestEqual(TEXT("shield row hidden without hitpoint data"), Hud->ComponentRows[int32(ESWGSpaceHudComponent::Shield)].Label->GetVisibility(), ESlateVisibility::Collapsed);
		TestTrue(TEXT("energy level shows the capacitor"), Hud->EnergyText->GetVisibility() != ESlateVisibility::Collapsed);
		// Warnings: hidden until a missile is launched at the ship.
		TestEqual(TEXT("no missile warning by default"), Hud->IncomingMissileText->GetVisibility(), ESlateVisibility::Collapsed);
		Graph->NoteIncomingMissile(1, 300, 100, 5.0);
		FSWGSpaceHudData Locked;
		FSWGSpaceHudData::Gather(*Graph, Locked);
		TestEqual(TEXT("a CreateMissile at the ship counts"), Locked.IncomingMissiles, 1);
		Hud->SetData(Locked);
		TestNotEqual(TEXT("the incoming missile warning shows"), Hud->IncomingMissileText->GetVisibility(), ESlateVisibility::Collapsed);
		const double AlarmBefore = Hud->LastAlarmTime;
		Hud->TickHud(0.016f);
		TestTrue(TEXT("the alarm is raised"), Hud->LastAlarmTime > AlarmBefore);
		TestEqual(TEXT("a missile at another ship is not a warning"), Graph->CountIncomingMissiles(300, FPlatformTime::Seconds()), 0);
		Graph->IncomingMissiles.Reset();
		TestEqual(TEXT("no incapacitation page while upright"), Hud->IncapText->GetVisibility(), ESlateVisibility::Collapsed);
		FSWGSpaceHudData Down = Full;
		Down.bPilotIncapacitated = true;
		Hud->SetData(Down);
		TestNotEqual(TEXT("the incapacitation page shows when down"), Hud->IncapText->GetVisibility(), ESlateVisibility::Collapsed);
		TestNotEqual(TEXT("with the eject button"), Hud->EjectButton->GetVisibility(), ESlateVisibility::Collapsed);

		// Clicks: Comm and Eject send their server commands, and nothing else in the HUD takes a hit, so mouse look keeps working.
		if (USWGCommandSubsystem* Commands = Game->GetSubsystem<USWGCommandSubsystem>())
		{
			TArray<FString> SentCommands;
			const FDelegateHandle SentHandle = Commands->OnCommandSent.AddLambda([&SentCommands](const FString& Name, uint32) { SentCommands.Add(Name); });
			ON_SCOPE_EXIT { Commands->OnCommandSent.Remove(SentHandle); };
			Hud->SetData(Full);
			Hud->CommButton->OnClicked.Broadcast();
			if (TestEqual(TEXT("clicking Comm sends one command"), SentCommands.Num(), 1))
			{
				TestEqual(TEXT("the command is comm"), SentCommands[0], FString(TEXT("comm")));
				TestEqual(TEXT("comm targets the HUD's target ship"), Commands->GetQueuedCommands().Last().TargetId, int64(200));
			}
			Hud->SetData(Down);
			Hud->EjectButton->OnClicked.Broadcast();
			if (TestEqual(TEXT("clicking Eject sends a second command"), SentCommands.Num(), 2))
			{
				TestEqual(TEXT("the command is escapePod"), SentCommands[1], FString(TEXT("escapePod")));
				TestEqual(TEXT("escapePod has no target"), Commands->GetQueuedCommands().Last().TargetId, int64(0));
			}
			Hud->SetData(FSWGSpaceHudData());
			Hud->CommButton->OnClicked.Broadcast();
			TestEqual(TEXT("Comm does nothing with no target"), SentCommands.Num(), 2);

			// Hit-test audit over the whole widget tree, with the buttons shown.
			Hud->SetData(Down);
			TestNotEqual(TEXT("the Comm button takes hits where drawn"), Hud->CommButton->GetVisibility(), ESlateVisibility::Collapsed);
			TArray<FString> HitTestable;
			Hud->WidgetTree->ForEachWidget([&](UWidget* Widget)
			{
				if (Widget->GetVisibility() != ESlateVisibility::Visible) return;
				for (const UWidget* Ancestor = Widget; Ancestor; Ancestor = Ancestor->GetParent())
					if (Ancestor == Hud->CommButton || Ancestor == Hud->EjectButton) return;
				HitTestable.Add(Widget->GetName());
			});
			TestEqual(TEXT("no widget outside the two buttons is hit-testable"), HitTestable.Num(), 0);
			if (!HitTestable.IsEmpty()) AddError(FString::Printf(TEXT("hit-testable widgets: %s"), *FString::Join(HitTestable, TEXT(", "))));
			TestEqual(TEXT("the HUD root still lets hits through to the world"), Hud->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
		}

		// Flat mode: no holograms, the disc and blips instead.
		FlatCvar->Set(1, ECVF_SetByCode);
		Hud->SetData(Full);
		TestNull(TEXT("flat mode has no radar hologram"), Hud->RadarActor.Get());
		TestNull(TEXT("flat mode has no target hologram"), Hud->TargetHologram.Get());
		TestNotEqual(TEXT("flat mode draws the disc"), Hud->FlatRings[0]->GetVisibility(), ESlateVisibility::Collapsed);
		int32 FlatShown = 0;
		for (const FSWGSpaceHudFlatBlip& Blip : Hud->FlatBlips) if (Blip.Image->GetVisibility() != ESlateVisibility::Collapsed) ++FlatShown;
		TestEqual(TEXT("flat mode shows a blip for each object"), FlatShown, Full.Blips.Num());
		FlatCvar->Set(0, ECVF_SetByCode);

		// Nothing to show: every panel element collapses and the holograms go.
		Hud->SetData(FSWGSpaceHudData());
		TestEqual(TEXT("no target text without a target"), Hud->TargetName->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("no component rows without data"), Hud->ComponentRows[0].Label->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("no shield arc without data"), Hud->ShieldArcs[0].Image->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("no range text without radar data"), Hud->RangeText->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("no comm button without a target"), Hud->CommButton->GetVisibility(), ESlateVisibility::Collapsed);
		Hud->EnsureHolograms(false);
		State.TargetId = 0;
	}

	// ── Router activation and the old panel ─────────────────────────────────
	{
		// The router initialises through the local player's viewport client, which a standalone game instance does not have; give it a bare one.
		UGameViewportClient* Viewport = NewObject<UGameViewportClient>(GEngine, GEngine->GameViewportClientClass);
		Viewport->Init(*Game->GetWorldContext(), Game, false);
		// The editor's viewport class is not the CommonUI one, which the action router would log as an error for this fixture.
		IConsoleVariable* CheckViewportClass = IConsoleManager::Get().FindConsoleVariable(TEXT("CommonUI.Debug.CheckGameViewportClientValid"));
		const int32 PreviousCheck = CheckViewportClass ? CheckViewportClass->GetInt() : 0;
		if (CheckViewportClass) CheckViewportClass->Set(0, ECVF_SetByCode);
		ON_SCOPE_EXIT { if (CheckViewportClass) CheckViewportClass->Set(PreviousCheck, ECVF_SetByCode); };
		Game->GetWorldContext()->GameViewport = Viewport;
		FString Error;
		ULocalPlayer* LocalPlayer = Game->CreateLocalPlayer(0, Error, false);
		ON_SCOPE_EXIT { if (LocalPlayer) Game->RemoveLocalPlayer(LocalPlayer); Game->GetWorldContext()->GameViewport = nullptr; };
		// The HUD opens against a player controller, so the fixture needs one bound to the local player.
		APlayerController* Controller = LocalPlayer ? World->SpawnActor<APlayerController>() : nullptr;
		// A world that has not begun play does not register spawned controllers, and the player-added hooks need it registered.
		if (Controller) { World->AddController(Controller); Controller->SetPlayer(LocalPlayer); }
		USWGUISubsystem* Router = LocalPlayer ? LocalPlayer->GetSubsystem<USWGUISubsystem>() : nullptr;
		if (TestNotNull(TEXT("a local player with the UI router exists"), Router))
		{
			TestTrue(TEXT("the holo presenter offers the space HUD"), Router->CanOpenFeature(ESWGUIFeature::SpaceHud));
			UClass* HudClass = LoadClass<USWGHudWidget>(nullptr, TEXT("/SWGUICore/UI/HUD/WBP_Hud.WBP_Hud_C"));
			USWGHudWidget* CharacterHud = HudClass && Controller ? CreateWidget<USWGHudWidget>(Controller, HudClass) : nullptr;
			if (TestNotNull(TEXT("character HUD loads"), CharacterHud))
			{
				const TSharedRef<SWidget> CharacterSlate = CharacterHud->TakeWidget();
				ON_SCOPE_EXIT { CharacterHud->NativeDestruct(); };
				UWidget* FlightPanel = CharacterHud->WidgetTree->FindWidget(TEXT("ShipFlightPanel"));
				Ship->TangibleComponent->bHasBase3 = true;
				UStaticMeshComponent* Hull = NewObject<UStaticMeshComponent>(Ship);
				Hull->SetupAttachment(Ship->GetRootComponent());
				Hull->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				Hull->RegisterComponent();
				if (TestTrue(TEXT("fixture reaches flight readiness"), Graph->IsLocalSpaceSceneReady()) && TestNotNull(TEXT("flight panel"), FlightPanel))
				{
					CharacterHud->UpdateFlightReadout();
					TestTrue(TEXT("piloting opens the space HUD through the router"), Router->IsFeatureOpen(ESWGUIFeature::SpaceHud));
					TestEqual(TEXT("the space HUD hides the old flight panel"), FlightPanel->GetVisibility(), ESlateVisibility::Collapsed);
					Graph->ContainmentTypeByObjectId.Add(103, int32(ESWGContainmentType::VolumeContained));
					CharacterHud->UpdateFlightReadout();
					TestFalse(TEXT("leaving the seat closes the space HUD"), Router->IsFeatureOpen(ESWGUIFeature::SpaceHud));
				}
			}
		}
	}
	return !HasAnyErrors();
}

// Captures the HUD with sample data through a widget renderer. Only runs with -SWGCaptureSpaceHud; the PNGs go to .agent_tmp/stage6-captures.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceHudCaptureTest, "SWG.Space.SpaceHudCapture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpaceHudCaptureTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("SWGCaptureSpaceHud"))) return true;
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->InitializeStandalone();
	UWorld* World = Game->GetWorld();
	ON_SCOPE_EXIT { Game->Shutdown(); GEngine->DestroyWorldContext(World); };
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	if (!Tre) return false;
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives load"), Tre->LoadArchives())) return false;
	if (!TestTrue(TEXT("the HUD can be built"), USWGSpaceHudWidget::CanBuild(Game))) return false;
#if WITH_EDITOR
	for (const TCHAR* Path : {USWGSpaceHudWidget::SpriteMaterialPath, USWGSpaceHudWidget::ArcMaterialPath})
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Path);
		FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
		if (Resource && !Resource->IsGameThreadShaderMapComplete()) Resource->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
	}
	FAssetCompilingManager::Get().FinishAllCompilation();
#endif
	USWGSpaceHudWidget* Hud = CreateWidget<USWGSpaceHudWidget>(Game, USWGSpaceHudWidget::StaticClass());
	if (!TestNotNull(TEXT("space HUD widget builds"), Hud)) return false;
	const TSharedRef<SWidget> HudSlate = Hud->TakeWidget();

	FSWGSpaceHudData Data;
	Data.Speed = 92.f; Data.MaxSpeed = 120.f; Data.BoostMaxSpeed = 170.f; Data.Throttle = 0.8f;
	Data.Capacitor = 640.f; Data.CapacitorMax = 1000.f;
	Data.Booster = 140.f; Data.BoosterMax = 200.f;
	Data.Weapons = {{1, 1.f}, {2, 0.82f}, {3, 0.2f}};
	Data.bHasTarget = true;
	Data.TargetLocation = FVector(2400, 260, 90);
	Data.TargetRelativeVelocity = FVector(0, 400, 0);
	Data.ProjectileSpeed = 80.f;

	const FString Directory = FPaths::ProjectDir() / TEXT(".agent_tmp/stage6-captures");
	IFileManager::Get().MakeDirectory(*Directory, true);
	struct FCase { const TCHAR* Name; FVector2D Size; FVector TargetLocation; bool bHolo; };
	const FCase Cases[] = {
		{TEXT("hud-16x9"), FVector2D(1920, 1080), FVector(2400, 260, 90), true},
		{TEXT("hud-21x9"), FVector2D(3440, 1440), FVector(2400, 260, 90), true},
		{TEXT("hud-16x9-offscreen"), FVector2D(1920, 1080), FVector(600, 3000, 0), true},
		{TEXT("hud-16x9-plain"), FVector2D(1920, 1080), FVector(2400, 260, 90), false}};
	IConsoleVariable* HoloStrength = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.HudHoloStrength"));
	IConsoleVariable* DebugSlots = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.HudDebugSlots"));
	for (const FCase& Case : Cases)
	{
		HoloStrength->Set(Case.bHolo ? 1 : 0, ECVF_SetByCode);
		DebugSlots->Set(Case.Size.X > 3000 ? 1 : 0, ECVF_SetByCode);
		Hud->UpdateLayout(Case.Size);
		FSWGSpaceHudView Camera;
		Camera.Rotation = FRotator::ZeroRotator; Camera.HorizontalFovDegrees = 90.f;
		Hud->SetViewOverride(Camera);
		Data.TargetLocation = Case.TargetLocation;
		Hud->SetData(Data);
		Hud->StartIntro();
		Hud->PulseElement(Hud->GetGaugeImage(ESWGSpaceHudGauge::Weapon));
		Hud->TickHud(0.12f);
		Hud->TickHud(3.f);
		Hud->PulseElement(Hud->GetGaugeImage(ESWGSpaceHudGauge::Weapon));
		Hud->TickHud(0.f);
		const TSharedRef<SWidget> Backdrop = SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.004f, 0.008f, 0.02f))]
			+ SOverlay::Slot()[HudSlate];
		FWidgetRenderer Renderer(false);
		Renderer.SetIsPrepassNeeded(true);
		UTextureRenderTarget2D* Target = Renderer.DrawWidget(Backdrop, Case.Size);
		TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*(Directory / FString::Printf(TEXT("%s.png"), Case.Name))));
		TestTrue(*FString::Printf(TEXT("%s exports"), Case.Name), Target && Writer && FImageUtils::ExportRenderTarget2DAsPNG(Target, *Writer));
	}
	HoloStrength->Set(1, ECVF_SetByCode);
	DebugSlots->Set(0, ECVF_SetByCode);
	return !HasAnyErrors();
}
#endif
