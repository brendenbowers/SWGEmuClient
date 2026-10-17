#pragma once

#include "CoreMinimal.h"

struct FSWGDataTableData;

/** One row of datatables/space/asteroidfield/<zone>.iff. Positions are native units (x east, y up, z north). */
struct FSWGAsteroidFieldData
{
	FString Name;
	/** Spline fields are a tube of Radius around the control points; the others a sphere around Center. */
	bool bSpline = false;
	FVector Center = FVector::ZeroVector;
	TArray<FVector> ControlPoints;
	float Radius = 0;
	FString RadialStyleTable;
	int32 NumRadialAsteroids = 0;
	float MaxViewableDistance = 0;
	float ViewAllDistance = 0;
	/** Multiplier on the asteroids' native size, drawn between the two. Every retail row is 1..1. */
	float ScaleMin = 1;
	float ScaleMax = 1;
};

/** One row of a radial style table (datatables/space/asteroidfield/fieldstyle/radial_*.iff): a mesh that may appear among a field's near asteroids. */
struct FSWGAsteroidRadialStyleRow
{
	/** Appearance path, e.g. appearance/asteroid_radial_s01.apt. */
	FString Appearance;
	/** Relative chance of this row among the table's rows. */
	float Likelihood = 1;
	/** Multiplier on the mesh's native size, drawn between the two (retail: 0.5..1 for rocks, 1..1 for ship debris). */
	float ScaleMin = 1;
	float ScaleMax = 1;
	/** Both 100 in every retail table; read as metres around the viewer within which near asteroids appear and beyond which they are recycled. */
	float SpawnRange = 0;
	float DeleteRange = 0;
	FString HitEffect;
};

class SWGTRE_API FSWGAsteroidFieldReader
{
public:
	/** Reads the zone's field rows. The ScaleMin/ScaleMax columns are optional (default 1); false on a missing required column or a bad numeric cell. */
	static bool Read(const FSWGDataTableData& Table, TArray<FSWGAsteroidFieldData>& Out);

	/** Reads a radial style table. False if a column is missing, the table is empty, or a row has a non-finite or out-of-order value. */
	static bool ReadRadialStyle(const FSWGDataTableData& Table, TArray<FSWGAsteroidRadialStyleRow>& Out);
};
