#pragma once

#include "CoreMinimal.h"
#include "UI/Core/BaseScreen.h"
#include "LobbyScreen.generated.h"

class AMHPlayerState;
class UButton;
class UTextBlock;
class UVerticalBox;
class UWidget;

/**
 * 集会所界面：显示玩家准备状态，房主在所有玩家准备后开始狩猎。
 */
UCLASS()
class MH_API ULobbyScreen : public UBaseScreen
{
	GENERATED_BODY()

public:
	ULobbyScreen();

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void OnOpen(UObject* Param) override;

private:
	void BuildLayout();
	void RefreshLobby();
	void SetStatus(const FString& Text);

	UFUNCTION()
	void HandleReadyClicked();

	UFUNCTION()
	void HandleStartClicked();

	UFUNCTION()
	void HandleLeaveClicked();

	AMHPlayerState* GetLocalPlayerState() const;

	UPROPERTY()
	TObjectPtr<UVerticalBox> PlayerList;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ReadyButtonLabel;

	UPROPERTY()
	TObjectPtr<UTextBlock> StartButtonLabel;

	UPROPERTY()
	TObjectPtr<UButton> ReadyButton;

	UPROPERTY()
	TObjectPtr<UButton> StartButton;

	UPROPERTY()
	TObjectPtr<UButton> LeaveButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> LeaveButtonLabel;

	UPROPERTY()
	TArray<TObjectPtr<UWidget>> PlayerRows;

	float RefreshAccumulator = 0.f;
	bool bActionPending = false;
	float ActionLockRemaining = 0.f;
	/** 开始狩猎请求的保护计时：服务器未响应时自动恢复按钮。 */
	float StartPendingRemaining = 0.f;
	bool bStartPending = false;
};
