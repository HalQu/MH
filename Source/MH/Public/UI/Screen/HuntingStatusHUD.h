#pragma once

#include "CoreMinimal.h"
#include "UI/Core/BaseScreen.h"
#include "HuntingStatusHUD.generated.h"

class UTextBlock;

/**
 * 狩猎阶段的非交互常驻状态栏，显示服务器复制的剩余时间和结算文本。
 */
UCLASS()
class MH_API UMHHuntingStatusHUD : public UBaseScreen
{
	GENERATED_BODY()

public:
	UMHHuntingStatusHUD();

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildLayout();
	void RefreshStatus();

	UPROPERTY()
	TObjectPtr<UTextBlock> TimerText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ResultText;
};
