#pragma once

#include "CoreMinimal.h"
#include "SWGMapMarkerWidget.h"

class USWGTreSubsystem;
class USWGWaypointSubsystem;
struct FSWGMapLocation;
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

	/**
	 * Nearby server map locations; detail 1 shows services, detail 2 adds terminals.
	 * Tre resolves the retail type names and "@table:key" names; null falls back to English.
	 */
	SWGUI_API TArray<FSWGMapMarker> MakeLocationMarkers(const TArray<FSWGMapLocation>& Locations,
		const FVector2D& Center, float Radius, int32 Detail, USWGTreSubsystem* Tre = nullptr);

	/** The local player as a heading marker; false without a pawn. */
	SWGUI_API bool MakePlayerMarker(const APawn* Pawn, FSWGMapMarker& OutMarker);
}
