// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BaseScreen.generated.h"
class UBaseViewModel;
/**
 * BaseScreen — 所有 UI 页面的基类
 *
 * 设计意图：
 *   纯 View 层。不包含任何游戏逻辑，不计算任何值。
 *   只做三件事：
 *     1. 持有 ViewModel，管理它的生命周期
 *     2. 把 ViewModel 的 Output 属性绑定到 UI 控件上
 *     3. 把用户操作（点击、拖拽）转发给 ViewModel 的 Input 方法
 *
 * 生命周期（由 UIManager 驱动）：
 *   OnOpen   → 创建 ViewModel → 激活 → 绑定 UI → 播打开动画
 *   OnCovered → 暂停 ViewModel，自身半透明
 *   OnRevealed → 恢复 ViewModel，自身可见，刷新数据
 *   OnClose  → 销毁 ViewModel → 播关闭动画
 *             （页面移除由 UIManager 的 PendingRemovals 统一负责）
 *
 * 使用模式（子类）：
 *   1. 在蓝图里设置 ViewModelClass
 *   2. 重写 OnOpen，从中取 ViewModel 绑定 UI
 *   3. 按钮 OnClick → ViewModel->RequestXxx()
 */
UCLASS()
class MH_API UBaseScreen : public UUserWidget
{
	GENERATED_BODY()
    friend class UUIManager;

public:
    // ============================================
    // 生命周期（由 UIManager 调用）
    // ============================================

    /**
     * 页面打开时调用。
     * 负责创建 ViewModel、绑定 UI、播放打开动画。
     * @param Param 打开参数（如要显示的物品ID），可为空
     */
    virtual void OnOpen(UObject* Param = nullptr);

    /**
     * 被上层页面覆盖时调用。
     * 暂停 ViewModel，自身不可交互。
     */
    virtual void OnCovered();

    /**
     * 上层关闭后重新可见。
     * 恢复 ViewModel，刷新数据。
     */
    virtual void OnRevealed();

    /**
     * 页面关闭时调用。
     * 销毁 ViewModel，播放关闭动画。
     * 动画结束后由 UIManager 统一从视口移除。
     */
    virtual void OnClose();

    /**
     * 按返回键（ESC / 手柄 B）。
     * 默认路由到 UIManager::HandleBack，
     * 按 Overlay > Popup > Screen 优先级关闭最顶层页面。
     */
    virtual void OnBack();

    // ============================================
    // 子类可覆盖的钩子
    // ============================================

    /** 获取数据源 — 默认从 PlayerController 拿 PlayerState */
    virtual UObject* GetDataSource() const;

    // 打开/关闭动画（BlueprintImplementableEvent 让蓝图做动画）
    UFUNCTION(BlueprintNativeEvent, Category = "Screen")
    void PlayOpenAnimation();
    UFUNCTION(BlueprintNativeEvent, Category = "Screen")
    void PlayCloseAnimation();

    // ============================================
    // 按键处理
    // ============================================

    /**
     * ESC / 手柄B → OnBack()。
     * UIManager 打开页面时会把键盘焦点给到最顶层页面，按键会先到这里。
     */
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

    // ============================================
    // 公开访问
    // ============================================

    UFUNCTION(BlueprintCallable, Category = "Screen")
    UBaseViewModel* GetViewModel() const { return ViewModel; }

    UFUNCTION(BlueprintCallable, Category = "Screen")
    bool IsScreenOpen() const { return bIsOpen; }

    UFUNCTION(BlueprintCallable, Category = "Screen")
    bool IsTopmost() const { return bIsTopmost; }

protected:
    // ── 编辑器配置 ──

    /** 当前页面对应的 ViewModel 类，在蓝图里设置 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
    TSubclassOf<UBaseViewModel> ViewModelClass;

    /** 关闭动画时长，UIManager 用它延迟移除页面 */
    UPROPERTY(EditDefaultsOnly, Category = "Screen")
    float CloseAnimDuration = 0.3f;

    // ── 运行时状态 ──

    UPROPERTY(BlueprintReadOnly, Category = "Screen")
    UBaseViewModel* ViewModel = nullptr;

    /** UIManager 的弱引用，由 UIManager 在 PushScreen 时设置 */
    UUIManager* OwnerUIManager = nullptr;

    bool bIsOpen = false;
    bool bIsTopmost = true;
};


// ================================================
// 辅助宏 — 创建绑定了 ViewModel 的快捷绑定
// ================================================

/**
 * 用法（在 Screen::OnOpen 里）：
 *   BIND_VM_PROPERTY(HealthPercent, &UHUDWidget::SetHealthPercent);
 *
 * 展开为：
 *   ViewModel->HealthPercent.OnChanged.AddUObject(this, &UHUDWidget::SetHealthPercent);
 */
#define BIND_VM_PROPERTY(VM, PropertyName, SetterFunc) \
    if (VM!=nullptr) \
    { \
        VM->PropertyName.OnChanged.AddUObject(this, SetterFunc); \
        /* 绑定即同步：立即广播当前值，防止首次广播早于订阅 */ \
        VM->PropertyName.Broadcast(); \
    }
