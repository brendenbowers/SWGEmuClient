#include "Subsystems/SWGCombatMusicSubsystem.h"
#include "SWGLogCategories.h"

#include "Common/SWGPostureTypes.h"
#include "Components/SWGCombatStateComponent.h"
#include "Network/Messages/Zone/Object/CombatActionIn.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "SWGAudioSubsystem.h"
#include "SWGMusicManager.h"
#include "Subsystems/SWGCombatSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	const TCHAR* GameMusicPath = TEXT("scene/game_music_manager.iff");
}

void USWGCombatMusicSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Combat = Cast<USWGCombatSubsystem>(Collection.InitializeDependency(USWGCombatSubsystem::StaticClass()));
	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));
	Tre = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));
	Audio = Cast<USWGAudioSubsystem>(Collection.InitializeDependency(USWGAudioSubsystem::StaticClass()));

	if (Combat)
	{
		ResolvedHandle = Combat->OnCombatActionResolved.AddUObject(this, &USWGCombatMusicSubsystem::HandleCombatActionResolved);
	}
}

void USWGCombatMusicSubsystem::Deinitialize()
{
	if (Combat && ResolvedHandle.IsValid())
	{
		Combat->OnCombatActionResolved.Remove(ResolvedHandle);
		ResolvedHandle.Reset();
	}

	ClearCombatLayer();
	MusicData.Reset();
	Combat = nullptr;
	ObjectGraph = nullptr;
	Tre = nullptr;
	Audio = nullptr;
	Super::Deinitialize();
}

TStatId USWGCombatMusicSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGCombatMusicSubsystem, STATGROUP_Tickables);
}

ESWGCombatMusicRole USWGCombatMusicSubsystem::ClassifyTemplate(const FString& TemplatePath)
{
	if (TemplatePath.Contains(TEXT("/creature/player/")))
	{
		return ESWGCombatMusicRole::Player;
	}
	if (TemplatePath.Contains(TEXT("dressed_")))
	{
		return ESWGCombatMusicRole::Npc;
	}
	return ESWGCombatMusicRole::Creature;
}

ESWGCombatMusicRole USWGCombatMusicSubsystem::ClassifyObject(int64 ObjectId) const
{
	const ISWGNetworkObjectInterface* NetworkObject = Cast<const ISWGNetworkObjectInterface>(ObjectGraph ? ObjectGraph->FindActor(ObjectId) : nullptr);
	return NetworkObject && Tre ? ClassifyTemplate(Tre->ResolveTemplatePath(NetworkObject->GetObjectCrc())) : ESWGCombatMusicRole::Creature;
}

const FSWGGameMusicData* USWGCombatMusicSubsystem::GetMusicData()
{
	if (!bMusicLoadAttempted && Tre)
	{
		bMusicLoadAttempted = true;
		MusicData = MakeShared<FSWGGameMusicData>();
		if (!FSWGGameMusicReader::Read(Tre->CreateIffReader(GameMusicPath), *MusicData))
		{
			UE_LOG(LogSWGCombat, Warning, TEXT("could not read %s - no combat music"), GameMusicPath);
			MusicData.Reset();
		}
	}
	return MusicData.Get();
}

void USWGCombatMusicSubsystem::HandleCombatActionResolved(const FCombatActionIn& Action, const TArray<FSWGResolvedCombatDefender>& Defenders)
{
	const int64 PlayerId = ObjectGraph ? ObjectGraph->GetLocalPlayerObjectId() : 0;
	if (PlayerId == 0)
	{
		return;
	}

	if (Action.AttackerId == PlayerId && !Defenders.IsEmpty() && Defenders[0].Defender)
	{
		LastOpponentId = Defenders[0].Defender->ObjectId;
	}
	else if (Action.AttackerId != PlayerId)
	{
		for (const FSWGResolvedCombatDefender& Resolved : Defenders)
		{
			if (Resolved.Defender && Resolved.Defender->ObjectId == PlayerId)
			{
				LastOpponentId = Action.AttackerId;
				break;
			}
		}
	}
}

void USWGCombatMusicSubsystem::SetCombatLayer(const TArray<FString>& Entries, ESWGCombatMusicRole RoleToPlay)
{
	USWGMusicManager* Music = Audio ? Audio->GetMusicManager() : nullptr;
	if (Music)
	{
		Music->SetLayer(ESWGMusicLayer::Combat, FSWGGameMusicData::ForRole(Entries, RoleToPlay));
	}
}

void USWGCombatMusicSubsystem::ClearCombatLayer()
{
	if (USWGMusicManager* Music = Audio ? Audio->GetMusicManager() : nullptr)
	{
		Music->SetLayer(ESWGMusicLayer::Combat, FString());
	}
}

void USWGCombatMusicSubsystem::StartCombat(int64 PlayerId)
{
	const FSWGGameMusicData* Data = GetMusicData();
	if (!Data)
	{
		return;
	}

	const USWGCombatStateComponent* CombatState = ObjectGraph->FindComponent<USWGCombatStateComponent>(PlayerId);
	OpponentId = CombatState && CombatState->TargetId != 0 ? CombatState->TargetId : LastOpponentId;
	Role = OpponentId != 0 ? ClassifyObject(OpponentId) : ESWGCombatMusicRole::Creature;

	UE_LOG(LogSWGCombat, Log, TEXT("combat music: opponent %lld, role %d"), OpponentId, static_cast<int32>(Role));
	SetCombatLayer(Data->CombatEnter, Role);
}

void USWGCombatMusicSubsystem::EndCombat()
{
	const FSWGGameMusicData* Data = GetMusicData();
	const USWGCombatStateComponent* OpponentState = OpponentId != 0 ? ObjectGraph->FindComponent<USWGCombatStateComponent>(OpponentId) : nullptr;

	// Victory only when the opponent actually fell (or is already gone); walking away or a
	// peace leaves nothing to celebrate, so the loop just fades.
	const bool bOpponentDown = OpponentId != 0 && (!OpponentState || OpponentState->IsDowned());
	if (Data && bOpponentDown)
	{
		SetCombatLayer(Data->CombatExit, Role);
	}
	else
	{
		ClearCombatLayer();
	}
	OpponentId = 0;
}

void USWGCombatMusicSubsystem::Tick(float DeltaTime)
{
	const int64 PlayerId = ObjectGraph->GetLocalPlayerObjectId();
	const USWGCombatStateComponent* CombatState = PlayerId != 0 ? ObjectGraph->FindComponent<USWGCombatStateComponent>(PlayerId) : nullptr;
	if (!CombatState || !CombatState->bHasBase3)
	{
		bInCombat = false;
		DownedPosture = 0;
		return;
	}

	if (CombatState->IsDowned())
	{
		// One sting per downing; going from incapacitated to dead plays the death one on top.
		if (DownedPosture != CombatState->Posture)
		{
			DownedPosture = CombatState->Posture;
			bInCombat = false;
			OpponentId = 0;
			if (const FSWGGameMusicData* Data = GetMusicData())
			{
				SetCombatLayer(CombatState->IsDead() ? Data->CombatDeath : Data->CombatIncapacitated, Role);
			}
		}
		return;
	}

	if (DownedPosture != 0)
	{
		DownedPosture = 0;
		ClearCombatLayer();
	}

	const bool bFighting = CombatState->HasState(ESWGState::Combat);
	if (bFighting && !bInCombat)
	{
		bInCombat = true;
		StartCombat(PlayerId);
	}
	else if (!bFighting && bInCombat)
	{
		bInCombat = false;
		EndCombat();
	}
}
