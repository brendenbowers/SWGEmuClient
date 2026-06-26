#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGNetworkSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

static const FString LoginServerHost = TEXT("10.0.0.190");
static const int32   LoginServerPort = 44453;
static const float   TestTimeoutSeconds = 10.f;

// ── Helpers ───────────────────────────────────────────────────────────────────

static USWGNetworkSubsystem* GetNetworkSubsystem(FAutomationTestBase* Test)
{
	const TIndirectArray<FWorldContext>& Contexts = GEngine->GetWorldContexts();
	for (const FWorldContext& Ctx : Contexts)
	{
		if (Ctx.World() && Ctx.World()->GetGameInstance())
		{
			return Ctx.World()->GetGameInstance()->GetSubsystem<USWGNetworkSubsystem>();
		}
	}
	Test->AddError(TEXT("No valid world context with a game instance found"));
	return nullptr;
}

// ── Test: TCP reachability via ConnectAsync ───────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGLoginServerConnectTest,
	"SWGEmu.Integration.LoginServer.Connect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGLoginServerConnectTest::RunTest(const FString& Parameters)
{
	USWGNetworkSubsystem* Network = GetNetworkSubsystem(this);
	if (!Network)
		return false;

	bool bComplete = false;
	bool bPassed   = false;

	Network->ConnectAsync(LoginServerHost, LoginServerPort)
		.Next([&](const TResult<void>& Result)
		{
			if (Result.IsSuccess())
			{
				AddInfo(FString::Printf(TEXT("Connected to %s:%d"), *LoginServerHost, LoginServerPort));
				bPassed = true;
			}
			else
			{
				AddError(FString::Printf(TEXT("Connection failed: %s"), *Result.GetError()));
			}

			Network->Disconnect();
			bComplete = true;
		});

	const double Deadline = FPlatformTime::Seconds() + TestTimeoutSeconds;
	while (!bComplete && FPlatformTime::Seconds() < Deadline)
		FPlatformProcess::Sleep(0.05f);

	if (!bComplete)
		AddError(TEXT("Test timed out waiting for ConnectAsync"));

	return bPassed;
}

// ── Test: Connect then disconnect cleanly ─────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGLoginServerDisconnectTest,
	"SWGEmu.Integration.LoginServer.Disconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGLoginServerDisconnectTest::RunTest(const FString& Parameters)
{
	USWGNetworkSubsystem* Network = GetNetworkSubsystem(this);
	if (!Network)
		return false;

	bool bComplete = false;
	bool bPassed   = false;

	Network->ConnectAsync(LoginServerHost, LoginServerPort)
		.Next([&](const TResult<void>& Result)
		{
			if (!Result.IsSuccess())
			{
				AddError(FString::Printf(TEXT("Connection failed: %s"), *Result.GetError()));
				bComplete = true;
				return;
			}

			AddInfo(TEXT("Connected — disconnecting"));
			Network->Disconnect();

			TestFalse(TEXT("IsConnected should be false after Disconnect"), Network->IsConnected());
			bPassed   = true;
			bComplete = true;
		});

	const double Deadline = FPlatformTime::Seconds() + TestTimeoutSeconds;
	while (!bComplete && FPlatformTime::Seconds() < Deadline)
		FPlatformProcess::Sleep(0.05f);

	if (!bComplete)
		AddError(TEXT("Test timed out"));

	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS
