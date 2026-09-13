#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "UI/Core/UIScreenTypes.h"
#include "UIManager.generated.h"

class APlayerController;
class APawn;
class UBaseScreen;
class ULocalPlayer;

/**
 * UIManager - 本地玩家 UI 调度器
 *
 * 每个 ULocalPlayer 拥有独立实例，UI 创建、输入模式和页面栈都只作用于
 * 当前客户端。服务器端没有 LocalPlayer，因此不会创建界面。
 */
UCLASS()
class MH_API UUIManager : public ULocalPlayerSubsystem, public FTickableGameObject
{
    GENERATED_BODY()

public:
    /** 构造：初始化 Tick 所需状态，不做具体的界面创建。 */
    UUIManager();

    /** 子系统初始化，注册到 LocalPlayer 生命周期。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** 退出时关闭所有页面并解绑控制器，防止悬挂引用。 */
    virtual void Deinitialize() override;
    /** 本地玩家换 Controller（重连/换关卡）时重新绑定并刷新数据上下文。 */
    virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

    /** 获取指定本地玩家对应的 UI 管理器。 */
    UFUNCTION(BlueprintPure, Category = "UI", meta = (WorldContext = "WorldContextObject"))
    static UUIManager* GetUIManager(const UObject* WorldContextObject, int32 PlayerIndex = 0);

    /** 取该 UIManager 当前绑定的本地 PlayerController。 */
    UFUNCTION(BlueprintPure, Category = "UI")
    APlayerController* GetOwningPlayerController() const;

    /** 在本机控制器所属的 UIManager 上打开常驻页面；远程控制器返回 nullptr。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Persistent")
    static UBaseScreen* OpenPersistentScreenForLocalPlayer(
        APlayerController* PlayerController,
        FName ScreenID,
        TSubclassOf<UBaseScreen> ScreenClass,
        EUIScreenInputMode InputMode = EUIScreenInputMode::GameOnly);


    // ============================================
    // 常驻页面（HUD / 主菜单等）
    // ============================================

    /** 打开一个常驻页面（HUD/主菜单）；同一 ScreenID 已存在时复用旧实例。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Persistent")
    UBaseScreen* OpenPersistentScreen(
        FName ScreenID,
        TSubclassOf<UBaseScreen> ScreenClass,
        EUIScreenInputMode InputMode = EUIScreenInputMode::GameOnly);

    /** 关闭并移除指定常驻页面。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Persistent")
    void ClosePersistentScreen(FName ScreenID);

    /** 查询常驻页面，不存在时返回 nullptr。 */
    UFUNCTION(BlueprintPure, Category = "UI|Persistent")
    UBaseScreen* GetPersistentScreen(FName ScreenID) const;

    // ============================================
    // 页面栈
    // ============================================

    /** 将页面压入 Screen 层，成为最高优先级的可交互页面。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Screen")
    UBaseScreen* PushScreen(TSubclassOf<UBaseScreen> ScreenClass, UObject* Param = nullptr);

    /** 弹出 Screen 层栈顶页面（带动画延迟移除）。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Screen")
    void PopScreen();

    /** 弹到指定页面（含它自己被关闭）；TargetScreen 不在栈中时忽略。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Screen")
    void PopToScreen(UBaseScreen* TargetScreen);

    /** 弹出弹窗层，优先级高于普通页面、低于 Overlay。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Popup")
    UBaseScreen* ShowPopup(TSubclassOf<UBaseScreen> PopupClass, UObject* Param = nullptr);

    /** 关闭当前弹窗。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Popup")
    void ClosePopup();

    /** 压入覆盖层，最高层级，通常用于加载遮罩/全局提示。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Overlay")
    UBaseScreen* PushOverlay(TSubclassOf<UBaseScreen> OverlayClass, UObject* Param = nullptr);

    /** 关闭当前覆盖层。 */
    UFUNCTION(BlueprintCallable, Category = "UI|Overlay")
    void PopOverlay();

    /** 关闭所有页面。bImmediate=false 时保留关闭动画等待时间。 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void CloseAllScreens(bool bImmediate = true);

    /** 重新解析所有页面的数据源。Pawn、PlayerState 或 PlayerController 变化时由管理器调用。 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void RefreshAllScreensDataContext();

    // ============================================
    // 返回键路由 / 查询
    // ============================================

    /** 返回键路由：优先关 Overlay，再 Popup，最后普通页面。 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void HandleBack();

    /** 取指定层的栈顶页面。 */
    UFUNCTION(BlueprintPure, Category = "UI")
    UBaseScreen* GetTopScreen(EUILayer Layer) const;

    /** 取当前全局最顶层、正在接收输入的页面（Overlay > Popup > Screen）。 */
    UFUNCTION(BlueprintPure, Category = "UI")
    UBaseScreen* GetTopmostScreen() const;

    /** 指定层的页面数量，常用于判断某层是否为空。 */
    UFUNCTION(BlueprintPure, Category = "UI")
    int32 GetScreenStackDepth(EUILayer Layer) const;

