#include "Objects/SWGObject.h"
#include "Components/SceneComponent.h"

ASWGObject::ASWGObject()
{
	// Without a root component, SpawnActor's transform parameter (and any
	// later SetActorLocation/SetActorTransform call) has nothing to store
	// itself in and silently no-ops — every ASWGObject-derived actor
	// (items, static props, buildings) sat at (0,0,0) until its mesh
	// component got built and became root, at which point it defaulted to
	// identity relative to that already-lost transform. ASWGCreature never
	// hit this because it's an ACharacter, which gets a CapsuleComponent
	// root for free.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultRoot"));
}

