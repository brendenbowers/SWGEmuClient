#include "SWGSpaceHudWidget.h"
#include "SWGHoloStyle.h"
#include "SWGRetailStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Common/SWGWorldScale.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	TAutoConsoleVariable<int32> CVarHudFlat(
		TEXT("swg.Space.HudFlat"), 0,
		TEXT("1 draws the piloting HUD's 3D parts (the radar sphere and the target ship) flat on the screen. Honoured by the radar and target panel."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarHudHoloStrength(
		TEXT("swg.Space.HudHoloStrength"), 1.f,
		TEXT("0..1 strength of the HUD hologram look (scanlines, sweep, flicker, grain). 0 draws the plain additive sprites."),
		ECVF_Default);
	TAutoConsoleVariable<int32> CVarHudDebugSlots(
		TEXT("swg.Space.HudDebugSlots"), 0,
		TEXT("1 outlines the HUD's slots (reticle, radar, target, status, warnings) and the safe area."),
		ECVF_Default);

	/** Retail's hud text colours, from ui_hud_space_ship_reticle.inc. */
	FLinearColor Srgb(uint8 Red, uint8 Green, uint8 Blue) { return FLinearColor::FromSRGBColor(FColor(Red, Green, Blue)); }
	const FLinearColor ReticleGreen = Srgb(0x4A, 0xFE, 0x16);
	const FLinearColor GlowCyan = Srgb(0x1C, 0xFF, 0xFF);
	const FLinearColor GlowBack = Srgb(0x00, 0xD6, 0xFB);
	const FLinearColor WeaponTextColor = Srgb(0x00, 0xFF, 0xAF);
	/** The lead mark takes the speed gauge's tick amber; the aim mark and the reticle share a green; the arrow is offScreenIndicator's. */
	const FLinearColor LeadColor = Srgb(0xFF, 0xAC, 0x00);
	const FLinearColor ArrowColor = Srgb(0x00, 0xFF, 0x00);

	constexpr float RevealSeconds = 0.5f;
	constexpr float ScanlinePitch = 3.f;
	constexpr int32 MaxWeaponIndicators = 8;
	/** Retail page coordinates (354x284) of the reticle's centre. */
	const FVector2D RetailPageCentre(177.0, 142.0);
	/** Slot size and screen margin of the marks, in design units. */
	constexpr float MarkSize = 56.f;
	constexpr float ArrowEdgeInset = 36.f;

	FBox2D CentredRect(const FVector2D& Centre, const FVector2D& Size) { return FBox2D(Centre - Size * 0.5, Centre + Size * 0.5); }
}

bool USWGSpaceHudWidget::CanBuild(UGameInstance* Game)
{
	USWGTreSubsystem* Tre = Game ? Game->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (!Tre || !Tre->IsLoaded() || !Tre->FileExists(FSWGSpaceHudAtlas::GetAtlasPath())) return false;
	return LoadObject<UMaterialInterface>(nullptr, SpriteMaterialPath) && LoadObject<UMaterialInterface>(nullptr, ArcMaterialPath);
}

bool USWGSpaceHudWidget::IsFlat()
{
	return CVarHudFlat.GetValueOnGameThread() != 0;
}

void USWGSpaceHudWidget::EnsureBuilt()
{
	if (bBuilt || !WidgetTree) return;
	bBuilt = true;
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UGameInstance* Game = GetGameInstance();
	Tre = Game ? Game->GetSubsystem<USWGTreSubsystem>() : nullptr;
	Atlas = FSWGSpaceHudAtlas(Tre);
	SpriteParent = LoadObject<UMaterialInterface>(nullptr, SpriteMaterialPath);
	ArcParent = LoadObject<UMaterialInterface>(nullptr, ArcMaterialPath);
	if (!SpriteParent || !ArcParent)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGSpaceHudWidget: M_SWGHoloUI / M_SWGHoloUIArc are missing; run swg.CreateHoloUIAssets. The sprites and gauges will not draw."));
	}

	if (!WidgetTree->RootWidget)
	{
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = RootCanvas;
	}
	else
	{
		RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	}
	for (int32 SlotIndex = 0; SlotIndex < int32(ESWGSpaceHudSlot::Count); ++SlotIndex)
	{
		SlotPanels[SlotIndex] = WidgetTree->ConstructWidget<UCanvasPanel>();
		RootCanvas->AddChildToCanvas(SlotPanels[SlotIndex]);
		UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
		Frame->SetBrush(SWGHoloStyle::FrameBrush());
		Frame->SetVisibility(ESlateVisibility::Collapsed);
		SlotPanels[SlotIndex]->AddChildToCanvas(Frame);
		SlotFrames[SlotIndex] = Frame;
		if (UCanvasPanelSlot* FrameSlot = Cast<UCanvasPanelSlot>(Frame->Slot))
		{
			FrameSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			FrameSlot->SetOffsets(FMargin(0.f));
		}
	}
	BuildReticle();
	BuildRadar();
	BuildTargetPanel();
	BuildStatusPanel();
	BuildWarnings();

	// Lead, aim and the off-screen arrow live in screen space above the slots.
	FSWGSpaceHudSprite Sprite;
	if (Atlas.Find(TEXT("TargetLead"), Sprite)) LeadImage = AddSprite(RootCanvas, Sprite, LeadColor, 0.7f);
	if (Atlas.Find(TEXT("TargetAim"), Sprite)) AimImage = AddSprite(RootCanvas, Sprite, ReticleGreen, 0.7f);
	if (Atlas.Find(TEXT("OffScreenArrow"), Sprite)) ArrowImage = AddSprite(RootCanvas, Sprite, ArrowColor, 0.7f);
	for (UImage* Mark : {LeadImage.Get(), AimImage.Get(), ArrowImage.Get()})
	{
		if (Mark) Mark->SetVisibility(ESlateVisibility::Collapsed);
	}

	UpdateLayout(FVector2D(1920.0, 1080.0));
}

void USWGSpaceHudWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureBuilt();
}

TSharedRef<SWidget> USWGSpaceHudWidget::RebuildWidget()
{
	// A widget made without a player context never gets NativeOnInitialized, so the tree is also built here, before Slate reads it.
	EnsureBuilt();
	return Super::RebuildWidget();
}

void USWGSpaceHudWidget::NativeConstruct()
{
	Super::NativeConstruct();
	StartIntro();
}

void USWGSpaceHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const FVector2D LocalSize = FVector2D(MyGeometry.GetLocalSize());
	if (LocalSize.X > 1.0 && LocalSize.Y > 1.0 && !LocalSize.Equals(Layout.ViewSize, 0.5)) UpdateLayout(LocalSize);
	RefreshFromWorld();
	TickHud(InDeltaTime);
}

// ── Building ────────────────────────────────────────────────────────────────

UMaterialInstanceDynamic* USWGSpaceHudWidget::MakeMaterial(bool bArc, const FSWGSpaceHudSprite* Sprite, const FLinearColor& Tint)
{
	UMaterialInterface* Parent = bArc ? ArcParent.Get() : SpriteParent.Get();
	if (!Parent)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGSpaceHudWidget: the %s material is not loaded"), bArc ? TEXT("arc") : TEXT("sprite"));
		return nullptr;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
	if (Sprite)
	{
		UTexture2D* Texture = Atlas.LoadTexture(*Sprite);
		if (!Texture)
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGSpaceHudWidget: could not load %s"), *Sprite->TexturePath);
			return nullptr;
		}
		const FVector4f Uv = Atlas.GetUvRect(*Sprite);
		Material->SetTextureParameterValue(TEXT("Sprite"), Texture);
		Material->SetScalarParameterValue(TEXT("U0"), Uv.X); Material->SetScalarParameterValue(TEXT("V0"), Uv.Y);
		Material->SetScalarParameterValue(TEXT("U1"), Uv.Z); Material->SetScalarParameterValue(TEXT("V1"), Uv.W);
		Material->SetVectorParameterValue(TEXT("Tint"), Tint);
	}
	// A fixed fraction per element keeps their flicker and sweep phases apart.
	Material->SetScalarParameterValue(TEXT("Seed"), FMath::Frac(Elements.Num() * 0.6180339f));
	Material->SetScalarParameterValue(TEXT("Reveal"), 0.f);
	Material->SetScalarParameterValue(TEXT("HoloStrength"), FMath::Clamp(CVarHudHoloStrength.GetValueOnGameThread(), 0.f, 1.f));
	return Material;
}

UImage* USWGSpaceHudWidget::AddSprite(UCanvasPanel* Parent, const FSWGSpaceHudSprite& Sprite, const FLinearColor& Tint, float RevealDelay)
{
	UMaterialInstanceDynamic* Material = Parent ? MakeMaterial(false, &Sprite, Tint) : nullptr;
	if (!Material) return nullptr;
	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	Image->SetBrushFromMaterial(Material);
	Image->SetVisibility(ESlateVisibility::HitTestInvisible);
	Parent->AddChildToCanvas(Image);
	FSWGSpaceHudElement& Element = Elements.AddDefaulted_GetRef();
	Element.Image = Image; Element.Material = Material; Element.RevealDelay = RevealDelay;
	return Image;
}

