#pragma once

#include "CoreMinimal.h"
#include "GamePlay/MHGameInstance.h"
#include "UI/Core/BaseScreen.h"
#include "MainMenuScreen.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UWidget;
class UMHMainMenuScreen;

UCLASS()
class UMHSessionButtonProxy : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleClicked();

	UPROPERTY()
	TObjectPtr<UObject> Owner;

	int32 SessionIndex = INDEX_NONE;
};

/**
 * 局域网主菜单。直接驱动 GameInstance 的 Session API，不依赖蓝图事件图。
 */
UCLASS()
class MH_API UMHMainMenuScreen : public UBaseScreen
{
	GENERATED_BODY()

public:
	UMHMainMenuScreen();

	virtual void NativeConstruct() override;
	virtual void OnOpen(UObject* Param) override;
	virtual void OnClose() override;

private:
	friend class UMHSessionButtonProxy;

	void BuildLayout();
	void RefreshSessionList(const TArray<FSessionData>& SessionResults);
	void SetStatus(const FString& Text);
	void SetBusy(bool bNewBusy);

	UFUNCTION()
	void HandleHostClicked();

	UFUNCTION()
	void HandleRefreshClicked();

	UFUNCTION()
	void HandleQuitClicked();

	UFUNCTION()
	void HandleSessionsFound(const TArray<FSessionData>& SessionResults);

	UFUNCTION()
	void HandleHostSessionComplete();

	UFUNCTION()
	void HandleSessionJoined();

	UFUNCTION()
	void HandleSessionOperationFailed(const FString& ErrorMessage);

	void JoinSessionByIndex(int32 SessionIndex);
	void BindSessionDelegates();
	void UnbindSessionDelegates();

	UPROPERTY()
	TObjectPtr<UEditableTextBox> ServerNameInput;

	UPROPERTY()
	TObjectPtr<UVerticalBox> SessionList;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UButton> HostButton;

	UPROPERTY()
	TObjectPtr<UButton> RefreshButton;

	UPROPERTY()
	TObjectPtr<UButton> QuitButton;

	UPROPERTY()
	TArray<TObjectPtr<UWidget>> SessionRowWidgets;

	UPROPERTY()
	TArray<TObjectPtr<UMHSessionButtonProxy>> SessionButtonProxies;

	bool bBusy = false;
};
