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
    /** 由 UBaseScreen 创建后调用：InOuter 通常是所属 Screen，InData 是初始数据源。 */
    virtual void Initialize(UObject* InOuter, UObject* InData);

    /** Screen 打开/重新可见时调用；子类在这里订阅事件、启动刷新。 */
    virtual void OnActivated();
    /** Screen 被覆盖或隐藏时调用；子类在这里退订事件，避免后台空转。 */
    virtual void OnDeactivated();
    /** Screen 真正关闭时调用；子类在这里做最终清理。 */
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

    /** 类型安全的 DataSource 访问，类型不匹配时返回 nullptr。 */
    template<typename T>
    T* GetDataSource() const
    {
        return Cast<T>(DataSource.Get());
    }

    /** 蓝图/反射用的 DataSource 读取，不保证已加载。 */
    UFUNCTION(BlueprintPure, Category = "UI|ViewModel")
    UObject* GetDataSourceObject() const { return DataSource.Get(); }

    /** 当前是否处于激活状态（由 Screen 生命周期驱动，不代表一定可见）。 */
    UFUNCTION(BlueprintPure, Category = "UI|ViewModel")
    bool IsActive() const { return bIsActive; }

    /** 取全局 UI 事件总线（挂在 GameInstance 上的子系统）。 */
    UUIEventBus* GetBus() const;

protected:
    /** 数据源用弱引用，避免 Widget/Model 与 ViewModel 形成 GC 引用环。 */
    TWeakObjectPtr<UObject> DataSource;
    /** 创建/持有这个 ViewModel 的 Screen（弱引用，生命周期由 Screen 控制）。 */
    TWeakObjectPtr<UObject> OuterWidget;
    /** 激活标记；SetDataSource/RefreshAll 等只在激活状态下真正刷 UI。 */
    bool bIsActive = false;
};
