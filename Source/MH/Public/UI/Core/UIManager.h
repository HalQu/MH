// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UIManager.generated.h"

class UBaseScreen;

/**
 * EUILayer — 界面层级
 *
 * ZOrder 从低到高，高层盖住低层。
 * HUD 是常驻层，不走页面栈。Screen/Popup/Overlay 每层有独立的压栈。
 */
UENUM(BlueprintType)
enum class EUILayer : uint8
{
    World = 0,    // 3D 空间 UI（伤害数字、怪物名字）— 预留，暂无 API
    HUD = 10,     // 常驻 HUD（血量、耐力、道具栏）— 不压栈
    Screen = 20,  // 全屏界面（菜单、装备箱）— 压栈管理
    Popup = 30,   // 弹窗（确认框、对话框）— 压栈管理
    Overlay = 40, // 最高层（加载界面、暂停）— 压栈管理
};

/**
 * UIManager — 全局 UI 调度器
 *
 * 设计意图：
 *   只管页面栈、层级、输入模式的切换。
 *   不关心任何页面的内容，不操作任何 ViewModel 的属性。
 *
 * 核心职责：
 *   1. 页面栈: Push / Pop / 支持关闭任意层级
 *   2. 层级管理: 根据 EUILayer 分配 ZOrder
 *   3. 常驻页面: HUD 不压栈，常驻显示
 *   4. 输入模式: 打开全屏→UI Only（并聚焦新页面），全部关闭→Game Only
 *   5. 返回键: ESC / 手柄B 由页面 OnBack → HandleBack 按 Overlay>Popup>Screen 顺序关闭
 *
 * 生命周期：
 *   WorldSubsystem — 随关卡创建和销毁。
 *   关卡切换时自动重置所有页面栈。
 */
UCLASS()
class MH_API UUIManager : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
    // ============================================
    // WorldSubsystem 生命周期
    // ============================================

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** 每一帧检查，处理待清理的页面 */
    virtual void Tick(float DeltaTime) override;
    /** 游戏暂停时也继续 Tick，保证关闭动画后的页面能被移除 */
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UUIManager, STATGROUP_Tickables);
    }

    // ============================================
    // 常驻页面（HUD）
    // ============================================

    /**
     * 打开常驻页面。不会覆盖其他页面，也不进栈。
     * @param ScreenID    内部标识名
     * @param ScreenClass 页面类
     * @return 创建的页面，或已有的页面（如果 ScreenID 重复）
     */
    UBaseScreen* OpenPersistentScreen(FName ScreenID, TSubclassOf<UBaseScreen> ScreenClass);
    void ClosePersistentScreen(FName ScreenID);

    /** 获取常驻页面 */
    UBaseScreen* GetPersistentScreen(FName ScreenID) const;

    // ============================================
    // 全屏页面（Screen 层）
    // ============================================

    /**
     * 压入全屏页面。之前的页面被覆盖。
     * @param ScreenClass 页面类
     * @param Param       传递给 OnOpen 的参数
     * @return 创建的页面
     */
    UBaseScreen* PushScreen(TSubclassOf<UBaseScreen> ScreenClass, UObject* Param = nullptr);

    /**
     * 返回上一页（等同于 PopScreen(nullptr)）
     */
    void PopScreen();

    /**
     * 关闭到指定页面（含）。nullptr 表示关闭最顶层一个。
     * 关闭的页面播完动画后从视口移除。
     */
    void PopToScreen(UBaseScreen* TargetScreen);

    // ============================================
    // 弹窗（Popup 层压栈，可叠放多个）
    // ============================================

    UBaseScreen* ShowPopup(TSubclassOf<UBaseScreen> PopupClass, UObject* Param = nullptr);
    void ClosePopup();

    // ============================================
    // 最高层（加载界面、暂停菜单）
    // ============================================

    UBaseScreen* PushOverlay(TSubclassOf<UBaseScreen> OverlayClass, UObject* Param = nullptr);
    void PopOverlay();

    // ============================================
    // 返回键路由
    // ============================================

    /**
     * 按返回键（ESC / 手柄B）时的统一入口。
     * 按 Overlay > Popup > Screen 的优先级关闭最顶层页面。
     * 页面默认的 OnBack() 会调用这里。
     */
    void HandleBack();

    // ============================================
    // 查询
    // ============================================

    UBaseScreen* GetTopScreen(EUILayer Layer) const;

    /** 当前最顶层页面（Overlay > Popup > Screen），没有则返回 nullptr */
    UBaseScreen* GetTopmostScreen() const;
    int32 GetScreenStackDepth(EUILayer Layer) const;

private:
    // ============================================
    // 内部方法
    // ============================================

    /** 压入任意层级（Screen/Popup/Overlay 共用） */
    UBaseScreen* PushToLayer(EUILayer Layer, TSubclassOf<UBaseScreen> ScreenClass, UObject* Param);

    /** 从指定层级弹出（TargetScreen 为空时只弹栈顶） */
    void PopFromLayer(EUILayer Layer, UBaseScreen* TargetScreen = nullptr);

    /** 创建 Widget 并添加到视口指定层级 */
    UBaseScreen* CreateAndAddScreen(TSubclassOf<UBaseScreen> ScreenClass, EUILayer Layer);

    /** 获取层级的 ZOrder 基数 */
    static int32 GetLayerBaseZOrder(EUILayer Layer);

    /** 切换到 UI 输入模式，并把键盘焦点给到指定页面 */
    void SetInputModeUI(UBaseScreen* FocusScreen);

    /** 切换到游戏输入模式 */
    void SetInputModeGame();

    /** 根据当前页面状态同步输入模式（有页面→UI，无页面→Game） */
    void UpdateInputMode();

    // ============================================
    // 数据成员
    // ============================================

    /** 每层独立页面栈 */
    TMap<EUILayer, TArray<UBaseScreen*>> LayerStacks;

    /** 常驻页面表 */
    TMap<FName, UBaseScreen*> PersistentScreens;

    /** 待移除的页面（播完关闭动画后真正移除） */
    struct FPendingRemoval
    {
        UBaseScreen* Screen;
        float RemainingTime;
    };
    TArray<FPendingRemoval> PendingRemovals;
};