#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BaseViewModel.generated.h"

class UUIEventBus;

/**
 * BaseViewModel - Model 与 View 之间的适配层。
 *
 * 子类负责把 Model 数据转换为输出属性，并处理 Screen 转发的用户操作。
 * DataSource 变化时由 UBaseScreen/UIManager 统一调用 SetDataSource()。
 */
UCLASS()
class MH_API UBaseViewModel : public UObject
{
    GENERATED_BODY()

public:
    virtual void Initialize(UObject* InOuter, UObject* InData);

    virtual void OnActivated();
    virtual void OnDeactivated();
    virtual void OnDestroy();

    /**
     * 替换数据源。若 ViewModel 当前处于激活状态，会先解绑旧 Model，再绑定新 Model。
     */
    UFUNCTION(BlueprintCallable, Category = "UI|ViewModel")
    void SetDataSource(UObject* InData);

    /**
     * 强制刷新所有 Output 属性。
     */
    UFUNCTION(BlueprintCallable, Category = "UI|ViewModel")
    virtual void RefreshAll();

    template<typename T>
    T* GetDataSource() const
    {
        return Cast<T>(DataSource.Get());
    }

    UFUNCTION(BlueprintPure, Category = "UI|ViewModel")
    UObject* GetDataSourceObject() const { return DataSource.Get(); }

    UFUNCTION(BlueprintPure, Category = "UI|ViewModel")
    bool IsActive() const { return bIsActive; }

    UUIEventBus* GetBus() const;

protected:
    TWeakObjectPtr<UObject> DataSource;
    TWeakObjectPtr<UObject> OuterWidget;
    bool bIsActive = false;
};
