#pragma once

#include "CoreMinimal.h"

class FSWGIffReader;
struct FSWGIffChunk;

enum class ESWGCollisionExtentType : uint8
{
	Null,
	Box,
	Sphere,
	Cylinder,
	Mesh,
	Composite,
	Detail,
};

/**
 * One node of an appearance's extent tree — SWG's collision primitives
 * (BoxExtent, SphereExtent, CylinderExtent, MeshExtent, ComponentExtent,
 * DetailExtent). Already in UE units and axes (Y-up swapped to Z-up, scaled
 * by SWGWorldScale), local to the appearance, like FSWGFloorReader's output.
 */
struct SWGTRE_API FSWGCollisionExtent
{
	ESWGCollisionExtentType Type = ESWGCollisionExtentType::Null;

	/** Box: axis-aligned in appearance space. */
	FBox Box = FBox(ForceInit);

	/** Sphere: Center + Radius. Cylinder: Center is the base, Radius, Height along +Z. */
	FVector Center = FVector::ZeroVector;
	float Radius = 0.0f;
	float Height = 0.0f;

	/** Mesh: a triangle list. */
	TArray<FVector> Vertices;
	TArray<int32> Indices;

	/**
	 * Composite: every child blocks. Detail: coarse-to-fine alternatives,
	 * the last being the most accurate — retail tests them in order as
	 * successive rejects, so for building collision the last one is the shape.
	 */
	TArray<FSWGCollisionExtent> Children;
};

/** The collision-relevant part of FORM APPR — carried by .lod, .msh and .sat files. */
struct SWGTRE_API FSWGAppearanceCollision
{
	/** The render bounds; SWG collides against this when no CollisionExtent is authored. */
	TOptional<FSWGCollisionExtent> BoundingExtent;

	/** The authored blockers. Unset when the file has FORM NULL there. */
	TOptional<FSWGCollisionExtent> CollisionExtent;

	/** FLOR: the walkable surface (.flr), empty when the object has none — you stand on the terrain instead. */
	FString FloorPath;

	bool HasCollisionExtent() const { return CollisionExtent.IsSet() && CollisionExtent->Type != ESWGCollisionExtentType::Null; }
	bool HasAnything() const { return HasCollisionExtent() || BoundingExtent.IsSet() || !FloorPath.IsEmpty(); }
};

/**
 * Reads FORM APPR > FORM 000x > [extent] [collision extent] HPTS FLOR from
 * any appearance file (.lod's DTLA, .msh's MESH, .sat's SMAT). Layouts,
 * confirmed by dumping thm_tato_imprv_bridge_sml_s01.lod, core_decd_kenalpatree_a1.lod,
 * frn_impl_table_giant_s01.lod and frn_tech_command_console_s01.lod:
 *
 *   EXBX > 0001 > [EXSP sphere] BOX[max xyz, min xyz]
 *   EXSP > 0001 > SPHR[center xyz, radius]
 *   XCYL > 0000 > CYLN[base xyz, radius, height]
 *   CMSH > 0000 > IDTL > 0000 > VERT[xyz...] INDX[int32...]
 *   CMPT > 0000 > CPST > 0000 > children       (composite)
 *   DTAL > 0000 > CPST > 0000 > children       (detail, coarse to fine)
 *   NULL
 *   FLOR > DATA[uint8 hasFloor][path\0]
 */
class SWGTRE_API FSWGAppearanceCollisionReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGAppearanceCollision& OutCollision);

	/** Whether a FORM tag is one of the extent forms above. */
	static bool IsExtentForm(const FSWGIffChunk& Form);

private:
	static bool ReadExtent(const FSWGIffReader& Reader, const FSWGIffChunk& Form, FSWGCollisionExtent& OutExtent);
	static bool ReadChildrenExtents(const FSWGIffReader& Reader, const FSWGIffChunk& Form, TArray<FSWGCollisionExtent>& OutChildren);

	FSWGAppearanceCollisionReader() = default;
};
