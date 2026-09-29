#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/** One resource spawn a survey tool can look for. */
struct SWGEMU_API FSWGSurveyResourceEntry
{
	/** The spawn's generated name ("Aqabuo"); what requestsurvey/requestcoresample take. */
	FString Name;
	uint64 ObjectId = 0;
	/** resource_tree.iff ENUM of the spawn's final class ("copper_desh"). */
	FString Type;
};

/**
 * Core3 ResourceSpawner::sendResourceListForSurvey, sent when a survey tool is
 * used. The header comment in ResourceListForSurveyMessage.h is wrong: the type
 * is per entry (addResource), and the tool's survey type follows the list.
 */
struct SWGEMU_API FResourceListForSurveyMessage : public FSWGNetMessage
{
	TArray<FSWGSurveyResourceEntry> Resources;
	/** The tool template's surveyType ("mineral", "flora_resources", ...). */
	FString SurveyType;
	uint64 PlayerId = 0;

	FResourceListForSurveyMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};

/** One grid point of a survey: raw position and density 0-1. */
struct SWGEMU_API FSWGSurveyPoint
{
	float X = 0.f;
	float Y = 0.f;
	float Density = 0.f;
};

/**
 * Core3 ResourceSpawner::sendSurvey: an N x N grid (N = 3-5 by tool range)
 * centred on the player, rows running north to south. Wire order per point is
 * X, Z (always 0), Y, density.
 */
struct SWGEMU_API FSurveyMessage : public FSWGNetMessage
{
	TArray<FSWGSurveyPoint> Points;

	FSurveyMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};
