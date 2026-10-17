#include "SWGSpaceHudAtlas.h"
#include "Engine/Texture2D.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	/** The alpha of texel Index (0..15, row-major) of one 16-byte DXT5 block. */
	uint8 DecodeDxt5Alpha(const uint8* Block, int32 Index)
	{
		const int32 Alpha0 = Block[0], Alpha1 = Block[1];
		uint64 Bits = 0;
		for (int32 ByteIndex = 0; ByteIndex < 6; ++ByteIndex) Bits |= uint64(Block[2 + ByteIndex]) << (8 * ByteIndex);
		const int32 Code = int32((Bits >> (3 * Index)) & 7);
		if (Code == 0) return uint8(Alpha0);
		if (Code == 1) return uint8(Alpha1);
		if (Alpha0 > Alpha1) return uint8(((8 - Code) * Alpha0 + (Code - 1) * Alpha1) / 7);
		if (Code == 6) return 0;
		if (Code == 7) return 255;
		return uint8(((6 - Code) * Alpha0 + (Code - 1) * Alpha1) / 5);
	}
}

FSWGSpaceHudSprite FSWGSpaceHudAtlas::FromRetailRect(const FString& TexturePath, int32 X0, int32 Y0, int32 X1, int32 Y1)
{
	FSWGSpaceHudSprite Sprite;
	Sprite.TexturePath = TexturePath;
	Sprite.Rect = FIntRect(FMath::Min(X0, X1), FMath::Min(Y0, Y1), FMath::Max(X0, X1), FMath::Max(Y0, Y1));
	Sprite.bMirrorX = X1 < X0;
	Sprite.bMirrorY = Y1 < Y0;
	return Sprite;
}

bool FSWGSpaceHudAtlas::Find(FName Name, FSWGSpaceHudSprite& Out) const
{
	const FString Atlas = GetAtlasPath();
	// ui_hud_space_ship_reticle.inc: the centre page's glow images (the left arcs; the right ones are the same shapes mirrored).
	if (Name == TEXT("GlowLeft")) { Out = FromRetailRect(Atlas, 446, 42, 510, 198); return true; }
	if (Name == TEXT("GlowLeftBack")) { Out = FromRetailRect(Atlas, 209, 15, 252, 144); return true; }
	if (Name == TEXT("GlowRight")) { Out = FromRetailRect(Atlas, 511, 204, 449, 322); return true; }
	if (Name == TEXT("GlowRightBack")) { Out = FromRetailRect(Atlas, 292, 193, 244, 296); return true; }
	// ui_hud_space.inc: offScreenIndicator, an arrow pointing right.
	if (Name == TEXT("OffScreenArrow")) { Out = FromRetailRect(Atlas, 214, 392, 230, 419); return true; }
	// ui_hud_space.inc / ui_hud_space_radar_spherical.inc: the radar's blip and arrow images (namespace images.blips / images.arrows).
	if (Name == TEXT("BlipDefault")) { Out = FromRetailRect(Atlas, 400, 39, 408, 46); return true; }
	if (Name == TEXT("BlipStructure")) { Out = FromRetailRect(Atlas, 401, 47, 409, 54); return true; }
	if (Name == TEXT("BlipCorpse")) { Out = FromRetailRect(Atlas, 400, 55, 408, 62); return true; }
	if (Name == TEXT("BlipWaypoint")) { Out = FromRetailRect(Atlas, 400, 62, 408, 70); return true; }
	if (Name == TEXT("BlipBracket")) { Out = FromRetailRect(Atlas, 399, 99, 408, 107); return true; }
	if (Name == TEXT("ArrowBracket")) { Out = FromRetailRect(Atlas, 398, 83, 409, 95); return true; }
	if (Name == TEXT("ArrowDefault")) { Out = FromRetailRect(Atlas, 412, 82, 425, 103); return true; }
	// ui_hud_space.inc missileLockOnYou: the red edge glows, left and right (the right is the left mirrored).
	if (Name == TEXT("MissileGlowLeft")) { Out = FromRetailRect(Atlas, 354, 202, 448, 293); return true; }
	if (Name == TEXT("MissileGlowRight")) { Out = FromRetailRect(Atlas, 448, 202, 354, 293); return true; }
	// ui_styles.inc space.Icon.components: the components page's icons.
	if (Name == TEXT("ComponentShield")) { Out = FromRetailRect(Atlas, 482, 345, 506, 369); return true; }
	if (Name == TEXT("ComponentArmor")) { Out = FromRetailRect(Atlas, 481, 453, 506, 474); return true; }
	if (Name == TEXT("ComponentWeapon")) { Out = FromRetailRect(Atlas, 478, 371, 509, 392); return true; }
	if (Name == TEXT("ComponentReactor")) { Out = FromRetailRect(Atlas, 481, 397, 505, 421); return true; }
	if (Name == TEXT("ComponentCapacitor")) { Out = FromRetailRect(Atlas, 481, 425, 505, 449); return true; }
	if (Name == TEXT("ComponentEngine")) { Out = FromRetailRect(Atlas, 482, 482, 506, 506); return true; }
	// ui_hud_space.inc radarGlow: the radar gauge's glow disc (a texture of its own, 128x128).
	if (Name == TEXT("RadarGlow")) { Out = FromRetailRect(TEXT("texture/ui_space_radar_gauge.dds"), 0, 0, 128, 128); return true; }
	if (Name == TEXT("TargetLead") || Name == TEXT("TargetAim"))
	{
		Out = FSWGSpaceHudSprite();
		Out.TexturePath = Name == TEXT("TargetLead") ? TEXT("texture/ui_target_lead.dds") : TEXT("texture/ui_target_aim.dds");
		const UTexture2D* Texture = LoadTexture(Out);
		if (!Texture) return false;
		Out.Rect = FIntRect(0, 0, Texture->GetSizeX(), Texture->GetSizeY());
		return true;
	}
	return false;
}

