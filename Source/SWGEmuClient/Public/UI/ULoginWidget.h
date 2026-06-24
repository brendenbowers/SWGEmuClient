#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "ULoginWidget.generated.h"

/**
 * Login screen widget.
 *
 * Presents server address, username, and password fields with async connection.
 * Binds to BindWidget properties: ServerAddressBox, UsernameBox, PasswordBox,
 * ConnectButton, StatusText.
 */
UCLASS()
class SWGEMUCLIENT_API ULoginWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable)
	void OnConnectClicked();

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* ServerAddressBox;
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* UsernameBox;
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* PasswordBox;
	UPROPERTY(meta = (BindWidget))
	UButton* ConnectButton;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;
};
