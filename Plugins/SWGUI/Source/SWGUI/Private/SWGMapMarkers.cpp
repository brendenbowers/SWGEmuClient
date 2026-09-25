#include "SWGMapMarkers.h"
#include "Common/SWGWorldScale.h"
#include "Network/Messages/Zone/MapLocationMessages.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGWaypointSubsystem.h"
#include "TRE/SWGCrc32.h"

namespace
{
	struct FLocationType
	{
		uint8 Category;
		/** 0 matches any subcategory. */
		uint8 Subcategory;
		/** map_loc_cat_n key; the English text is the fallback without TRE data. */
		const TCHAR* Key;
		const TCHAR* Fallback;
		bool bTerminal;
		FLinearColor Color;
	};

	// Hues kept clear of the hologram's own blue; a terminal shares its service's colour.
	const FLinearColor Gold(1.f, 0.8f, 0.2f);
	const FLinearColor Orange(1.f, 0.5f, 0.15f);
	const FLinearColor Medical(1.f, 0.3f, 0.35f);
	const FLinearColor TravelColor(0.35f, 1.f, 0.35f);
	const FLinearColor Lime(0.75f, 1.f, 0.25f);
	const FLinearColor Magenta(1.f, 0.4f, 1.f);
	const FLinearColor Civic(0.92f, 0.92f, 0.92f);

	// Core3 MapLocationType ids. Trainers and vendors are left out: too many to be useful at city scale.
	const FLocationType LocationTypes[] = {
		{ 2, 0, TEXT("bank"), TEXT("Bank"), false, Gold },
		{ 3, 0, TEXT("cantina"), TEXT("Cantina"), false, Orange },
		{ 4, 0, TEXT("capitol"), TEXT("Capitol"), false, Civic },
		{ 5, 0, TEXT("cloningfacility"), TEXT("Cloning Facility"), false, Medical },
		{ 6, 0, TEXT("garage"), TEXT("Parking Garage"), false, Civic },
		{ 7, 0, TEXT("guild"), TEXT("Guild Hall"), false, Civic },
		{ 12, 0, TEXT("hotel"), TEXT("Hotel"), false, Civic },
		{ 13, 0, TEXT("medicalcenter"), TEXT("Medical Center"), false, Medical },
		{ 14, 0, TEXT("shuttleport"), TEXT("Shuttleport"), false, TravelColor },
		{ 15, 0, TEXT("starport"), TEXT("Starport"), false, TravelColor },
		{ 16, 0, TEXT("themepark"), TEXT("Theme Park"), false, Civic },
		{ 26, 0, TEXT("junkshop"), TEXT("Junk Shop"), false, Civic },
		{ 27, 0, TEXT("tavern"), TEXT("Tavern"), false, Orange },
		{ 50, 0, TEXT("cityhall"), TEXT("City Hall"), false, Civic },
		{ 55, 0, TEXT("garage"), TEXT("Parking Garage"), false, Civic },
		{ 56, 0, TEXT("museum"), TEXT("Museum"), false, Civic },
		{ 57, 0, TEXT("salon"), TEXT("Salon"), false, Civic },
		{ 41, 42, TEXT("terminal_bank"), TEXT("Bank Terminal"), true, Gold },
		{ 41, 43, TEXT("terminal_bazaar"), TEXT("Bazaar Terminal"), true, Lime },
		{ 41, 44, TEXT("terminal_mission"), TEXT("Mission Terminal"), true, Magenta },
		{ 41, 0, TEXT("terminal"), TEXT("Terminal"), true, Civic },
	};

	const FLocationType* FindLocationType(const FSWGMapLocation& Location)
	{
		for (const FLocationType& Type : LocationTypes)
		{
			if (Type.Category == Location.Category && (Type.Subcategory == 0 || Type.Subcategory == Location.Subcategory))
			{
				return &Type;
			}
		}
		return nullptr;
	}
}

namespace SWGMapMarkers
{
	TArray<FSWGMapMarker> MakeLocationMarkers(const TArray<FSWGMapLocation>& Locations,
		const FVector2D& Center, float Radius, int32 Detail, USWGTreSubsystem* Tre)
	{
		TArray<FSWGMapMarker> Markers;
		if (Detail == 0)
		{
			return Markers;
		}
		for (const FSWGMapLocation& Location : Locations)
		{
			if (FVector2D::DistSquared(Location.Position, Center) > FMath::Square(Radius))
			{
				continue;
			}
			const FLocationType* Type = FindLocationType(Location);
			if (!Type || (Type->bTerminal && Detail < 2))
			{
				continue;
			}
			FString TypeName = Tre ? Tre->LookupString(TEXT("map_loc_cat_n"), Type->Key) : FString();
			if (TypeName.IsEmpty())
			{
				TypeName = Type->Fallback;
			}
			// Core3 sends "@table:key" names (city regions, unnamed objects).
			const FString Name = Tre ? Tre->ResolveStringId(Location.Name) : Location.Name;
			FSWGMapMarker& Marker = Markers.AddDefaulted_GetRef();
			Marker.Id = FName(*LexToString(Location.ObjectId));
			Marker.Position = Location.Position;
			Marker.Label = FText::FromString(Name.IsEmpty() || Name == TypeName
				? TypeName : FString::Printf(TEXT("%s: %s"), *TypeName, *Name));
			Marker.bCustomPinColor = true;
			Marker.PinColor = Type->Color;
			Marker.LabelColor = Marker.PinColor;
		}
		return Markers;
	}

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
