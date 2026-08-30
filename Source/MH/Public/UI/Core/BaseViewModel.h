// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BaseViewModel.generated.h"
class UUIEventBus;
/**
 * BaseViewModel — 所有 ViewModel 的基类
 *
 * 设计意图：
 *   Model↔View 之间的适配层。不画任何 UI，不拥有数据所有权。
 *   只做四件事：
 *     1. 从 Model 读取原始数据，格式化为 UI 需要的样子 → Output 属性
 *     2. 接收 Screen 转发的用户操作 → 校验 → 调用 Model
 *     3. 监听 Model 的变化，自动更新 Output 属性
 *     4. 卸载时解绑所有回调，防止野指针
 *
 * 生命周期：
 *   创建:   Screen::OnOpen → NewObject<VM>(this)
 *   激活:   VM->OnActivated()  (注册 Model 回调)
 *   暂停:   VM->OnDeactivated() (解绑 Model 回调，暂停更新)
 *   销毁:   Screen::OnClose → VM->OnDestroy() → GC 自动释放
 *
 * 使用模式（子类）：
 *   class UMyVM : public UBaseViewModel {
 *       // Output — Screen 绑定这些
 *       TBindedValue<FString> DisplayText;
 *       TBindedValue<int32>  CurrentValue;
 *
 *       // Input — Screen 调这些
 *       void RequestDoSomething(int32 Arg);
 *
 *       // Model 回调
 *       void OnModelDataChanged();
 *   };
 */
UCLASS()
class MH_API UBaseViewModel : public UObject
{
	GENERATED_BODY()
public:
    // ============================================
    // 生命周期（由 Screen 调用）
    // ============================================

    /**
     * 初始化。必须在所有操作之前调用一次。
     * @param InOuter  持有者（通常是 Screen），用于生命周期管理
     * @param InData   数据源，通常是 PlayerState 或其他子系统
     */
    virtual void Initialize(UObject* InOuter, UObject* InData);

    /**
     * Screen 打开或被 Reveal 时调用。
     * 子类重写以注册 Model 回调、做初始化同步。
     */
    virtual void OnActivated();

    /**
     * Screen 被覆盖或关闭时调用。
     * 子类重写以解绑 Model 回调、释放临时资源。
     * 注意：不销毁 ViewModel，只暂停监听。
     */
    virtual void OnDeactivated();

    /**
     * Screen 销毁前调用最后一次。
     * 确保所有绑定都解除，不依赖 GC 的顺序。
     */
    virtual void OnDestroy();

    // ============================================
    // 工具方法
    // ============================================

    /** 获取数据源，带类型安全转换 */
    template<typename T>
    T* GetDataSource() const
    {
        return Cast<T>(DataSource.Get());
    }

    /** 获取 UIEventBus（从 GameInstance 拿） */
    UUIEventBus* GetBus() const;

    /** 当前是否处于激活状态（能收到 Model 回调） */
    bool IsActive() const { return bIsActive; }

    /**
     * 强制刷新所有 Output 属性。
     * 子类重写以同步 Model → Output。
     * 用于：首次打开界面、从被覆盖恢复、语言切换等场景。
     */
    virtual void RefreshAll() {}

protected:
    // 数据源 — 弱引用，不延长 Model 生命周期
    TWeakObjectPtr<UObject> DataSource;

    // 持有者（Screen）— 弱引用防循环
    TWeakObjectPtr<UObject> OuterWidget;

    // 当前是否激活
    bool bIsActive = false;
};
