// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UI/Core/BaseViewModel.h"
#include "UI/Core/Observable.h"
#include "VMHuntingBaseHUD.generated.h"

class UHealthComponent;
class UCombatComponent;
class UHitReactionComponent;

/** 血量百分比（0~1）变化事件，供纯蓝图控件直接监听。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHHealthPercentChanged, float, NewPercent);
/** 战斗调试文本变化事件。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatDebugTextChanged, const FString&, NewText);

/**
 * 狩猎 HUD 的 ViewModel。
 * 它从本地玩家 Pawn 上的 Health/Combat/HitReaction 组件取数据，
 * 转成 HealthPercent / CombatDebugText 两个可绑定输出属性供 HUD 显示。
 */
UCLASS()
class MH_API UVMHuntingBaseHUD : public UBaseViewModel
{
	GENERATED_BODY()
public:
    /** 输出属性：当前血量百分比，HUD 绑定它来更新进度条。 */
    TBindedValue<float> HealthPercent;

    /** 输出属性：多行战斗调试文本；CVar 关闭时为空字符串。 */
    TBindedValue<FString> CombatDebugText;

    /** 蓝图可绑定的血量变化事件（与 TBindedValue 同步发出）。 */
    UPROPERTY(BlueprintAssignable, Category = "UI|Health")
    FMHHealthPercentChanged OnHealthPercentChanged;

    /** 蓝图可绑定的调试文本变化事件。 */
    UPROPERTY(BlueprintAssignable, Category = "UI|Combat")
    FMHCombatDebugTextChanged OnCombatDebugTextChanged;

    /** 蓝图读取用：当前血量百分比。 */
    UFUNCTION(BlueprintPure, Category = "UI|Health")
    float GetHealthPercentValue() const { return HealthPercent.Get(); }

    /** 蓝图读取用：当前调试文本。 */
    UFUNCTION(BlueprintPure, Category = "UI|Combat")
    FString GetCombatDebugTextValue() const { return CombatDebugText.Get(); }

    /** 重新拼装调试文本；mh.Combat.DebugHUD 为 0 时清空。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Combat")
    void RefreshCombatDebug();

    /** HUD 打开时解析并绑定本地玩家组件，同时刷一次数据。 */
    virtual void OnActivated() override;
    /** HUD 被覆盖/关闭时解绑，避免后台继续收委托。 */
    virtual void OnDeactivated() override;
    /** 数据上下文（Pawn/Controller）变化后重新解析组件并全量刷新。 */
    virtual void RefreshAll() override;

private:
    /** 解析本地玩家 Pawn 上的 UHealthComponent（DataSource 兜底到本地 Controller）。 */
    UHealthComponent* ResolveHealthComponent() const;
    /** 绑定血量组件并幂等挂上 OnHealthChanged。 */
    void BindHealthComponent(UHealthComponent* HealthComponent);
    /** 解绑血量委托并清弱引用。 */
    void UnbindHealthComponent();
    /** 写 HealthPercent 并广播蓝图事件（入参已 Clamp）。 */
    void SetHealthPercent(float NewPercent);

    /** 解析本地玩家 Pawn 上的 UCombatComponent。 */
    UCombatComponent* ResolveCombatComponent() const;
    /** 解析本地玩家 Pawn 上的 UHitReactionComponent（仅调试显示用，不持有绑定）。 */
    UHitReactionComponent* ResolveHitReactionComponent() const;
    /** 绑定战斗组件并幂等挂上 OnHitConfirmed。 */
    void BindCombatComponent(UCombatComponent* CombatComponent);
    /** 解绑战斗委托并清弱引用。 */
    void UnbindCombatComponent();
    /** 写 CombatDebugText 并广播蓝图事件。 */
    void SetCombatDebugText(const FString& NewText);
    /** 拼装多行调试文本：预测/动作/命中窗口/血量/受击/最近命中。 */
    FString BuildCombatDebugText() const;

    /** 收到服务器确认命中：缓存事件与时间，供 HUD 显示。 */
    UFUNCTION()
    void HandleHitConfirmed(const FMHCombatHitEvent& HitEvent);

    /** 血量变化：换算成百分比后刷新 HUD。 */
    UFUNCTION()
    void HandleHealthChanged(float NewHealth, float MaxHealth);

    /** 当前绑定的血量/战斗组件（弱引用，避免影响 Pawn 生命周期）。 */
    TWeakObjectPtr<UHealthComponent> BoundHealthComponent;
    TWeakObjectPtr<UCombatComponent> BoundCombatComponent;
    /** 最近一次确认命中的事件与本地接收时刻，用于显示命中反馈。 */
    FMHCombatHitEvent LastConfirmedHit;
    float LastConfirmedHitWorldTime = -1000.f;
};
