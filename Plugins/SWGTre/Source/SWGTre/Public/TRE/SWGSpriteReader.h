#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * appearance/**.spr, FORM QUAD (452 files: billboard cards for far trees/flora, bolt sprites, waypoint icon)
 * or FORM SPRT (UI icons) > 0000: 4 floats, 4 floats, shader.
 *
 * The first four floats read as a bounding sphere: centre x,y,z then radius (inferred: conf_mtns_sparse = 0, 9.17, 0, 20.2
 * is a 20 m tree card centred at mid-height; ui_waypoint = 0,0,0,0.5; sprite_template = 0,0,0,10). The next
 * four are 1,1,1,1 in every file seen (RGBA tint, inferred). The shader is a bare name for QUAD
 * ("redbolt_a1_aaef17" -> shader/redbolt_a1_aaef17.sht) and a full path for SPRT.
 */
struct SWGTRE_API FSWGSprite
{
	bool bIsQuad = true;
	FVector3f BoundsCenter = FVector3f::ZeroVector;
	float BoundsRadius = 0.f;
	FLinearColor Tint = FLinearColor::White;
	/** As stored. */
	FString ShaderName;

	/** shader/<name>.sht unless the stored name is already a path. */
	FString GetShaderPath() const;
};

class SWGTRE_API FSWGSpriteReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGSprite& OutSprite);
};
