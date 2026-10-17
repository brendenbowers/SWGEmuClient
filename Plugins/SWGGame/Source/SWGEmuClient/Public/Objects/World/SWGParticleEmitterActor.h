#pragma once

#include "CoreMinimal.h"
#include "Objects/World/SWGStaticProp.h"
#include "SWGParticleEmitterActor.generated.h"

class USWGTreSubsystem;

/**
 * A particle object (object/static/particle/*.iff): no mesh, its appearanceFilename is a .prt.
 * Spawned from the world snapshot, interior layouts and the server like any other static object; the effect
 * itself is played by USWGWorldParticlesSubsystem only while the camera is near.
 */
UCLASS()
class SWGEMUCLIENT_API ASWGParticleEmitterActor : public ASWGStaticProp
{
	GENERATED_BODY()

public:
	ASWGParticleEmitterActor();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** True for the particle object templates, which never get a mesh. */
	static bool IsParticleObjectTemplate(const FString& TemplatePath);

	/** The template's appearanceFilename (through its DERV chain) when it is a .prt. Cached per template; safe off the game thread. */
	static bool ResolveParticlePath(USWGTreSubsystem& Tre, const FString& TemplatePath, FString& OutPrtPath);

	/** Resolves the .prt and registers with the world particle subsystem. Game thread, after the actor is placed. */
	void InitFromTemplate(const FString& InTemplatePath);

	const FString& GetParticlePath() const { return ParticlePath; }

private:
	FString ParticlePath;
	int32 EmitterId = 0;
};