UImage* USWGSpaceHudWidget::AddReticleSprite(const FSWGSpaceHudSprite& Sprite, const FLinearColor& Tint, const FBox2D& RetailRect, float Delay, float Opacity)
{
	UImage* Image = AddSprite(SlotPanels[int32(ESWGSpaceHudSlot::Reticle)], Sprite, Tint, Delay);
	if (FSWGSpaceHudElement* Element = FindElement(Image))
	{
		Element->RetailRect = RetailRect;
		Element->Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	}
	return Image;
}

UTextBlock* USWGSpaceHudWidget::AddReticleText(const FBox2D& RetailRect, int32 RetailFontPixels, const FLinearColor& Color, float Delay)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetJustification(ETextJustify::Center);
	Text->SetShadowOffset(FVector2D(1.0, 1.0));
	Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Text->SetVisibility(ESlateVisibility::Collapsed);
	SlotPanels[int32(ESWGSpaceHudSlot::Reticle)]->AddChildToCanvas(Text);
	FSWGSpaceHudText& Entry = Texts.AddDefaulted_GetRef();
	Entry.Text = Text; Entry.RetailRect = RetailRect; Entry.RetailFontPixels = RetailFontPixels; Entry.RevealDelay = Delay;
	return Text;
}

void USWGSpaceHudWidget::AddNineSlice(const TCHAR* Style, const FLinearColor& Tint, float Delay)
{
	// The retail 64x64 reticle image draws a rectangle style: corners at their own size, edge strips stretched between them.
	const FBox2D Box = CentredRect(RetailPageCentre, FVector2D(64.0, 64.0));
	auto Piece = [&](const TCHAR* Name, FSWGSpaceHudSprite& Out) { return Atlas.FindReticlePiece(Style, Name, Out); };
	FSWGSpaceHudSprite NorthWest, NorthEast, SouthWest, SouthEast, North, South, West, East;
	if (!Piece(TEXT("nw"), NorthWest) || !Piece(TEXT("ne"), NorthEast) || !Piece(TEXT("sw"), SouthWest) || !Piece(TEXT("se"), SouthEast)
		|| !Piece(TEXT("n"), North) || !Piece(TEXT("s"), South) || !Piece(TEXT("w"), West) || !Piece(TEXT("e"), East))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGSpaceHudWidget: retail reticle style '%s' is missing from ui_styles.inc"), Style);
		return;
	}
	auto Size = [](const FSWGSpaceHudSprite& Sprite) { return FVector2D(Sprite.Rect.Width(), Sprite.Rect.Height()); };
	const FVector2D Nw = Size(NorthWest), Ne = Size(NorthEast), Sw = Size(SouthWest), Se = Size(SouthEast);
	auto Add = [&](const FSWGSpaceHudSprite& Sprite, const FVector2D& Min, const FVector2D& Extent)
	{
		// The e style's edge strips are blank in the sheet; there is nothing to draw for them.
		if (Atlas.CountVisibleTexels(Sprite, 4) == 0) return;
		if (UImage* Image = AddReticleSprite(Sprite, Tint, FBox2D(Min, Min + Extent), Delay)) BracketImages.Add(Image);
	};
	Add(NorthWest, Box.Min, Nw);
	Add(NorthEast, FVector2D(Box.Max.X - Ne.X, Box.Min.Y), Ne);
	Add(SouthWest, FVector2D(Box.Min.X, Box.Max.Y - Sw.Y), Sw);
	Add(SouthEast, FVector2D(Box.Max.X - Se.X, Box.Max.Y - Se.Y), Se);
	Add(North, FVector2D(Box.Min.X + Nw.X, Box.Min.Y), FVector2D(Box.GetSize().X - Nw.X - Ne.X, Nw.Y));
	Add(South, FVector2D(Box.Min.X + Sw.X, Box.Max.Y - Sw.Y), FVector2D(Box.GetSize().X - Sw.X - Se.X, Sw.Y));
	Add(West, FVector2D(Box.Min.X, Box.Min.Y + Nw.Y), FVector2D(Nw.X, Box.GetSize().Y - Nw.Y - Sw.Y));
	Add(East, FVector2D(Box.Max.X - Ne.X, Box.Min.Y + Ne.Y), FVector2D(Ne.X, Box.GetSize().Y - Ne.Y - Se.Y));
}

