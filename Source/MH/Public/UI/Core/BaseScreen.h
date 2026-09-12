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
    virtual void OnOpen(UObject* Param = nullptr);
    virtual void OnCovered();
    virtual void OnRevealed();
    virtual void OnClose();

    /** 返回键默认路由。蓝图页面可以覆写此事件实现页面内返回逻辑。 */
    UFUNCTION(BlueprintNativeEvent, Category = "Screen")
    void OnBack();
    virtual void OnBack_Implementation();

    virtual UObject* GetDataSource() const;

    /** 重新解析数据源并刷新 ViewModel。Pawn/Controller 变化时由 UIManager 调用。 */
    UFUNCTION(BlueprintCallable, Category = "Screen")
    void RefreshDataContext();

    UFUNCTION(BlueprintCallable, Category = "Screen")
    void SetInputModePolicy(EUIScreenInputMode NewInputMode);

    UFUNCTION(BlueprintPure, Category = "Screen")
    EUIScreenInputMode GetInputModePolicy() const { return InputModePolicy; }

    UFUNCTION(BlueprintPure, Category = "Screen")
    float GetCloseAnimDuration() const { return FMath::Max(0.f, CloseAnimDuration); }

    UFUNCTION(BlueprintPure, Category = "Screen")
    UUIManager* GetUIManager() const;

    UFUNCTION(BlueprintNativeEvent, Category = "Screen")
    void PlayOpenAnimation();
    virtual void PlayOpenAnimation_Implementation();

    UFUNCTION(BlueprintNativeEvent, Category = "Screen")
    void PlayCloseAnimation();
    virtual void PlayCloseAnimation_Implementation();

    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

    UFUNCTION(BlueprintCallable, Category = "Screen")
    UBaseViewModel* GetViewModel() const { return ViewModel; }

    UFUNCTION(BlueprintCallable, Category = "Screen")
    bool IsScreenOpen() const { return bIsOpen; }

    UFUNCTION(BlueprintCallable, Category = "Screen")
    bool IsTopmost() const { return bIsTopmost; }

protected:
    virtual void NativeDestruct() override;

    /** 蓝图生命周期事件，主要用于纯蓝图页面接入统一 UI 系统。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Screen", meta = (DisplayName = "On Screen Opened"))
    void BP_OnOpened(UObject* Param);

    UFUNCTION(BlueprintImplementableEvent, Category = "Screen", meta = (DisplayName = "On Screen Covered"))
    void BP_OnCovered();

    UFUNCTION(BlueprintImplementableEvent, Category = "Screen", meta = (DisplayName = "On Screen Revealed"))
    void BP_OnRevealed();

    UFUNCTION(BlueprintImplementableEvent, Category = "Screen", meta = (DisplayName = "On Screen Closed"))
    void BP_OnClosed();

    UFUNCTION(BlueprintImplementableEvent, Category = "Screen", meta = (DisplayName = "On Data Context Changed"))
    void BP_OnDataContextChanged();

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
    TSubclassOf<UBaseViewModel> ViewModelClass;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
    EUIScreenInputMode InputModePolicy = EUIScreenInputMode::GameOnly;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
    float CloseAnimDuration = 0.3f;

    UPROPERTY(BlueprintReadOnly, Category = "Screen")
    UBaseViewModel* ViewModel = nullptr;

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
