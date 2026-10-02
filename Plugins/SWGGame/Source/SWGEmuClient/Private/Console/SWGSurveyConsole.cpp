#include "Subsystems/SWGSurveySubsystem.h"
#include "Common/SWGWorldScale.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

static FAutoConsoleCommandWithWorldAndArgs GSurveyFakeCommand(
	TEXT("swg.Survey.Fake"),
	TEXT("Fakes a survey tool use (no args) or a survey result around the player (\"result [range]\") for UI work."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		// From the editor's console the world is the editor's; use the running game's instead.
		if (GEngine && (!World || !World->GetGameInstance()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					World = Context.World();
					break;
				}
			}
		}
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGSurveySubsystem* Survey = GameInstance ? GameInstance->GetSubsystem<USWGSurveySubsystem>() : nullptr;
		if (!Survey)
		{
			return;
		}
		if (Args.IsEmpty() || Args[0] != TEXT("result"))
		{
			TArray<FSWGSurveyResource> Resources;
			const TCHAR* const Fake[][2] = {
				{ TEXT("Aqabuo"), TEXT("copper_desh") }, { TEXT("Oroxin"), TEXT("iron_polonium") },
				{ TEXT("Suvemi"), TEXT("aluminum_titanium") }, { TEXT("Ekkerite"), TEXT("steel_duranium") } };
			for (const auto& Entry : Fake)
			{
				FSWGSurveyResource& Resource = Resources.AddDefaulted_GetRef();
				Resource.Name = Entry[0];
				Resource.Type = Entry[1];
			}
			Survey->InjectResources(Resources, TEXT("mineral"));
			return;
		}
		const APlayerController* Controller = GameInstance->GetFirstLocalPlayerController(World);
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		const FVector Raw = Pawn ? SWGToRawSpace(Pawn->GetActorLocation()) : FVector::ZeroVector;
		const float Range = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 256.f;
		const int32 Points = Range >= 256.f ? 5 : Range >= 128.f ? 4 : 3;
		// Same walk as Core3 ResourceSpawner::sendSurvey, with a made-up deposit to the north-east.
		const float Spacing = Range / (Points - 1);
		const FVector2D Deposit(Raw.X + Range * 0.4f, Raw.Y + Range * 0.25f);
		TArray<FVector2D> Positions;
		TArray<float> Densities;
		for (int32 Row = 0; Row < Points; ++Row)
		{
			for (int32 Column = 0; Column < Points; ++Column)
			{
				const FVector2D Position(Raw.X - (Points - 1) * 0.5f * Spacing + Column * Spacing,
					Raw.Y + (Points - 1) * 0.5f * Spacing - Row * Spacing);
				Positions.Add(Position);
				Densities.Add(FMath::Clamp(0.85f * FMath::Exp(-FVector2D::DistSquared(Position, Deposit) / (Range * Range * 0.35f)), 0.f, 1.f));
			}
		}
		Survey->InjectResult(USWGSurveySubsystem::BuildResult(Survey->GetSelectedResource(), Positions, Densities));
	}));
