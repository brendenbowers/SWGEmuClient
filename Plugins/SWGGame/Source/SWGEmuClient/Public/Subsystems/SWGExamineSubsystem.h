#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGExamineSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGCommandSubsystem;
class USWGObjectGraphSubsystem;
class USWGTreSubsystem;
struct FSWGNetMessage;

/** One examine line, already put through the string tables. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGExamineAttribute
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Examine")
	FString Label;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Examine")
	FString Value;

	/** Group the line belongs to ("Damage", "Attack Cost"), from a "category.name" key. Empty for ungrouped lines. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Examine")
	FString Category;
};

/** What an examine window shows for one object. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGExamineInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Examine")
	int64 ObjectId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Examine")
	FString Name;

	/** The template's detailedDescription, resolved. Empty when the template has none. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Examine")
	FString Description;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Examine")
	TArray<FSWGExamineAttribute> Attributes;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnExamineInfo, const FSWGExamineInfo&, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnExamineRequested, int64, ObjectId);

/**
 * Examine: the name and description come from what the client already has
 * (baseline name, template description); the attribute lines are the
 * server's, fetched with getattributesbatch and delivered as an
 * AttributeListMessage. Describe() gives the client half at once and fires
 * OnExamineInfo again when the server's half lands.
 */
UCLASS()
class SWGEMUCLIENT_API USWGExamineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** What is known now, and a request for the rest. False if the object is unknown. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Examine")
	bool Describe(int64 ObjectId, FSWGExamineInfo& OutInfo);

	/** Asks whoever shows examine windows to open one. Fired by the radial menu's Examine option. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Examine")
	void RequestExamine(int64 ObjectId);

	/** Fired with the full info once the server's attributes arrive. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Examine")
	FSWGOnExamineInfo OnExamineInfo;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Examine")
	FSWGOnExamineRequested OnExamineRequested;

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);

	/** The client-side half: name and description. */
	bool DescribeLocal(int64 ObjectId, FSWGExamineInfo& OutInfo) const;

	/**
	 * Attribute keys are shown through obj_attr_n; a "category.name" key names
	 * its group first, and a key that is already an "@table:key" resolves as
	 * such. Values may carry their own reference.
	 */
	void ResolveAttributeLabel(const FString& Key, FString& OutLabel, FString& OutCategory) const;
	FString ResolveAttributeValue(const FString& Value) const;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGCommandSubsystem> Commands;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	FDelegateHandle MessageHandle;
};
