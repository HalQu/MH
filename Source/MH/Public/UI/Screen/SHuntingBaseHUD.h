// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "UI/Core/BaseScreen.h"
#include "UI/Core/BaseViewModel.h"
#include "UI/VM/VMHuntingBaseHUD.h"
#include "SHuntingBaseHUD.generated.h"

/**
 * 狩猎常驻 HUD。HealthBar 是现有必需控件；CombatDebugTextBlock 为可选控件，
 * 旧 WBP 没有该控件时会在根面板下自动创建一个左下角调试文本。
 */
UCLASS()
class MH_API USHuntingBaseHUD : public UBaseScreen
{
	GENERATED_BODY()

public:
	USHuntingBaseHUD();

	/** 构造时确保调试文本框存在：WBP 里没放 CombatDebugTextBlock 时自动创建一个。 */
	virtual void NativeConstruct() override;
	/** 每帧刷新战斗调试文本；CVar 关闭时会写入空字符串自动隐藏。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** 页面打开时把 ViewModel 的输出属性绑定到控件 Setter。 */
	virtual void OnOpen(UObject* Param) override;

	/** 必需控件：显示本地玩家血量百分比（0~1）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar = nullptr;

	/** 可选控件：战斗调试信息；缺失时由 EnsureCombatDebugTextWidget 补建。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CombatDebugTextBlock = nullptr;

	/** ViewModel 血量属性回调；入参会被 Clamp 到 0~1。 */
	void SetHealthBar(float Percent);
	/** ViewModel 调试文本回调；空文本时把控件折叠。 */
	void SetCombatDebugText(FString Text);

private:
	/** 旧 WBP 没有调试文本框时，在根面板左下角自动补一个等宽字体文本。 */
	void EnsureCombatDebugTextWidget();
};
