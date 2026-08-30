// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Core/UIEventBus.h"


// ============================================
// 生命周期
// ============================================

void UUIEventBus::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    Listeners.Empty();
    BroadcastingEvents.Empty();

    UE_LOG(LogTemp, Log, TEXT("[UIEventBus] Initialized"));
}

void UUIEventBus::Deinitialize()
{
    // 防御性清理：确保所有监听都释放
    Listeners.Empty();
    BroadcastingEvents.Empty();

    UE_LOG(LogTemp, Log, TEXT("[UIEventBus] Deinitialized"));

    Super::Deinitialize();
}

// ============================================
// 广播
// ============================================

void UUIEventBus::Broadcast(FName EventName, const FUIEventPayload& Payload)
{
    // 没有这个事件或没有监听者 — 直接返回，不分配空 TArray
    TArray<FUIEventHandler>* Found = Listeners.Find(EventName);
    if (!Found || Found->Num() == 0)
    {
        return;
    }

    // 递归保护：同一事件在回调里再次广播会被跳过（防止死循环）。
    // 不同事件的嵌套广播（回调里广播另一个事件）是允许的。
    if (BroadcastingEvents.Contains(EventName))
    {
        UE_LOG(LogTemp, Warning, TEXT("[UIEventBus] Recursive broadcast detected for '%s', skipped"),
            *EventName.ToString());
        return;
    }

    BroadcastingEvents.Add(EventName);

    // 复制一份回调列表再遍历 — 防回调里 Unlisten 导致迭代器失效
    TArray<FUIEventHandler> HandlersCopy = *Found;
    for (FUIEventHandler& Handler : HandlersCopy)
    {
        if (Handler.IsBound())
        {
            Handler.Execute(Payload);
        }
    }

    BroadcastingEvents.Remove(EventName);
}

void UUIEventBus::Broadcast(FName EventName)
{
    FUIEmptyPayload Empty;
    Broadcast(EventName, Empty);
}

// ============================================
// 监听
// ============================================

FDelegateHandle UUIEventBus::Listen(FName EventName, FUIEventHandler InHandler)
{
    if (!InHandler.IsBound())
    {
        UE_LOG(LogTemp, Warning, TEXT("[UIEventBus] Listen for '%s' with unbound handler, ignored"),
            *EventName.ToString());
        return FDelegateHandle();
    }

    TArray<FUIEventHandler>& List = Listeners.FindOrAdd(EventName);

    // 防止同一委托实例重复注册（不同实例允许，按 Handle 精确退订）
    const FDelegateHandle NewHandle = InHandler.GetHandle();
    for (const FUIEventHandler& Existing : List)
    {
        if (Existing.GetHandle() == NewHandle)
        {
            UE_LOG(LogTemp, Warning, TEXT("[UIEventBus] Duplicate listener for '%s', ignored"),
                *EventName.ToString());
            return NewHandle;
        }
    }

    List.Add(InHandler);

    UE_LOG(LogTemp, Verbose, TEXT("[UIEventBus] Listening '%s' → %d listener(s)"),
        *EventName.ToString(), List.Num());

    return NewHandle;
}

// ============================================
// 取消监听
// ============================================

void UUIEventBus::Unlisten(FName EventName, FDelegateHandle InHandle)
{
    if (!InHandle.IsValid())
    {
        return;
    }

    TArray<FUIEventHandler>* Found = Listeners.Find(EventName);
    if (!Found)
    {
        return;
    }

    // 按精确 Handle 移除，避免同名对象注册多个回调时误删
    for (int32 i = Found->Num() - 1; i >= 0; --i)
    {
        if ((*Found)[i].GetHandle() == InHandle)
        {
            Found->RemoveAt(i);
            UE_LOG(LogTemp, Verbose, TEXT("[UIEventBus] Unlistened '%s' → %d listener(s)"),
                *EventName.ToString(), Found->Num());
            return;
        }
    }
}

void UUIEventBus::UnlistenAll(const UObject* InObject)
{
    if (!IsValid(InObject))
    {
        return;
    }

    for (auto& Pair : Listeners)
    {
        TArray<FUIEventHandler>& List = Pair.Value;

        for (int32 i = List.Num() - 1; i >= 0; --i)
        {
            if (!List[i].IsBound())
            {
                List.RemoveAt(i);
                continue;
            }

            // 比对绑定对象的原始指针（不需要确切匹配 Handle）
            if (List[i].GetUObject() == InObject)
            {
                List.RemoveAt(i);
            }
        }
    }

    // 清理空的条目（TMap 没有 RemoveAll，用迭代器删除）
    for (auto It = Listeners.CreateIterator(); It; ++It)
    {
        if (It.Value().Num() == 0)
        {
            It.RemoveCurrent();
        }
    }

    UE_LOG(LogTemp, Verbose, TEXT("[UIEventBus] Unlistened all for object"));
}

// ============================================
// 调试
// ============================================

int32 UUIEventBus::GetListenerCount(FName EventName) const
{
    const TArray<FUIEventHandler>* Found = Listeners.Find(EventName);
    if (!Found)
    {
        return 0;
    }

    // 只算 still bound 的
    int32 Count = 0;
    for (const FUIEventHandler& H : *Found)
    {
        if (H.IsBound()) ++Count;
    }
    return Count;
}