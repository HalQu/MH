#pragma once

/**
 * 可绑定值：ViewModel 的输出属性用它持有数据，值变化时自动广播给 UI。
 * 只在值真正变化时派发，避免每帧刷新造成无效 Setter 调用。
 */
template<typename T>
struct TBindedValue
{
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnChanged, T);

public:
    /** 写入新值；与当前值不同时更新并广播。 */
    void Set(const T& NewValue)
    {
        if (Current != NewValue)
        {
            T OldValue = Current;
            Current = NewValue;
            OnChanged.Broadcast(Current);
        }
    }

    /** 只读取值。 */
    const T& Get() const { return Current; }

    /** 不改变值，强行再广播一次；绑定后/数据源重绑时用来刷 UI。 */
    void Broadcast() const
    {
        OnChanged.Broadcast(Current);
    }

    /** 隐式转换与赋值语法糖，让绑定值用起来像普通变量。 */
    operator const T& () const { return Current; }
    TBindedValue& operator=(const T& NewValue) { Set(NewValue); return *this; }

public:
    /**
     * 订阅者列表，外部直接使用。
     * UI 绑定：ViewModel->Health.OnChanged.AddUObject(Widget, &UMyWidget::SetText);
     */
    FOnChanged OnChanged;

private:
    // 值初始化：int32/float/bool 等 POD 类型首次 Set 前也不会是垃圾值
    T Current{};
};
