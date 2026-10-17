#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class USWGTreSubsystem;

/**
 * One rectangle of a retail HUD sprite sheet. Retail SourceRects are x0,y0,x1,y1 with the max edge exclusive; a reversed pair
 * (x1 < x0) draws the same pixels mirrored, which Rect and the mirror flags record separately.
 */
struct SWGUIHOLO_API FSWGSpaceHudSprite
{
	FString TexturePath;
	/** Normalised: Min <= Max on both axes. */
	FIntRect Rect;
	bool bMirrorX = false;
	bool bMirrorY = false;

	bool IsValid() const { return !TexturePath.IsEmpty() && Rect.Width() > 0 && Rect.Height() > 0; }
};

/**
 * The sprites of the space HUD, cut from texture/ui_space_hud.dds (a 512x512 white-on-alpha atlas tinted at runtime) and the
 * two target marks that are textures of their own. Reticle pieces come from ui_styles.inc through the TRE style sheet; the
 * glows, the off-screen arrow and the marks are the SourceRects of ui_hud_space_ship_reticle.inc, ui_hud_space.inc and
 * the textures themselves.
 */
class SWGUIHOLO_API FSWGSpaceHudAtlas
{
public:
	explicit FSWGSpaceHudAtlas(USWGTreSubsystem* InTre) : Tre(InTre) {}

	static const TCHAR* GetAtlasPath() { return TEXT("texture/ui_space_hud.dds"); }

	/**
	 * Named sprites: GlowLeft, GlowLeftBack, GlowRight, GlowRightBack, OffScreenArrow, TargetLead, TargetAim; the radar's BlipDefault,
	 * BlipStructure, BlipCorpse, BlipWaypoint, BlipBracket, ArrowBracket, ArrowDefault and RadarGlow; MissileGlowLeft / Right; and the
	 * components page's ComponentShield, Armor, Weapon, Reactor, Capacitor and Engine.
	 */
	bool Find(FName Name, FSWGSpaceHudSprite& Out) const;

	/** The retail badge of a ship faction string (rebel, imperial, hutt, nym, rsf); false for any other. */
	bool FindFactionBadge(const FString& Faction, FSWGSpaceHudSprite& Out) const;

	/**
	 * A piece of a retail rectangle style in the space reticle namespace: Style is its letter ("e"), Piece one of
	 * nw ne sw se n s w e c. The reticle brackets are these nine-slices, drawn here as separate pieces.
	 */
	bool FindReticlePiece(const TCHAR* Style, const TCHAR* Piece, FSWGSpaceHudSprite& Out) const;

	UTexture2D* LoadTexture(const FSWGSpaceHudSprite& Sprite) const;

	/**
	 * The sprite's rectangle as UVs (u0, v0, u1, v1) inside its texture, inset by half a texel so a linear sample never reads a
	 * neighbouring sprite. A mirrored sprite has u0 > u1 (or v0 > v1), which is how the materials flip it.
	 */
	FVector4f GetUvRect(const FSWGSpaceHudSprite& Sprite) const;

	/** Texels of the sprite whose alpha is at least Threshold, decoded from the DXT5 file itself. Used to check that a crop is not empty. */
	int32 CountVisibleTexels(const FSWGSpaceHudSprite& Sprite, uint8 Threshold = 32) const;

private:
	static FSWGSpaceHudSprite FromRetailRect(const FString& TexturePath, int32 X0, int32 Y0, int32 X1, int32 Y1);

	USWGTreSubsystem* Tre = nullptr;
};
