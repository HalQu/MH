#pragma once

#include "CoreMinimal.h"
#include "UI/Core/BaseScreen.h"
#include "HuntMenuScreen.generated.h"

class UButton;
class UTextBlock;

/**
 * 狩猎中的流程菜单：显示复制倒计时与结算，并提供离开/提前结束入口。
 */
UCLASS()
class MH_API UMHHuntMenuScreen : public UBaseScreen
{
	GENERATED_BODY()

public:
	UMHHuntMenuScreen();

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void OnOpen(UObject* Param) override;
	virtual void OnBack_Implementation() override;

private:
	void BuildLayout();
	void RefreshStatus();

	UFUNCTION()
	void HandleEndHuntClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleLeaveClicked();

	UPROPERTY()
	TObjectPtr<UTextBlock> TimerText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ResultText;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UButton> EndHuntButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> EndHuntButtonLabel;

	UPROPERTY()
	TObjectPtr<UButton> CloseButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> CloseButtonLabel;

	UPROPERTY()
	TObjectPtr<UButton> LeaveButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> LeaveButtonLabel;

	bool bEndHuntPending = false;

	/** 结束狩猎请求的保护计时：服务器未响应时自动恢复按钮。 */
	float EndHuntPendingRemaining = 0.f;
};
