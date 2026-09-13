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

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void OnOpen(UObject* Param) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CombatDebugTextBlock = nullptr;

	void SetHealthBar(float Percent);
	void SetCombatDebugText(FString Text);

private:
	void EnsureCombatDebugTextWidget();
};
