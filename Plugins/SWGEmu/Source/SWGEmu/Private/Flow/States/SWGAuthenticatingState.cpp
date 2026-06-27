#include "Flow/States/SWGAuthenticatingState.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGMessageWaitSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Network/Messages/Login/LoginIDMessage.h"
#include "Network/Messages/Login/LoginClientTokenMessage.h"
#include "Network/Messages/Login/LoginEnumClusterMessage.h"
#include "Network/Messages/Login/LoginClusterStatusMessage.h"
#include "Network/Messages/Login/EnumerateCharacterIdMessage.h"

void FSWGAuthenticatingState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx)
{
	if (!UIStateMachine.Network || !UIStateMachine.WaitSubsystem)
	{
		return;
	}

	int32 Epoch = UIStateMachine.Epoch;
	TWeakObjectPtr<USWGClientFlowSubsystem> StateMchineWeakRef = &UIStateMachine;
	FLoginIDMessage Msg;
	Msg.Username = Ctx.Username;
	Msg.Password = Ctx.Password;

	// TODO: Handle the error opcode without wiating for the timeout 
	TSet<uint32> Opcodes = {
		static_cast<uint32>(ESWGMessageOp::LoginClientToken),
		static_cast<uint32>(ESWGMessageOp::LoginClusterStatus),
		static_cast<uint32>(ESWGMessageOp::LoginEnumCluster),
		static_cast<uint32>(ESWGMessageOp::EnumerateCharacterId)
	};
	UIStateMachine.WaitSubsystem->WaitForAll(Opcodes).Next([Epoch, StateMchineWeakRef, &Ctx](TResult<TMap<uint32, TSharedPtr<FSWGNetMessage>>> Result)
		{
			TStrongObjectPtr<USWGClientFlowSubsystem> StateMachine = StateMchineWeakRef.Pin();
			if (!StateMachine.IsValid() || StateMachine->Epoch != Epoch)
			{
				return;
			}

			if (!Result.IsSuccess())
			{
				StateMachine->Fail(Result.GetError());
				return;
			}

			// EnumCluster provides names; ClusterStatus provides IP/port/population.
			// Both are keyed by ServerID, so accumulate into a map and merge.
			TMap<uint32, FSWGGalaxyInfo> GalaxyMap;

			for (auto& Message : Result.GetValue())
			{
				ESWGMessageOp OpCode = static_cast<ESWGMessageOp>(Message.Key);
				switch (OpCode)
				{
				case ESWGMessageOp::LoginClientToken:
					{
						TSharedPtr<FLoginClientTokenMessage> LoginTokenMessage = StaticCastSharedPtr<FLoginClientTokenMessage>(Message.Value);
						Ctx.SessionToken = LoginTokenMessage->SessionKey;
						Ctx.UserID = LoginTokenMessage->StationID;
					}
					break;
				case ESWGMessageOp::LoginEnumCluster:
					{
						TSharedPtr<FLoginEnumClusterMessage> EnumClusterMessage = StaticCastSharedPtr<FLoginEnumClusterMessage>(Message.Value);
						for (const FServerName& Server : EnumClusterMessage->Servers)
						{
							FSWGGalaxyInfo& Galaxy = GalaxyMap.FindOrAdd(Server.ServerID);
							Galaxy.GalaxyID = (int32)Server.ServerID;
							Galaxy.Name     = Server.ServerDisplayName;
						}
					}
					break;
				case ESWGMessageOp::LoginClusterStatus:
					{
						TSharedPtr<FLoginClusterStatusMessage> ClusterStatusMessage = StaticCastSharedPtr<FLoginClusterStatusMessage>(Message.Value);
						for (const FServerDetails& Server : ClusterStatusMessage->Servers)
						{
							FSWGGalaxyInfo& Galaxy = GalaxyMap.FindOrAdd(Server.ServerID);
							Galaxy.GalaxyID   = (int32)Server.ServerID;
							Galaxy.IP         = Server.ServerIP;
							Galaxy.Port       = Server.ServerPort;
							Galaxy.Population = (int32)Server.Population;
							Galaxy.bOnline    = (Server.Status != 0);
						}
					}
					break;
				case ESWGMessageOp::EnumerateCharacterId:
					{
						TSharedPtr<FEnumerateCharacterIdMessage> EnumerateCharactersMessage = StaticCastSharedPtr<FEnumerateCharacterIdMessage>(Message.Value);
						Ctx.Characters.Reserve(EnumerateCharactersMessage->Characters.Num());
						for (const FCharacter& Character : EnumerateCharactersMessage->Characters)
						{
							FSWGCharacterInfo& CharacterInfo = Ctx.Characters.AddDefaulted_GetRef();
							CharacterInfo.CharacterID  = Character.CharacterID;
							CharacterInfo.Name         = Character.Name;
							CharacterInfo.GalaxyID     = Character.ServerID;
							CharacterInfo.RaceGenderCRC = Character.RaceGenderCRC;
							CharacterInfo.bActive      = Character.Status != 0;
						}
					}
					break;
				}
			}

			GalaxyMap.GenerateValueArray(Ctx.Galaxies);

			StateMachine->TransitionTo(ESWGClientState::GalaxySelect);
		});

	UIStateMachine.Network->SendMessage(Msg.Serialize());
}
void FSWGAuthenticatingState::Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}
