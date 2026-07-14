#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TRE/SWGTreArchive.h"
#include "TRE/SWGIffReader.h"
#include "SWGTreSubsystem.generated.h"

/**
 * Owns the read-only virtual filesystem over the game's .tre archives.
 *
 * Loads every *.tre in a directory (sorted by filename, matching SWG's own
 * patch-priority convention — higher-numbered patch archives are loaded
 * later and override earlier ones for any virtual path they both contain,
 * mirroring ParseCrcTable.ps1's "-Last 1" selection). Also eagerly parses
 * misc/object_template_crc_string_table.iff into an in-memory CRC->template
 * path map, since that's needed on every SceneCreateObjectByCrc once the
 * spawn-by-CRC dispatch (world-object-plan.html "CRC → actor-class spawn
 * table") is wired in.
 */
UCLASS(Config = Game)
class SWGTRE_API USWGTreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Indexes every *.tre file found directly under Directory (non-recursive,
	 * matching the retail layout). Safe to call again to reload from a new path.
	 * Returns false if the directory doesn't exist or contains no .tre files.
	 */
	bool LoadArchives(const FString& Directory);
	bool LoadArchives() { return LoadArchives(TreDirectory); }

	bool IsLoaded() const { return Archives.Num() > 0; }
	int32 GetLoadedArchiveCount() const { return Archives.Num(); }

	bool FileExists(const FString& VirtualPath) const;

	/** Debug helper: every indexed virtual path containing Substring (case-insensitive). */
	TArray<FString> FindVirtualPaths(const FString& Substring) const;

	/** Reads and decompresses one file's bytes by its virtual path (e.g. "object/mobile/shared_bantha.iff"). */
	TArray<uint8> ExtractFile(const FString& VirtualPath) const;

	/** Convenience: extracts a file and wraps it in an FSWGIffReader in one call. Reader.IsValid() is false if the path wasn't found. */
	FSWGIffReader CreateIffReader(const FString& VirtualPath) const;

	/** Looks up a template's virtual path from its CRC (from SceneCreateObjectByCrc), or empty if unknown. */
	FString ResolveTemplatePath(uint32 Crc) const;

	int32 GetTemplateCount() const { return CrcToTemplatePath.Num(); }

	TMap<uint32, FString>& GetCrcToTemplatePathMap() { return CrcToTemplatePath; }

private:
	void BuildVirtualFileTable();
	void BuildCrcTable();

	/**
	 * Directory to auto-load from on Initialize(), if set. Empty = don't
	 * auto-load; call LoadArchives() explicitly instead. Set via
	 * DefaultGame.ini:
	 *   [/Script/SWGEmu.SWGTreSubsystem]
	 *   TreDirectory=D:\StarWarsGalaxies
	 */
	UPROPERTY(Config)
	FString TreDirectory;

	/**
	 * If true, auto-load TreDirectory on Initialize(). Set via
	 * DefaultGame.ini:
	 *   [/Script/SWGEmu.SWGTreSubsystem]
	 *   AutoLoad=true
	 */
	UPROPERTY(Config)
	bool AutoLoad = false;

	TArray<TUniquePtr<FSWGTreArchive>> Archives;

	/** Virtual path -> index into Archives; later archives in load order win on conflict. */
	TMap<FString, int32> VirtualPathToArchiveIndex;

	TMap<uint32, FString> CrcToTemplatePath;
};
