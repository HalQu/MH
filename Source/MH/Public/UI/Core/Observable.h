#pragma once

template<typename T>
struct TBindedValue
{
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnChanged, T);

public:
    void Set(const T& NewValue)
    {
        if (Current != NewValue)
        {
            T OldValue = Current;
            Current = NewValue;
            OnChanged.Broadcast(Current);
        }
    }

    const T& Get() const { return Current; }

    void Broadcast() const
    {
        OnChanged.Broadcast(Current);
    }

    operator const T& () const { return Current; }
    TBindedValue& operator=(const T& NewValue) { Set(NewValue); return *this; }

public:
    // 订阅者列表，外部直接使用
    // UI 绑定: ViewModel->Health.OnChanged.AddUObject(Widget, &UMyWidget::SetText);
    FOnChanged OnChanged;

private:
    // 值初始化：int32/float/bool 等 POD 类型首次 Set 前也不会是垃圾值
    T Current{};
};