    // ============================================
    // FTickableGameObject
    // ============================================

    /** 每帧处理延迟移除队列和待刷新的数据上下文。 */
    virtual void Tick(float DeltaTime) override;
    /** 只在有待卸载页面或待刷新标记时才 Tick（Conditional）。 */
    virtual bool IsTickable() const override;
    /** 暂停时仍需推进关闭动画，所以允许 Tick。 */
    virtual bool IsTickableWhenPaused() const override { return true; }
    /** 编辑器预览不参与 Tick，避免影响编辑器性能。 */
    virtual bool IsTickableInEditor() const override { return false; }
    /** 采用条件 Tick，由 IsTickable 决定是否参与。 */
    virtual ETickableTickType GetTickableTickType() const override;
    /** Tick 归属的世界，取自当前 LocalPlayer。 */
    virtual UWorld* GetTickableGameObjectWorld() const override;
    /** 性能分析用的 stat id。 */
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UUIManager, STATGROUP_Tickables);
    }

private:
    friend class UBaseScreen;

    /** 创建页面、入栈并广播状态；所有层级的统一入口。 */
    UBaseScreen* PushToLayer(EUILayer Layer, TSubclassOf<UBaseScreen> ScreenClass, UObject* Param);
    /** 从指定层出栈；TargetScreen 为空时弹栈顶。 */
    void PopFromLayer(EUILayer Layer, UBaseScreen* TargetScreen = nullptr);
    /** 实例化控件、加入视口并登记到 OwnedScreens。 */
    UBaseScreen* CreateAndAddScreen(TSubclassOf<UBaseScreen> ScreenClass, EUILayer Layer);
    /** 页面被外部销毁时从各个栈和缓存中清除。 */
    void NotifyScreenDestroyed(UBaseScreen* Screen);

    /** 重算当前活动页面，并派发 Covered/Revealed 与输入模式。 */
    void ApplyStackState();
    /** 按 Overlay > Popup > Screen 的优先级返回当前活动页面。 */
    UBaseScreen* GetActiveStackScreen() const;
    /** 合并常驻页面与活动页面的输入策略，得出最终输入模式。 */
    EUIScreenInputMode GetDesiredInputMode() const;
    /** 找出需要接收键盘焦点的页面（UIOnly 页面优先）。 */
    UBaseScreen* GetInputFocusScreen() const;
    /** 重新计算并应用到 PlayerController。 */
    void UpdateInputMode();
    /** 实际调用 SetInputMode/光标设置。 */
    void SetInputMode(EUIScreenInputMode InputMode, UBaseScreen* FocusScreen);

    /** 关闭单个页面；bImmediate 决定是否等待关闭动画。 */
    void CloseScreen(UBaseScreen* Screen, bool bImmediate);
    /** 把页面加入延迟移除队列，等待关闭动画播完。 */
    void ScheduleRemoval(UBaseScreen* Screen);
    /** 真正从视口/持有列表移除页面。 */
    void ApplyRemoveFromParent(UBaseScreen* Screen);

    /** 绑定本地 PlayerController，并监听 Pawn 切换。 */
    void BindToPlayerController(APlayerController* PlayerController);
    /** 解绑 PlayerController 的所有监听。 */
    void UnbindFromPlayerController();

    /** 延迟到下一帧刷新，等待 Pawn/PlayerState 的复制顺序稳定。 */
    void RequestDataContextRefresh();

    /** Pawn 切换（复活/换角色）时重新解析页面数据源。 */
    UFUNCTION()
    void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

    /** 层级数值直接作为基础 ZOrder，保证不同层的显示顺序。 */
    static int32 GetLayerBaseZOrder(EUILayer Layer);

private:
    /** 各层级的页面栈（Screen/Popup/Overlay）；World/HUD 一般不用栈。 */
    TMap<EUILayer, TArray<UBaseScreen*>> LayerStacks;
    /** 常驻页面，不参与出栈，按 ScreenID 唯一。 */
    TMap<FName, UBaseScreen*> PersistentScreens;

    /** 当前正在接收输入/显示的栈页面；用于派发 Covered/Revealed。 */
    TWeakObjectPtr<UBaseScreen> ActiveStackScreen;
    /** 当前绑定的本地 PlayerController（弱引用，不影响其生命周期）。 */
    TWeakObjectPtr<APlayerController> BoundPlayerController;

    /** 由管理器持有的界面强引用，避免从视口移除后、延迟销毁前被 GC。 */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UBaseScreen>> OwnedScreens;

    /** 退出中标记；置位后拒绝创建新页面。 */
    bool bIsShuttingDown = false;
    /** 下一帧需要重新解析所有页面数据源的标记（等待 Pawn 复制稳定）。 */
    bool bRefreshDataContextNextTick = false;

    /** 等待关闭动画播完的页面记录。 */
    struct FPendingRemoval
    {
        TWeakObjectPtr<UBaseScreen> Screen;
        float RemainingTime = 0.f;
    };
    /** 延迟移除队列，在 Tick 里倒计时。 */
    TArray<FPendingRemoval> PendingRemovals;
};
