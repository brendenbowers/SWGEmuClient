#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** Who the player is fighting, which picks the combat music. File order within each tag is creature, NPC, player. */
enum class ESWGCombatMusicRole : uint8
{
	Creature,
	Npc,
	Player,
	Count,
};

/**
 * scene/game_music_manager.iff: FORM GMUS > FORM 0000 > one chunk per sound, each a
 * null-terminated .snd path. Repeated tags are indexed in file order.
 *
 * Ground combat, per ESWGCombatMusicRole: ENTR = the fight loop (_lp), EXIT = the victory
 * sting (_vict), DETH = the player-died sting (_death), INCP = the incapacitated sting (_def).
 * Space: SENT = battle loop, SEXI = victory sting (three pairs). WATR = ambient played at the
 * shore/water edge (not combat; kept for the ambience work).
 */
struct FSWGGameMusicData
{
	TArray<FString> CombatEnter;
	TArray<FString> CombatExit;
	TArray<FString> CombatDeath;
	TArray<FString> CombatIncapacitated;
	TArray<FString> SpaceEnter;
	TArray<FString> SpaceExit;
	TArray<FString> Water;

	/** The .snd for a role, or an empty string when the file has no entry. */
	static const FString& ForRole(const TArray<FString>& PerRole, ESWGCombatMusicRole Role)
	{
		static const FString Empty;
		return PerRole.IsValidIndex(static_cast<int32>(Role)) ? PerRole[static_cast<int32>(Role)] : Empty;
	}
};

class SWGTRE_API FSWGGameMusicReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGGameMusicData& OutData);
};
