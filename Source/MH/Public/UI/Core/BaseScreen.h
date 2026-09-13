#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Core/UIScreenTypes.h"
#include "BaseScreen.generated.h"

class UBaseViewModel;
class UUIManager;

/**
 * BaseScreen - 所有 UI 页面的基类
 *
 * Screen 只负责 View 和 ViewModel 的生命周期绑定。页面栈、覆盖状态、输入模式
 * 和本地玩家归属由 UUIManager 统一管理。
 */
UCLASS()
class MH_API UBaseScreen : public UUserWidget
{
    GENERATED_BODY()

public:
    /** UIManager 打开该页面时最先调用；Param 是 OpenScreen 传入的任意数据。 */
    virtual void OnOpen(UObject* Param = nullptr);
    /** 被新页面盖住（仍在栈中但不再是顶层）时调用。 */
    virtual void OnCovered();
    /** 上层页面关闭、重新回到顶层时调用。 */
    virtual void OnRevealed();
    /** 即将被关闭时调用；真正从栈中移除由 UIManager 负责。 */
    virtual void OnClose();

    /** 返回键默认路由。蓝图页面可以覆写此事件实现页面内返回逻辑。 */
    UFUNCTION(BlueprintNativeEvent, Category = "Screen")
    void OnBack();
    virtual void OnBack_Implementation();

    /** 返回当前页面的数据源；子类可重写，默认由 UIManager 的绑定逻辑决定。 */
    virtual UObject* GetDataSource() const;

    /** 重新解析数据源并刷新 ViewModel。Pawn/Controller 变化时由 UIManager 调用。 */
    UFUNCTION(BlueprintCallable, Category = "Screen")
    void RefreshDataContext();

    /** 切换本页面生效的输入模式，UIManager 会在开/关页面时应用并恢复。 */
    UFUNCTION(BlueprintCallable, Category = "Screen")
    void SetInputModePolicy(EUIScreenInputMode NewInputMode);

    /** 当前页面声明的输入模式策略。 */
    UFUNCTION(BlueprintPure, Category = "Screen")
    EUIScreenInputMode GetInputModePolicy() const { return InputModePolicy; }

    /** 关闭动画时长，作为 UIManager 延迟移除页面的依据。 */
    UFUNCTION(BlueprintPure, Category = "Screen")
    float GetCloseAnimDuration() const { return FMath::Max(0.f, CloseAnimDuration); }

    /** 取拥有该页面的 UI 管理器。 */
    UFUNCTION(BlueprintPure, Category = "Screen")
    UUIManager* GetUIManager() const;

    /** 打开动画钩子（蓝图可覆写），默认空实现。 */
    UFUNCTION(BlueprintNativeEvent, Category = "Screen")
    void PlayOpenAnimation();
    virtual void PlayOpenAnimation_Implementation();

    /** 关闭动画钩子（蓝图可覆写），默认空实现。 */
    UFUNCTION(BlueprintNativeEvent, Category = "Screen")
    void PlayCloseAnimation();
    virtual void PlayCloseAnimation_Implementation();

    /** 拦截 Escape/返回键，转交给 OnBack 后停止事件冒泡。 */
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

    /** 该页面实例持有的 ViewModel，可能为空（纯蓝图页面）。 */
    UFUNCTION(BlueprintCallable, Category = "Screen")
    UBaseViewModel* GetViewModel() const { return ViewModel; }

    /** 是否处于打开状态（已入栈，不代表可见）。 */
    UFUNCTION(BlueprintCallable, Category = "Screen")
    bool IsScreenOpen() const { return bIsOpen; }

    /** 是否是当前栈的顶层页面（弹窗/覆盖层另有优先级）。 */
    UFUNCTION(BlueprintCallable, Category = "Screen")
    bool IsTopmost() const { return bIsTopmost; }

protected:
    /** 控件被 GC/移除时兜底：解绑事件并通知 ViewModel 销毁。 */
    virtual void NativeDestruct() override;

    /** 蓝图生命周期事件，主要用于纯蓝图页面接入统一 UI 系统。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Screen", meta = (DisplayName = "On Screen Opened"))
    void BP_OnOpened(UObject* Param);

    /** 被覆盖时触发。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Screen", meta = (DisplayName = "On Screen Covered"))
    void BP_OnCovered();

    /** 重新回到顶层时触发。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Screen", meta = (DisplayName = "On Screen Revealed"))
    void BP_OnRevealed();

    /** 页面关闭时触发。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Screen", meta = (DisplayName = "On Screen Closed"))
    void BP_OnClosed();

    /** 数据源被重新解析后触发，蓝图在这里重绑数据。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Screen", meta = (DisplayName = "On Data Context Changed"))
    void BP_OnDataContextChanged();

protected:
    /** 要实例化的 ViewModel 类；为空时不创建 ViewModel。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
    TSubclassOf<UBaseViewModel> ViewModelClass;

    /** 该页面的默认输入模式策略；可在运行时用 SetInputModePolicy 改。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
    EUIScreenInputMode InputModePolicy = EUIScreenInputMode::GameOnly;

    /** 关闭动画时长（秒），UIManager 用它决定何时真正移除控件。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
    float CloseAnimDuration = 0.3f;

    /** 由 ViewModelClass 实例化出来的对象，归本页面持有。 */
    UPROPERTY(BlueprintReadOnly, Category = "Screen")
    UBaseViewModel* ViewModel = nullptr;

    /** UIManager 维护的状态标记：是否已打开 / 是否处于栈顶。 */
    bool bIsOpen = false;
    bool bIsTopmost = false;
};

/**
 * ViewModel 属性绑定。绑定前先移除该 Screen 的旧绑定，避免 OnOpen 重入时重复执行。
 */
#define BIND_VM_PROPERTY(VM, PropertyName, SetterFunc) \
    do \
    { \
        if (VM != nullptr) \
        { \
            VM->PropertyName.OnChanged.RemoveAll(this); \
            VM->PropertyName.OnChanged.AddUObject(this, SetterFunc); \
            VM->PropertyName.Broadcast(); \
        } \
    } while (false)
