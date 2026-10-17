#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGEffectsTypes.h"
#include "SWGSpaceHudAtlas.h"
#include "SWGSpaceHudModel.h"
#include "SWGSpaceHudWidget.generated.h"

class ASWGHoloRadarActor;
class ASWGHoloShipActor;
class UButton;
class UCanvasPanel;
class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTextBlock;
class USWGTreSubsystem;

DECLARE_MULTICAST_DELEGATE_OneParam(FSWGOnSpaceHudData, const FSWGSpaceHudData&);

/** The three arc gauges round the reticle. */
enum class ESWGSpaceHudGauge : uint8 { Speed, Weapon, Booster, Count };

/** One image of the HUD driven by an M_SWGHoloUI / M_SWGHoloUIArc instance: it sweeps in at its delay and pulses on request. */
USTRUCT()
struct FSWGSpaceHudElement
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UImage> Image;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;
	/** The reticle page's retail pixels, scaled into place by the layout; empty for elements the caller positions itself. */
	FBox2D RetailRect = FBox2D(ForceInit);
	/** Where the element last was on the whole HUD, so the material's scanlines and sweep line up across elements. */
	FBox2D ScreenRect = FBox2D(ForceInit);
	float RevealDelay = 0.f;
	float Pulse = 0.f;
	float Reveal = 0.f;
};

USTRUCT()
struct FSWGSpaceHudText
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UTextBlock> Text;
	FBox2D RetailRect = FBox2D(ForceInit);
	int32 RetailFontPixels = 13;
	float RevealDelay = 0.f;
};

USTRUCT()
struct FSWGSpaceHudGaugeView
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UImage> Image;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY()
	TObjectPtr<UTextBlock> Text;
	float LastFill = -1.f;
	bool bShown = false;
};

/** A text of the radar, target, status and warnings panels: sized in design units (scaled by the layout) and faded in at its delay. */
USTRUCT()
struct FSWGSpaceHudPanelText
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UTextBlock> Text;
	int32 FontSize = 15;
	float RevealDelay = 0.f;
};

/** An arc or bar image of a panel with the material that fills it. */
USTRUCT()
struct FSWGSpaceHudMeter
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UImage> Image;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;
	float LastFill = -1.f;
};

/** One row of the status panel: the component icon, name, condition bar and percentage. */
USTRUCT()
struct FSWGSpaceHudComponentRow
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UImage> Icon;
	UPROPERTY()
	TObjectPtr<UTextBlock> Label;
	UPROPERTY()
	TObjectPtr<UTextBlock> Percent;
	UPROPERTY()
	FSWGSpaceHudMeter Bar;
};

/** A flat-mode radar blip: a pooled sprite image, retargeted each frame. */
USTRUCT()
struct FSWGSpaceHudFlatBlip
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UImage> Image;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;
	FVector2D Size = FVector2D::ZeroVector;
};

/**
 * The holographic piloting HUD, built in C++: a screen-space layer of atlas sprites and procedural gauge arcs drawn through
 * the additive hologram materials (scanlines, sweep, flicker, grain, chromatic edge). Opened by the UI router as the
 * SpaceHud feature (USWGHoloUISubsystem). It ignores hits itself, so the HUD input configuration and mouse capture are untouched;
 * only the Comm and Eject buttons take them, and only where they are drawn.
 *
 * The screen is split into slots (ESWGSpaceHudSlot), each a canvas panel:
 * - Reticle: the retail ShipReticle bracket and glows, Speed / Weapon / Booster arc gauges, weapon condition indicators, the
 *   lead and aim marks and the off-screen target arrow.
 * - Radar: the 3D radar hologram (ASWGHoloRadarActor) in the world, or with swg.Space.HudFlat the flat disc; shield and armor
 *   arcs round it either way.
 * - Target: name, type, difficulty, faction badge and shield / armor / system / chassis percentages from the target ship own
 *   baselines, a rotating hologram of the ship (ASWGHoloShipActor) or a flat icon, and the world bracket (pt_ui_target_ship).
 * - Status: capacitor energy, missile and countermeasure counts, and the six component condition bars.
 * - Warnings: Incoming Missile (from CreateMissileMessage), and the incapacitation page with the eject button.
 * An element whose data the baselines do not carry stays hidden.
 */
