#include "Network/Messages/SWGMessageOp.h"

FString GetMessageOpName(uint32 Opcode)
{
	switch ((ESWGMessageOp)Opcode)
	{
		case ESWGMessageOp::LoginClientToken: return TEXT("LoginClientToken");
		case ESWGMessageOp::LoginEnumCluster: return TEXT("LoginEnumCluster");
		case ESWGMessageOp::LoginClusterStatus: return TEXT("LoginClusterStatus");
		case ESWGMessageOp::EnumerateCharacterId: return TEXT("EnumerateCharacterId");
		case ESWGMessageOp::SelectCharacter: return TEXT("SelectCharacter");
		case ESWGMessageOp::CmdStartScene: return TEXT("CmdStartScene");
		case ESWGMessageOp::CmdSceneReady: return TEXT("CmdSceneReady");
		case ESWGMessageOp::CmdSceneReady2: return TEXT("CmdSceneReady2");
		case ESWGMessageOp::SceneCreateObjectByCrc: return TEXT("SceneCreateObjectByCrc");
		case ESWGMessageOp::SceneEndBaselines: return TEXT("SceneEndBaselines");
		case ESWGMessageOp::SceneDestroyObject: return TEXT("SceneDestroyObject");
		case ESWGMessageOp::BaselinesMessage: return TEXT("BaselinesMessage");
		case ESWGMessageOp::DeltasMessage: return TEXT("DeltasMessage");
		case ESWGMessageOp::UpdateTransformMessage: return TEXT("UpdateTransformMessage");
		case ESWGMessageOp::UpdateTransformMessageWithParent: return TEXT("UpdateTransformMessageWithParent");
		case ESWGMessageOp::UpdateContainmentMessage: return TEXT("UpdateContainmentMessage");
		case ESWGMessageOp::UpdatePostureMessage: return TEXT("UpdatePostureMessage");
		case ESWGMessageOp::UpdatePvpStatusMessage: return TEXT("UpdatePvpStatusMessage");
		case ESWGMessageOp::ObjControllerMessage: return TEXT("ObjControllerMessage");
		case ESWGMessageOp::ClientIdMsg: return TEXT("ClientIdMsg");
		case ESWGMessageOp::ClientPermissionsMessage: return TEXT("ClientPermissionsMessage");
		case ESWGMessageOp::ClientInactivity: return TEXT("ClientInactivity");
		case ESWGMessageOp::ClientLogout: return TEXT("ClientLogout");
		case ESWGMessageOp::ConnectPlayerMessage: return TEXT("ConnectPlayerMessage");
		case ESWGMessageOp::ConnectPlayerResponseMessage: return TEXT("ConnectPlayerResponseMessage");
		case ESWGMessageOp::ErrorMessage: return TEXT("ErrorMessage");
		case ESWGMessageOp::ParametersMessage: return TEXT("ParametersMessage");
		case ESWGMessageOp::AttributeListMessage: return TEXT("AttributeListMessage");
		default:
			return FString::Printf(TEXT("Unknown (0x%08X)"), Opcode);
	}
}