void USWGSpaceHudWidget::BuildGauge(ESWGSpaceHudGauge Gauge, float BeginTurns, float EndTurns, float RetailThicknessPixels, float RetailDiameter,
	const FLinearColor& OuterColor, const FLinearColor& InnerColor, float InnerOpacity, float Delay, const FBox2D& TextRect, const FLinearColor& TextColor)
{
	FSWGSpaceHudGaugeView& View = Gauges[int32(Gauge)];
	UMaterialInstanceDynamic* Material = MakeMaterial(true, nullptr, OuterColor);
	if (!Material) return;
	Material->SetScalarParameterValue(TEXT("ArcBegin"), BeginTurns);
	Material->SetScalarParameterValue(TEXT("ArcEnd"), EndTurns);
	Material->SetScalarParameterValue(TEXT("Thickness"), RetailThicknessPixels / (RetailDiameter * 0.5f));
	Material->SetScalarParameterValue(TEXT("InnerOpacity"), InnerOpacity);
	Material->SetVectorParameterValue(TEXT("OuterColor"), OuterColor);
	Material->SetVectorParameterValue(TEXT("InnerColor"), InnerColor);
	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	Image->SetBrushFromMaterial(Material);
	Image->SetVisibility(ESlateVisibility::Collapsed);
	SlotPanels[int32(ESWGSpaceHudSlot::Reticle)]->AddChildToCanvas(Image);
	FSWGSpaceHudElement& Element = Elements.AddDefaulted_GetRef();
	Element.Image = Image; Element.Material = Material; Element.RevealDelay = Delay;
	Element.RetailRect = CentredRect(RetailPageCentre + FVector2D(0.0, 0.5), FVector2D(RetailDiameter, RetailDiameter));
	View.Image = Image; View.Material = Material;
	View.Text = AddReticleText(TextRect, 13, TextColor, Delay);
}

void USWGSpaceHudWidget::BuildReticle()
{
	// ui_hud_space_ship_reticle.inc: reticle_0 (style e), the centre page's glows, then the Gauges page.
	AddNineSlice(TEXT("e"), ReticleGreen, 0.f);
	// The centre page sits at (69,37) in the 354x284 page; its images' Locations are relative to it.
	const FVector2D CentrePage(69.0, 37.0);
	auto AddGlow = [&](const TCHAR* Name, const FVector2D& Location, const FVector2D& Size, const FLinearColor& Tint, float Opacity, float Delay)
	{
		FSWGSpaceHudSprite Sprite;
		if (Atlas.Find(Name, Sprite)) AddReticleSprite(Sprite, Tint, FBox2D(CentrePage + Location, CentrePage + Location + Size), Delay, Opacity);
	};
	AddGlow(TEXT("GlowLeftBack"), FVector2D(13.0, 28.0), FVector2D(43.0, 157.0), GlowCyan, 0.6f, 0.1f);
	AddGlow(TEXT("GlowLeft"), FVector2D(12.0, 20.0), FVector2D(71.0, 172.0), GlowCyan, 1.f, 0.2f);
	AddGlow(TEXT("GlowLeft"), FVector2D(12.0, 20.0), FVector2D(71.0, 172.0), GlowBack, 0.6f, 0.2f);
	AddGlow(TEXT("GlowRightBack"), FVector2D(178.0, 65.0), FVector2D(24.0, 97.0), GlowCyan, 0.6f, 0.1f);
	AddGlow(TEXT("GlowRight"), FVector2D(144.0, 58.0), FVector2D(62.0, 129.0), GlowCyan, 1.f, 0.2f);
	AddGlow(TEXT("GlowRight"), FVector2D(144.0, 58.0), FVector2D(62.0, 129.0), GlowBack, 0.6f, 0.2f);

	// The Gauges page: Speed on the left (arcs .31 to .67), Weapon on the right (.17 to -.07), the retail colours and thicknesses.
	// Booster shares the Weapon gauge's side as an inner ring so the three do not overlap.
	BuildGauge(ESWGSpaceHudGauge::Speed, 0.31f, 0.67f, 11.f, 180.f, Srgb(0x24, 0xDE, 0xFF), Srgb(0x2F, 0xEA, 0xFF), 0.4f, 0.35f,
		CentredRect(FVector2D(177.0, 222.0), FVector2D(70.0, 16.0)), Srgb(0x5B, 0xFE, 0x16));
	BuildGauge(ESWGSpaceHudGauge::Weapon, 0.17f, -0.07f, 8.f, 180.f, Srgb(0xFF, 0x2D, 0x2D), Srgb(0xFF, 0x25, 0x1A), 0.4f, 0.45f,
		CentredRect(FVector2D(246.0, 80.0), FVector2D(50.0, 16.0)), Srgb(0x66, 0xFF, 0x00));
	BuildGauge(ESWGSpaceHudGauge::Booster, 0.17f, -0.07f, 7.f, 152.f, Srgb(0x1A, 0xFF, 0x00), Srgb(0x1A, 0xFF, 0x00), 0.5f, 0.55f,
		CentredRect(FVector2D(246.0, 240.0), FVector2D(50.0, 16.0)), Srgb(0x5B, 0xFE, 0x16));

	// Weapon indicators (weapon0..n): the retail weapon page is hidden until the HUD has weapons to show. Each is the reticle's bracket
	// piece and a condition text, stacked down the right of the reticle.
	FSWGSpaceHudSprite BracketSprite;
	for (int32 WeaponIndex = 0; WeaponIndex < MaxWeaponIndicators; ++WeaponIndex)
	{
		const FVector2D Origin(292.0, 100.0 + WeaponIndex * 20.0);
		UImage* Icon = Atlas.FindReticlePiece(TEXT("e"), TEXT("nw"), BracketSprite)
			? AddReticleSprite(BracketSprite, ReticleGreen, FBox2D(Origin, Origin + FVector2D(10.0, 15.0)), 0.6f + 0.04f * WeaponIndex) : nullptr;
		if (Icon) Icon->SetVisibility(ESlateVisibility::Collapsed);
		WeaponIcons.Add(Icon);
		WeaponTexts.Add(AddReticleText(FBox2D(Origin + FVector2D(13.0, 0.0), Origin + FVector2D(60.0, 16.0)), 11, WeaponTextColor, 0.6f + 0.04f * WeaponIndex));
	}
}