UCLASS()
class SWGUIHOLO_API USWGSpaceHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* SpriteMaterialPath = TEXT("/Game/SWGEmu/Materials/M_SWGHoloUI.M_SWGHoloUI");
	static constexpr const TCHAR* ArcMaterialPath = TEXT("/Game/SWGEmu/Materials/M_SWGHoloUIArc.M_SWGHoloUIArc");
	static constexpr const TCHAR* RadarMaterialPath = TEXT("/Game/SWGEmu/Materials/M_SWGHoloRadar.M_SWGHoloRadar");

	/** True when the hologram materials exist and the retail sprite sheets are in the archives. The router offers the feature only then. */
	static bool CanBuild(UGameInstance* Game);

	/** swg.Space.HudFlat: the 3D parts (radar sphere, target ship) draw flat on the screen instead. */
	static bool IsFlat();

	/** Shows Data on the gauges and marks. A missing value hides its element. */
	void SetData(const FSWGSpaceHudData& Data);

	/** Gathers the pilot data from the object graph and applies it; false (and the HUD empties) when there is no ready ship. */
	bool RefreshFromWorld();

	/** Lays the slots out for a view of this size in slate units. Called from the widget tick; tests call it directly. */
	void UpdateLayout(const FVector2D& ViewSize);

	/** Advances the intro sweep, pulses, holograms and warnings. Called from the widget tick; tests call it with a fixed step. */
	void TickHud(float DeltaSeconds);

	/** Restarts the start-up sweep-in. */
	void StartIntro();

	/** Projects through this camera instead of the player one, for captures and tests. */
	void SetViewOverride(const TOptional<FSWGSpaceHudView>& View) { ViewOverride = View; }

	const FSWGSpaceHudLayout& GetLayout() const { return Layout; }
	UCanvasPanel* GetSlotPanel(ESWGSpaceHudSlot Region) const { return SlotPanels[int32(Region)]; }
	FSWGSpaceHudAtlas& GetAtlas() { return Atlas; }

	/** Fires after SetData, so other stages can read the same snapshot. */
	FSWGOnSpaceHudData OnDataApplied;

	/**
	 * Adds a holo sprite image to Parent (the caller positions its canvas slot) and registers it for the intro sweep and pulses.
	 * Null if the materials or the sprite sheet are missing.
	 */
	UImage* AddSprite(UCanvasPanel* Parent, const FSWGSpaceHudSprite& Sprite, const FLinearColor& Tint, float RevealDelay);

	/** Briefly brightens the element drawing Image (a value changed). */
	void PulseElement(const UImage* Image);

	// Read by tests.
	UImage* GetGaugeImage(ESWGSpaceHudGauge Gauge) const { return Gauges[int32(Gauge)].Image; }
	UMaterialInstanceDynamic* GetGaugeMaterial(ESWGSpaceHudGauge Gauge) const { return Gauges[int32(Gauge)].Material; }
	UTextBlock* GetGaugeText(ESWGSpaceHudGauge Gauge) const { return Gauges[int32(Gauge)].Text; }
	UImage* GetLeadImage() const { return LeadImage; }
	UImage* GetAimImage() const { return AimImage; }
	UImage* GetArrowImage() const { return ArrowImage; }
	UImage* GetReticleBracketImage() const { return BracketImages.IsEmpty() ? nullptr : BracketImages[0].Get(); }
	int32 GetVisibleWeaponIndicators() const;
	const TArray<FSWGSpaceHudElement>& GetElements() const { return Elements; }
	const TArray<FSWGSpaceHudText>& GetTexts() const { return Texts; }
	float GetIntroSeconds() const { return IntroSeconds; }

protected:
	virtual void NativeOnInitialized() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FSWGSpaceHudTest;
	friend class FSWGSpaceHudHoloTest;
	friend class FSWGSpaceHudCaptureTest;
