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
    UUIManager();

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

    /** 获取指定本地玩家对应的 UI 管理器。 */
    UFUNCTION(BlueprintPure, Category = "UI", meta = (WorldContext = "WorldContextObject"))
    static UUIManager* GetUIManager(const UObject* WorldContextObject, int32 PlayerIndex = 0);

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

    UFUNCTION(BlueprintCallable, Category = "UI|Persistent")
    UBaseScreen* OpenPersistentScreen(
        FName ScreenID,
        TSubclassOf<UBaseScreen> ScreenClass,
        EUIScreenInputMode InputMode = EUIScreenInputMode::GameOnly);

    UFUNCTION(BlueprintCallable, Category = "UI|Persistent")
    void ClosePersistentScreen(FName ScreenID);

    UFUNCTION(BlueprintPure, Category = "UI|Persistent")
    UBaseScreen* GetPersistentScreen(FName ScreenID) const;

    // ============================================
    // 页面栈
    // ============================================

    UFUNCTION(BlueprintCallable, Category = "UI|Screen")
    UBaseScreen* PushScreen(TSubclassOf<UBaseScreen> ScreenClass, UObject* Param = nullptr);

    UFUNCTION(BlueprintCallable, Category = "UI|Screen")
    void PopScreen();

    UFUNCTION(BlueprintCallable, Category = "UI|Screen")
    void PopToScreen(UBaseScreen* TargetScreen);

    UFUNCTION(BlueprintCallable, Category = "UI|Popup")
    UBaseScreen* ShowPopup(TSubclassOf<UBaseScreen> PopupClass, UObject* Param = nullptr);

    UFUNCTION(BlueprintCallable, Category = "UI|Popup")
    void ClosePopup();

    UFUNCTION(BlueprintCallable, Category = "UI|Overlay")
    UBaseScreen* PushOverlay(TSubclassOf<UBaseScreen> OverlayClass, UObject* Param = nullptr);

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

    UFUNCTION(BlueprintCallable, Category = "UI")
    void HandleBack();

    UFUNCTION(BlueprintPure, Category = "UI")
    UBaseScreen* GetTopScreen(EUILayer Layer) const;

    UFUNCTION(BlueprintPure, Category = "UI")
    UBaseScreen* GetTopmostScreen() const;

    UFUNCTION(BlueprintPure, Category = "UI")
    int32 GetScreenStackDepth(EUILayer Layer) const;

    // ============================================
    // FTickableGameObject
    // ============================================

    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual bool IsTickableInEditor() const override { return false; }
    virtual ETickableTickType GetTickableTickType() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UUIManager, STATGROUP_Tickables);
    }

private:
    friend class UBaseScreen;

    UBaseScreen* PushToLayer(EUILayer Layer, TSubclassOf<UBaseScreen> ScreenClass, UObject* Param);
    void PopFromLayer(EUILayer Layer, UBaseScreen* TargetScreen = nullptr);
    UBaseScreen* CreateAndAddScreen(TSubclassOf<UBaseScreen> ScreenClass, EUILayer Layer);
    void NotifyScreenDestroyed(UBaseScreen* Screen);

    void ApplyStackState();
    UBaseScreen* GetActiveStackScreen() const;
    EUIScreenInputMode GetDesiredInputMode() const;
    UBaseScreen* GetInputFocusScreen() const;
    void UpdateInputMode();
    void SetInputMode(EUIScreenInputMode InputMode, UBaseScreen* FocusScreen);

    void CloseScreen(UBaseScreen* Screen, bool bImmediate);
    void ScheduleRemoval(UBaseScreen* Screen);
    void ApplyRemoveFromParent(UBaseScreen* Screen);

    void BindToPlayerController(APlayerController* PlayerController);
    void UnbindFromPlayerController();

    /** 延迟到下一帧刷新，等待 Pawn/PlayerState 的复制顺序稳定。 */
    void RequestDataContextRefresh();

    UFUNCTION()
    void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

    static int32 GetLayerBaseZOrder(EUILayer Layer);

private:
    TMap<EUILayer, TArray<UBaseScreen*>> LayerStacks;
    TMap<FName, UBaseScreen*> PersistentScreens;

    TWeakObjectPtr<UBaseScreen> ActiveStackScreen;
    TWeakObjectPtr<APlayerController> BoundPlayerController;

    /** 由管理器持有的界面强引用，避免从视口移除后、延迟销毁前被 GC。 */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UBaseScreen>> OwnedScreens;

    bool bIsShuttingDown = false;
    bool bRefreshDataContextNextTick = false;

    struct FPendingRemoval
    {
        TWeakObjectPtr<UBaseScreen> Screen;
        float RemainingTime = 0.f;
    };
    TArray<FPendingRemoval> PendingRemovals;
};