// ── Layout ──────────────────────────────────────────────────────────────────

FSWGSpaceHudElement* USWGSpaceHudWidget::FindElement(const UImage* Image)
{
	return Elements.FindByPredicate([Image](const FSWGSpaceHudElement& Element) { return Element.Image == Image; });
}

void USWGSpaceHudWidget::SetElementRegion(FSWGSpaceHudElement& Element, const FBox2D& ScreenRect)
{
	Element.ScreenRect = ScreenRect;
	if (!Element.Material || Layout.ViewSize.Y <= 0.0) return;
	Element.Material->SetScalarParameterValue(TEXT("RegionYOffset"), float(ScreenRect.Min.Y / Layout.ViewSize.Y));
	Element.Material->SetScalarParameterValue(TEXT("RegionYSpan"), float(FMath::Max(ScreenRect.GetSize().Y, 1.0) / Layout.ViewSize.Y));
	Element.Material->SetScalarParameterValue(TEXT("ScanRows"), float(Layout.ViewSize.Y / ScanlinePitch));
}

void USWGSpaceHudWidget::UpdateLayout(const FVector2D& ViewSize)
{
	EnsureBuilt();
	if (ViewSize.X <= 0.0 || ViewSize.Y <= 0.0 || !RootCanvas) return;
	Layout = SWGSpaceHud::ComputeLayout(ViewSize);
	const bool bShowFrames = CVarHudDebugSlots.GetValueOnGameThread() != 0;
	for (int32 SlotIndex = 0; SlotIndex < int32(ESWGSpaceHudSlot::Count); ++SlotIndex)
	{
		if (UCanvasPanelSlot* PanelSlot = Cast<UCanvasPanelSlot>(SlotPanels[SlotIndex]->Slot))
		{
			const FBox2D& Rect = Layout.Slots[SlotIndex];
			PanelSlot->SetPosition(Rect.Min);
			PanelSlot->SetSize(Rect.GetSize());
		}
		if (SlotFrames[SlotIndex]) SlotFrames[SlotIndex]->SetVisibility(bShowFrames ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	const double Scale = Layout.GetReticleScale();
	const FVector2D Origin = Layout.GetSlot(ESWGSpaceHudSlot::Reticle).Min;
	for (FSWGSpaceHudElement& Element : Elements)
	{
		if (!Element.RetailRect.bIsValid)
		{
			SetElementRegion(Element, Element.ScreenRect.bIsValid ? Element.ScreenRect : FBox2D(FVector2D::ZeroVector, ViewSize));
			continue;
		}
		if (UCanvasPanelSlot* ElementSlot = Cast<UCanvasPanelSlot>(Element.Image->Slot))
		{
			ElementSlot->SetPosition(Element.RetailRect.Min * Scale);
			ElementSlot->SetSize(Element.RetailRect.GetSize() * Scale);
		}
		SetElementRegion(Element, FBox2D(Origin + Element.RetailRect.Min * Scale, Origin + Element.RetailRect.Max * Scale));
	}
	for (const FSWGSpaceHudText& Entry : Texts)
	{
		if (UCanvasPanelSlot* TextSlot = Cast<UCanvasPanelSlot>(Entry.Text->Slot))
		{
			TextSlot->SetPosition(Entry.RetailRect.Min * Scale);
			TextSlot->SetSize(Entry.RetailRect.GetSize() * Scale);
		}
		Entry.Text->SetFont(SWGRetailStyle::Font(FMath::Max(8, FMath::RoundToInt(Entry.RetailFontPixels * Scale)), true));
	}
	LayoutPanels();
}

// ── Animation ───────────────────────────────────────────────────────────────

void USWGSpaceHudWidget::StartIntro()
{
	IntroSeconds = 0.f;
	for (FSWGSpaceHudElement& Element : Elements)
	{
		Element.Reveal = 0.f;
		if (Element.Material) Element.Material->SetScalarParameterValue(TEXT("Reveal"), 0.f);
	}
}

void USWGSpaceHudWidget::PulseElement(const UImage* Image)
{
	if (FSWGSpaceHudElement* Element = FindElement(Image)) Element->Pulse = 1.f;
}

void USWGSpaceHudWidget::TickHud(float DeltaSeconds)
{
	IntroSeconds += DeltaSeconds;
	const float HoloStrength = FMath::Clamp(CVarHudHoloStrength.GetValueOnGameThread(), 0.f, 1.f);
	const bool bStrengthChanged = HoloStrength != AppliedHoloStrength;
	AppliedHoloStrength = HoloStrength;
	for (FSWGSpaceHudElement& Element : Elements)
	{
		if (!Element.Material) continue;
		const float Linear = FMath::Clamp((IntroSeconds - Element.RevealDelay) / RevealSeconds, 0.f, 1.f);
		const float Reveal = Linear * Linear * (3.f - 2.f * Linear);
		if (Reveal != Element.Reveal) { Element.Reveal = Reveal; Element.Material->SetScalarParameterValue(TEXT("Reveal"), Reveal); }
		if (Element.Pulse > 0.f)
		{
			Element.Pulse = FMath::Max(0.f, Element.Pulse - DeltaSeconds * 2.5f);
			Element.Material->SetScalarParameterValue(TEXT("Pulse"), Element.Pulse);
		}
		if (bStrengthChanged) Element.Material->SetScalarParameterValue(TEXT("HoloStrength"), HoloStrength);
	}
	for (const FSWGSpaceHudText& Entry : Texts)
	{
		const float Linear = FMath::Clamp((IntroSeconds - Entry.RevealDelay) / RevealSeconds, 0.f, 1.f);
		Entry.Text->SetRenderOpacity(Linear);
	}
	for (const FSWGSpaceHudPanelText& Entry : PanelTexts)
	{
		Entry.Text->SetRenderOpacity(FMath::Clamp((IntroSeconds - Entry.RevealDelay) / RevealSeconds, 0.f, 1.f));
	}
	UpdateHolograms(DeltaSeconds);
	UpdateWarnings(DeltaSeconds);
}

void USWGSpaceHudWidget::NativeDestruct()
{
	EnsureHolograms(false);
	StopTargetBracket();
	Super::NativeDestruct();
}

// ── Data ────────────────────────────────────────────────────────────────────

bool USWGSpaceHudWidget::RefreshFromWorld()
{
	const USWGObjectGraphSubsystem* Graph = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	FSWGSpaceHudData Data;
	if (!Graph || !FSWGSpaceHudData::Gather(*Graph, Data))
	{
		SetRenderOpacity(0.f);
		EnsureHolograms(false);
		StopTargetBracket();
		return false;
	}
	SetData(Data);
	return true;
}

int32 USWGSpaceHudWidget::GetVisibleWeaponIndicators() const
{
	int32 Count = 0;
	for (const TObjectPtr<UTextBlock>& Text : WeaponTexts)
	{
		if (Text && Text->GetVisibility() != ESlateVisibility::Collapsed) ++Count;
	}
	return Count;
}

void USWGSpaceHudWidget::ApplyGauge(ESWGSpaceHudGauge Gauge, bool bHasData, float Fill, const FText& Label)
{
	FSWGSpaceHudGaugeView& View = Gauges[int32(Gauge)];
	if (!View.Image) return;
	const ESlateVisibility Shown = bHasData ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	View.Image->SetVisibility(Shown);
	if (View.Text) View.Text->SetVisibility(Shown);
	View.bShown = bHasData;
	if (!bHasData) { View.LastFill = -1.f; return; }
	Fill = FMath::Clamp(Fill, 0.f, 1.f);
	View.Material->SetScalarParameterValue(TEXT("Fill"), Fill);
	if (View.Text) View.Text->SetText(Label);
	// A jump of a few percent (a capacitor shot, a booster burst) flashes the arc; steady drift does not.
	if (View.LastFill >= 0.f && FMath::Abs(Fill - View.LastFill) > 0.04f) PulseElement(View.Image);
	View.LastFill = Fill;
}

void USWGSpaceHudWidget::SetData(const FSWGSpaceHudData& Data)
{
	EnsureBuilt();
	LastData = Data;
	SetRenderOpacity(1.f);

	// Speed: the arc spans zero to the booster's top speed when the booster is known, else to MaxSpeed. The tick marks the throttle
	// setting and the overdrive region (red) is the speed range past MaxSpeed.
	const bool bHasSpeed = Data.Speed.IsSet() && Data.MaxSpeed.IsSet() && *Data.MaxSpeed > 0.f;
	float SpeedFill = 0.f;
	if (bHasSpeed)
	{
		const float ArcMaximum = Data.BoostMaxSpeed.IsSet() ? *Data.BoostMaxSpeed : *Data.MaxSpeed;
		SpeedFill = *Data.Speed / ArcMaximum;
		UMaterialInstanceDynamic* Material = Gauges[int32(ESWGSpaceHudGauge::Speed)].Material;
		if (Material)
		{
			Material->SetScalarParameterValue(TEXT("OverdriveFrom"), Data.BoostMaxSpeed.IsSet() ? *Data.MaxSpeed / ArcMaximum : 2.f);
			Material->SetScalarParameterValue(TEXT("TickAt"), Data.Throttle.IsSet() ? FMath::Clamp(*Data.Throttle * *Data.MaxSpeed / ArcMaximum, 0.f, 1.f) : -1.f);
		}
		const bool bOverdrive = Data.BoostMaxSpeed.IsSet() && *Data.Speed > *Data.MaxSpeed;
		if (bOverdrive && !bSpeedInOverdrive) PulseElement(Gauges[int32(ESWGSpaceHudGauge::Speed)].Image);
		bSpeedInOverdrive = bOverdrive;
	}
	ApplyGauge(ESWGSpaceHudGauge::Speed, bHasSpeed, SpeedFill, FText::AsNumber(FMath::RoundToInt(Data.Speed.Get(0.f))));

	const bool bHasCapacitor = Data.Capacitor.IsSet() && Data.CapacitorMax.IsSet() && *Data.CapacitorMax > 0.f;
	const float CapacitorFill = bHasCapacitor ? *Data.Capacitor / *Data.CapacitorMax : 0.f;
	ApplyGauge(ESWGSpaceHudGauge::Weapon, bHasCapacitor, CapacitorFill,
		FText::Format(NSLOCTEXT("SWGEmu", "HudPercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(CapacitorFill * 100.f))));
	const bool bHasBooster = Data.Booster.IsSet() && Data.BoosterMax.IsSet() && *Data.BoosterMax > 0.f;
	const float BoosterFill = bHasBooster ? *Data.Booster / *Data.BoosterMax : 0.f;
	ApplyGauge(ESWGSpaceHudGauge::Booster, bHasBooster, BoosterFill,
		FText::Format(NSLOCTEXT("SWGEmu", "HudPercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(BoosterFill * 100.f))));

	ApplyWeapons(Data);
	ApplyTargetMarks(Data);
	ApplyRadar(Data);
	ApplyTargetPanel(Data);
	ApplyStatus(Data);
	ApplyWarnings(Data);
	OnDataApplied.Broadcast(Data);
}

void USWGSpaceHudWidget::ApplyWeapons(const FSWGSpaceHudData& Data)
{
	for (int32 WeaponIndex = 0; WeaponIndex < WeaponTexts.Num(); ++WeaponIndex)
	{
		const bool bShown = Data.Weapons.IsValidIndex(WeaponIndex);
		const ESlateVisibility State = bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		if (WeaponIcons[WeaponIndex]) WeaponIcons[WeaponIndex]->SetVisibility(State);
		WeaponTexts[WeaponIndex]->SetVisibility(State);
		if (!bShown) continue;
		const float Condition = Data.Weapons[WeaponIndex].Condition;
		WeaponTexts[WeaponIndex]->SetText(FText::Format(NSLOCTEXT("SWGEmu", "HudPercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(Condition * 100.f))));
		WeaponTexts[WeaponIndex]->SetColorAndOpacity(FSlateColor(SWGSpaceHud::GetConditionColor(Condition)));
	}
}

bool USWGSpaceHudWidget::BuildView(FSWGSpaceHudView& OutView) const
{
	if (ViewOverride.IsSet())
	{
		OutView = *ViewOverride;
		OutView.Size = Layout.ViewSize;
		return true;
	}
	const APlayerController* Controller = GetOwningPlayer();
	const APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
	if (!Camera) return false;
	OutView.Location = Camera->GetCameraLocation();
	OutView.Rotation = Camera->GetCameraRotation();
	OutView.HorizontalFovDegrees = Camera->GetFOVAngle();
	OutView.Size = Layout.ViewSize;
	return true;
}

void USWGSpaceHudWidget::PlaceFloatingImage(UImage* Image, const FVector2D& Centre, const FVector2D& Size)
{
	if (UCanvasPanelSlot* ImageSlot = Cast<UCanvasPanelSlot>(Image->Slot))
	{
		ImageSlot->SetPosition(Centre - Size * 0.5);
		ImageSlot->SetSize(Size);
	}
	if (FSWGSpaceHudElement* Element = FindElement(Image)) SetElementRegion(*Element, CentredRect(Centre, Size));
}

void USWGSpaceHudWidget::ApplyTargetMarks(const FSWGSpaceHudData& Data)
{
	for (UImage* Mark : {LeadImage.Get(), AimImage.Get(), ArrowImage.Get()})
	{
		if (Mark) Mark->SetVisibility(ESlateVisibility::Collapsed);
	}
	FSWGSpaceHudView View;
	if (!BuildView(View)) return;
	const FVector2D MarkExtent(MarkSize * Layout.Unit, MarkSize * Layout.Unit);

	// Aim: where the nose points, at the target's range (or a kilometre out without a target), so it reads against the target.
	const double AimRange = Data.bHasTarget ? FMath::Max(FVector::Dist(Data.TargetLocation, Data.ShipLocation), 1000.0) : 100000.0;
	const FSWGSpaceHudProjection Aim = SWGSpaceHud::Project(View, Data.ShipLocation + Data.ShipForward * AimRange);
	if (AimImage && Aim.bOnScreen)
	{
		AimImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		PlaceFloatingImage(AimImage, Aim.Position, MarkExtent);
	}
	if (!Data.bHasTarget) return;

	// Lead: where a projectile meets the target. Without a projectile speed (none in client data) it marks the target itself.
	const float ProjectileUnits = Data.ProjectileSpeed.IsSet() ? *Data.ProjectileSpeed * SWGWorldScale : 0.f;
	const FVector LeadPoint = Data.ShipLocation + SWGSpaceHud::InterceptPoint(Data.TargetLocation - Data.ShipLocation, Data.TargetRelativeVelocity, ProjectileUnits);
	const FSWGSpaceHudProjection Lead = SWGSpaceHud::Project(View, LeadPoint);
	if (Lead.bOnScreen)
	{
		if (LeadImage)
		{
			LeadImage->SetVisibility(ESlateVisibility::HitTestInvisible);
			PlaceFloatingImage(LeadImage, Lead.Position, MarkExtent);
		}
		return;
	}
	if (!ArrowImage) return;
	// Off screen: an arrow on the edge of the safe area, turned toward the target. The sprite points right.
	const FVector2D Centre = View.Size * 0.5;
	const FBox2D Edge = Layout.Safe.ExpandBy(-ArrowEdgeInset * Layout.Unit);
	const FVector2D Half = Edge.GetSize() * 0.5;
	const double ReachX = FMath::Abs(Lead.Direction.X) > UE_KINDA_SMALL_NUMBER ? Half.X / FMath::Abs(Lead.Direction.X) : TNumericLimits<double>::Max();
	const double ReachY = FMath::Abs(Lead.Direction.Y) > UE_KINDA_SMALL_NUMBER ? Half.Y / FMath::Abs(Lead.Direction.Y) : TNumericLimits<double>::Max();
	const FVector2D ArrowCentre = Centre + Lead.Direction * FMath::Min(ReachX, ReachY);
	ArrowImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceFloatingImage(ArrowImage, ArrowCentre, FVector2D(16.0, 27.0) * (1.6 * Layout.Unit));
	ArrowImage->SetRenderTransformAngle(float(FMath::RadiansToDegrees(FMath::Atan2(Lead.Direction.Y, Lead.Direction.X))));
}