#endif

	void EnsureBuilt();
	void BuildReticle();
	void BuildRadar();
	void BuildTargetPanel();
	void BuildStatusPanel();
	void BuildWarnings();
	void BuildGauge(ESWGSpaceHudGauge Gauge, float BeginTurns, float EndTurns, float RetailThicknessPixels, float RetailDiameter,
		const FLinearColor& OuterColor, const FLinearColor& InnerColor, float InnerOpacity, float Delay, const FBox2D& TextRect, const FLinearColor& TextColor);
	void AddNineSlice(const TCHAR* Style, const FLinearColor& Tint, float Delay);
	UImage* AddReticleSprite(const FSWGSpaceHudSprite& Sprite, const FLinearColor& Tint, const FBox2D& RetailRect, float Delay, float Opacity = 1.f);
	UTextBlock* AddReticleText(const FBox2D& RetailRect, int32 RetailFontPixels, const FLinearColor& Color, float Delay);
	FSWGSpaceHudElement* FindElement(const UImage* Image);
	UMaterialInstanceDynamic* MakeMaterial(bool bArc, const FSWGSpaceHudSprite* Sprite, const FLinearColor& Tint);
	void SetElementRegion(FSWGSpaceHudElement& Element, const FBox2D& ScreenRect);
	void PlaceFloatingImage(UImage* Image, const FVector2D& Centre, const FVector2D& Size);
	void ApplyGauge(ESWGSpaceHudGauge Gauge, bool bHasData, float Fill, const FText& Label);
	void ApplyTargetMarks(const FSWGSpaceHudData& Data);
	void ApplyWeapons(const FSWGSpaceHudData& Data);
	bool BuildView(FSWGSpaceHudView& OutView) const;

	// Panels (SWGSpaceHudWidgetPanels.cpp).
	UImage* AddPanelSprite(ESWGSpaceHudSlot Region, const TCHAR* SpriteName, const FLinearColor& Tint, float Delay);
	FSWGSpaceHudMeter AddPanelArc(ESWGSpaceHudSlot Region, float BeginTurns, float EndTurns, float Thickness, const FLinearColor& OuterColor,
		const FLinearColor& InnerColor, float InnerOpacity, float Delay);
	FSWGSpaceHudMeter AddPanelBar(ESWGSpaceHudSlot Region, float Delay);
	UTextBlock* AddPanelText(ESWGSpaceHudSlot Region, const FLinearColor& Color, ETextJustify::Type Justification, int32 FontSize, float Delay);
	UButton* AddPanelButton(ESWGSpaceHudSlot Region, UTextBlock*& OutLabel);
	void PlacePanelChild(UWidget* Child, ESWGSpaceHudSlot Region, const FVector2D& Position, const FVector2D& Size);
	void LayoutPanels();
	void ApplyRadar(const FSWGSpaceHudData& Data);
	void ApplyTargetPanel(const FSWGSpaceHudData& Data);
	void ApplyStatus(const FSWGSpaceHudData& Data);
	void ApplyWarnings(const FSWGSpaceHudData& Data);
	void SetMeterFill(FSWGSpaceHudMeter& Meter, float Fill, bool bBar);
	void EnsureHolograms(bool bWanted);
	void UpdateHolograms(float DeltaSeconds);
	void UpdateWarnings(float DeltaSeconds);
	void UpdateTargetBracket(const FSWGSpaceHudData& Data);
	void StopTargetBracket();
	void SendCommandNamed(const TCHAR* CommandName, int64 TargetId);
	UFUNCTION()
	void OnCommClicked();
	UFUNCTION()
	void OnEjectClicked();

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SpriteParent;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ArcParent;
	UPROPERTY()
	TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY()
	TObjectPtr<UCanvasPanel> SlotPanels[int32(ESWGSpaceHudSlot::Count)];
	UPROPERTY()
	TObjectPtr<UWidget> SlotFrames[int32(ESWGSpaceHudSlot::Count)];
	UPROPERTY()
	TArray<FSWGSpaceHudElement> Elements;
	UPROPERTY()
	TArray<FSWGSpaceHudText> Texts;
	UPROPERTY()
	FSWGSpaceHudGaugeView Gauges[int32(ESWGSpaceHudGauge::Count)];
	UPROPERTY()
	TArray<TObjectPtr<UImage>> BracketImages;
	UPROPERTY()
	TObjectPtr<UImage> LeadImage;
	UPROPERTY()
	TObjectPtr<UImage> AimImage;
	UPROPERTY()
	TObjectPtr<UImage> ArrowImage;
	/** Weapon indicators: a bracket and a condition text each, shown for the first N weapons. */
	UPROPERTY()
	TArray<TObjectPtr<UImage>> WeaponIcons;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> WeaponTexts;
	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	// 6c radar. The arcs are ordered right-top, left-top, right-bottom, left-bottom, as retail names its quadrants.
	UPROPERTY()
	FSWGSpaceHudMeter ShieldArcs[4];
	UPROPERTY()
	FSWGSpaceHudMeter ArmorArcs[4];
	UPROPERTY()
	TArray<TObjectPtr<UImage>> FlatRadarPieces;
	/** The flat disc pieces by role, all also in FlatRadarPieces. */
	UPROPERTY()
	TArray<TObjectPtr<UImage>> FlatGlows;
	UPROPERTY()
	TArray<TObjectPtr<UImage>> FlatRings;
	UPROPERTY()
	TArray<TObjectPtr<UImage>> FlatSpokes;
	UPROPERTY()
	TArray<FSWGSpaceHudFlatBlip> FlatBlips;
	UPROPERTY()
	TObjectPtr<UImage> FlatNorthTick;
	UPROPERTY()
	TObjectPtr<UTextBlock> RangeText;
	UPROPERTY()
	TObjectPtr<ASWGHoloRadarActor> RadarActor;

	// 6d target panel.
	UPROPERTY()
	TObjectPtr<ASWGHoloShipActor> TargetHologram;
	UPROPERTY()
	TObjectPtr<UTextBlock> TargetName;
	UPROPERTY()
	TObjectPtr<UTextBlock> TargetType;
	UPROPERTY()
	TObjectPtr<UTextBlock> TargetDifficulty;
	UPROPERTY()
	TObjectPtr<UTextBlock> TargetRowLabels[4];
	UPROPERTY()
	TObjectPtr<UTextBlock> TargetRowValues[4];
	UPROPERTY()
	TObjectPtr<UImage> FactionBadge;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FactionBadgeMaterial;
	UPROPERTY()
	TObjectPtr<UImage> FlatTargetIcon;
	UPROPERTY()
	TObjectPtr<UButton> CommButton;
	UPROPERTY()
	TObjectPtr<UTextBlock> CommLabel;

	// 6e status and warnings.
	UPROPERTY()
	TObjectPtr<UTextBlock> EnergyText;
	UPROPERTY()
	TObjectPtr<UTextBlock> AmmoLabels[3];
	UPROPERTY()
	TObjectPtr<UTextBlock> AmmoValues[3];
	UPROPERTY()
	FSWGSpaceHudComponentRow ComponentRows[int32(ESWGSpaceHudComponent::Count)];
	UPROPERTY()
	TObjectPtr<UTextBlock> IncomingMissileText;
	UPROPERTY()
	TObjectPtr<UImage> MissileGlows[2];
	UPROPERTY()
	TObjectPtr<UTextBlock> IncapText;
	UPROPERTY()
	FSWGSpaceHudMeter IncapBar;
	UPROPERTY()
	TObjectPtr<UButton> EjectButton;
	UPROPERTY()
	TObjectPtr<UTextBlock> EjectLabel;
	UPROPERTY()
	TArray<FSWGSpaceHudPanelText> PanelTexts;

	FSWGSpaceHudAtlas Atlas = FSWGSpaceHudAtlas(nullptr);
	FSWGSpaceHudLayout Layout;
	TOptional<FSWGSpaceHudView> ViewOverride;
	FSWGSpaceHudData LastData;
	float IntroSeconds = 0.f;
	float AppliedHoloStrength = -1.f;
	bool bSpeedInOverdrive = false;
	bool bBuilt = false;

	// The holograms screen regions (screen centre and sphere or hull radius in slate units), from LayoutPanels.
	FVector2D RadarSphereCentre = FVector2D::ZeroVector;
	float RadarSpherePixels = 0.f;
	FVector2D TargetHoloCentre = FVector2D::ZeroVector;
	float TargetHoloPixels = 0.f;

	FSWGEffectHandle TargetBracket;
	int64 BracketTargetId = 0;
	int64 TargetCommId = 0;
	float WarningSeconds = 0.f;
	double LastAlarmTime = -100.0;
};
