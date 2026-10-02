#include "SWGPlacementViewMode.h"

namespace
{
	TArray<FSWGPlacementViewRegistration>& Registrations()
	{
		static TArray<FSWGPlacementViewRegistration> Instance;
		return Instance;
	}
}

void FSWGPlacementViewRegistry::Register(const FSWGPlacementViewRegistration& Registration)
{
	Unregister(Registration.Id);
	Registrations().Add(Registration);
}

void FSWGPlacementViewRegistry::Unregister(FName Id)
{
	Registrations().RemoveAll([Id](const FSWGPlacementViewRegistration& Existing) { return Existing.Id == Id; });
}

const TArray<FSWGPlacementViewRegistration>& FSWGPlacementViewRegistry::GetAll()
{
	return Registrations();
}

FSWGPlacementViewRegistry::FOnToggleRequested& FSWGPlacementViewRegistry::OnToggleRequested()
{
	static FOnToggleRequested Instance;
	return Instance;
}

FSWGPlacementViewRegistry::FOnCommandRequested& FSWGPlacementViewRegistry::OnCommandRequested()
{
	static FOnCommandRequested Instance;
	return Instance;
}
