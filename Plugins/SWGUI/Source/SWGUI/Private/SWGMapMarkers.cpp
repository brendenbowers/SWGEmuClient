#include "SWGMapMarkers.h"
#include "Common/SWGWorldScale.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/SWGWaypointSubsystem.h"
#include "TRE/SWGCrc32.h"

namespace SWGMapMarkers
{
	TArray<FSWGMapMarker> MakeWaypointMarkers(const USWGWaypointSubsystem* Waypoints, const FString& Planet)
	{
		TArray<FSWGMapMarker> Markers;
		if (!Waypoints)
		{
			return Markers;
		}
		// Core3's waypoint planet CRC is the zone name's String::hashCode.
		const int32 PlanetCrc = static_cast<int32>(FSWGCrc32::HashString(Planet.ToLower()));
		for (const FSWGWaypointEntry& Waypoint : Waypoints->GetWaypoints())
		{
			if (Waypoint.PlanetCRC != PlanetCrc || Waypoint.CellId != 0)
			{
				continue;
			}
			FSWGMapMarker& Marker = Markers.AddDefaulted_GetRef();
			Marker.Id = FName(*LexToString(Waypoint.WaypointObjectId));
			Marker.Style = WaypointStyle;
			Marker.Position = FVector2D(Waypoint.RawPosition.X, Waypoint.RawPosition.Y);
			Marker.Label = Waypoint.Name;
			Marker.bCustomPinColor = true;
			Marker.PinColor = USWGWaypointSubsystem::GetWaypointColor(Waypoint.Color);
			Marker.PinColor.A = Waypoint.bActive ? 1.f : 0.55f;
			Marker.LabelColor = Marker.PinColor;
		}
		return Markers;
	}

	bool MakePlayerMarker(const APawn* Pawn, FSWGMapMarker& OutMarker)
	{
		if (!Pawn)
		{
			return false;
		}
		const FVector Raw = SWGToRawSpace(Pawn->GetActorLocation());
		OutMarker = FSWGMapMarker();
		OutMarker.Id = TEXT("Self");
		OutMarker.Style = PlayerStyle;
		OutMarker.Position = FVector2D(Raw.X, Raw.Y);
		OutMarker.bHasHeading = true;
		// UE yaw 0 is +X, which is north here, and turns toward east (+Y).
		OutMarker.Heading = Pawn->GetActorRotation().Yaw;
		return true;
	}
}
