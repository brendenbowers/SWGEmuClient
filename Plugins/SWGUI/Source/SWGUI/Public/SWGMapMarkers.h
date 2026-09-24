#pragma once

#include "CoreMinimal.h"
#include "SWGMapMarkerWidget.h"

class USWGWaypointSubsystem;
class APawn;

/** Marker sources shared by every map view (ticket window, planet map, hologram). */
namespace SWGMapMarkers
{
	/** Style the default marker widget and the hologram draw as a heading arrow. */
	const FName PlayerStyle(TEXT("Player"));
	const FName WaypointStyle(TEXT("Waypoint"));

	/**
	 * The player's waypoints on Planet ("tatooine"), coloured as their datapad
	 * colour, inactive ones dimmed. Ids are the waypoint object ids. Waypoints
	 * inside a cell are left out: their position is local to the room.
	 */
	SWGUI_API TArray<FSWGMapMarker> MakeWaypointMarkers(const USWGWaypointSubsystem* Waypoints, const FString& Planet);

	/** The local player as a heading marker; false without a pawn. */
	SWGUI_API bool MakePlayerMarker(const APawn* Pawn, FSWGMapMarker& OutMarker);
}
