

#pragma once

#include "CoreMinimal.h"
class FSWGIffReader;

struct FSWGPobPortalRef
{
    int32   PortalNumber;
    int32   ConnectingCellIndex;
    bool    bIsPassable;
    bool    bIsGeometryWindingClockwise;
    bool    bHasDoorHardpoint;  // agrees with DoorStyle.IsEmpty() on all 24 validated records
    FString DoorStyle;          // empty = open archway, no door object
    FTransform DoorHardpoint;   // shared building-local space (Sheet 00 §00.4); always present
	TArray<FVector> OpeningVertices;  // empty if portal has FORM NULL instead of FORM CMSH   
};

enum class ESWGPobLightType : uint8 { Ambient = 0, Parallel = 1, Point = 2 };

struct FSWGPobLight
{
    FLinearColor DiffuseColor;   // Alpha,Red,Green,Blue floats (Sheet 00 §00.6)
    FLinearColor SpecularColor;
    float ConstantAttenuation;
    float LinearAttenuation;
    float QuadraticAttenuation;
    ESWGPobLightType Type;
    FTransform   Transform;      // 48 bytes, position = translation column
};

struct FSWGPobCell
{
    int16   PortalCount;         // numberOfPortals field (Sheet 00 §00.2) is a wire int32; stored narrower here since real counts are tiny (max 9 in the corpus)
    bool    CanSeeParent;
    int32   CellIndex;          // 0 = exterior
    FString CellName;
    FString MeshPath;
    TArray<FVector> CollisionVertices;  // empty if cell has FORM NULL instead of FORM CMSH
    TArray<int32>   CollisionIndices;   // triangle list, empty = no embedded collision mesh
    FString CollisionFloorPath; // may be empty, separate from Collision*/Indices above (Sheet 00 §00.5)
    TArray<FSWGPobPortalRef> Portals;
    TArray<FSWGPobLight> Lights;
};

struct FSWGPobData { TArray<FSWGPobCell> Cells; };

/**
 * 
 */
class SWGTRE_API FSWGPobReader
{
public:
    static bool ReadPob(const FSWGIffReader& Reader, FSWGPobData& OutPob);
private:
	FSWGPobReader() = default;
};