bool FSWGSpaceHudAtlas::FindFactionBadge(const FString& Faction, FSWGSpaceHudSprite& Out) const
{
	// The faction badges retail ships: ui_space_faction_{rebel,imperial,hutt,nym,rsf}.dds, 64x64 each.
	static const TCHAR* const Factions[] = {TEXT("rebel"), TEXT("imperial"), TEXT("hutt"), TEXT("nym"), TEXT("rsf")};
	for (const TCHAR* Known : Factions)
	{
		if (!Faction.Equals(Known, ESearchCase::IgnoreCase)) continue;
		Out = FSWGSpaceHudSprite();
		Out.TexturePath = FString::Printf(TEXT("texture/ui_space_faction_%s.dds"), Known);
		const UTexture2D* Texture = LoadTexture(Out);
		if (!Texture) return false;
		Out.Rect = FIntRect(0, 0, Texture->GetSizeX(), Texture->GetSizeY());
		return true;
	}
	return false;
}

bool FSWGSpaceHudAtlas::FindReticlePiece(const TCHAR* Style, const TCHAR* Piece, FSWGSpaceHudSprite& Out) const
{
	const FSWGUIStyleSheet* Sheet = Tre ? Tre->GetUIStyleSheet() : nullptr;
	const FSWGUIImageStyle* ImageStyle = Sheet ? Sheet->FindImageStyle(FString::Printf(TEXT("space.reticles.%s.default.%s"), Style, Piece)) : nullptr;
	if (!ImageStyle) return false;
	const FIntRect& Source = ImageStyle->SourceRect;
	Out = FromRetailRect(FString::Printf(TEXT("texture/%s.dds"), *ImageStyle->Source), Source.Min.X, Source.Min.Y, Source.Max.X, Source.Max.Y);
	return Out.IsValid();
}

UTexture2D* FSWGSpaceHudAtlas::LoadTexture(const FSWGSpaceHudSprite& Sprite) const
{
	return Tre && !Sprite.TexturePath.IsEmpty() ? Tre->GetOrLoadTexture(Sprite.TexturePath) : nullptr;
}

FVector4f FSWGSpaceHudAtlas::GetUvRect(const FSWGSpaceHudSprite& Sprite) const
{
	const UTexture2D* Texture = LoadTexture(Sprite);
	if (!Texture || !Sprite.IsValid()) return FVector4f(0.f, 0.f, 1.f, 1.f);
	const float Width = Texture->GetSizeX(), Height = Texture->GetSizeY();
	// A one-texel-wide strip collapses to its centre line, which is what stretching it should show.
	const float InsetX = FMath::Min(0.5f, Sprite.Rect.Width() * 0.5f), InsetY = FMath::Min(0.5f, Sprite.Rect.Height() * 0.5f);
	float U0 = (Sprite.Rect.Min.X + InsetX) / Width, U1 = (Sprite.Rect.Max.X - InsetX) / Width;
	float V0 = (Sprite.Rect.Min.Y + InsetY) / Height, V1 = (Sprite.Rect.Max.Y - InsetY) / Height;
	if (Sprite.bMirrorX) Swap(U0, U1);
	if (Sprite.bMirrorY) Swap(V0, V1);
	return FVector4f(U0, V0, U1, V1);
}

int32 FSWGSpaceHudAtlas::CountVisibleTexels(const FSWGSpaceHudSprite& Sprite, uint8 Threshold) const
{
	const TArray<uint8> File = Tre ? Tre->ExtractFile(Sprite.TexturePath) : TArray<uint8>();
	if (File.Num() < 128 || FMemory::Memcmp(File.GetData() + 84, "DXT5", 4) != 0) return 0;
	const int32 Height = *reinterpret_cast<const int32*>(File.GetData() + 12);
	const int32 Width = *reinterpret_cast<const int32*>(File.GetData() + 16);
	const int32 BlocksPerRow = Width / 4;
	int32 Visible = 0;
	for (int32 Y = FMath::Max(0, Sprite.Rect.Min.Y); Y < FMath::Min(Height, Sprite.Rect.Max.Y); ++Y)
	{
		for (int32 X = FMath::Max(0, Sprite.Rect.Min.X); X < FMath::Min(Width, Sprite.Rect.Max.X); ++X)
		{
			const int64 BlockOffset = 128 + int64((Y / 4) * BlocksPerRow + (X / 4)) * 16;
			if (BlockOffset + 16 > File.Num()) continue;
			if (DecodeDxt5Alpha(File.GetData() + BlockOffset, (Y % 4) * 4 + (X % 4)) >= Threshold) ++Visible;
		}
	}
	return Visible;
}
