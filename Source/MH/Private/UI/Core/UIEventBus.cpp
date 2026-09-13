#include "UI/Core/UIEventBus.h"

#include "Misc/ScopeExit.h"

void UUIEventBus::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    Listeners.Empty();
    BroadcastingEvents.Empty();

    UE_LOG(LogTemp, Log, TEXT("[UIEventBus] Initialized"));
}

void UUIEventBus::Deinitialize()
{
    Listeners.Empty();
    BroadcastingEvents.Empty();

    UE_LOG(LogTemp, Log, TEXT("[UIEventBus] Deinitialized"));

    Super::Deinitialize();
}

void UUIEventBus::Broadcast(FName EventName, const FUIEventPayload& Payload)
{
    BroadcastInternal(EventName, Payload, nullptr);
}

void UUIEventBus::Broadcast(FName EventName)
{
    FUIEmptyPayload Empty;
    // 旧入口视为空载荷事件：无类型监听仍兼容，强类型监听需声明 FUIEmptyPayload。
    BroadcastInternal(EventName, Empty, FUIEmptyPayload::StaticStruct());
}

// 同一事件名的递归广播会被丢弃，避免监听者再次 Broadcast 导致无限递归。
void UUIEventBus::BroadcastInternal(
    FName EventName,
    const FUIEventPayload& Payload,
    const UScriptStruct* PayloadType)
{
    TArray<FUIEventListener>* Found = Listeners.Find(EventName);
    if (!Found || Found->Num() == 0)
    {
        return;
    }

    if (BroadcastingEvents.Contains(EventName))
    {
        UE_LOG(LogTemp, Warning, TEXT("[UIEventBus] Recursive broadcast detected for '%s', skipped"),
            *EventName.ToString());
        return;
    }

    TArray<FDelegateHandle> Handles;
    Handles.Reserve(Found->Num());
    for (const FUIEventListener& Listener : *Found)
    {
        if (Listener.Handler.IsBound())
        {
            Handles.Add(Listener.Handler.GetHandle());
        }
    }

    BroadcastingEvents.Add(EventName);
    ON_SCOPE_EXIT
    {
        BroadcastingEvents.Remove(EventName);
    };

    // 每次执行前重新查找当前监听者。广播中 Unlisten 的对象不会在本次广播中继续执行。
    for (const FDelegateHandle& Handle : Handles)
    {
        TArray<FUIEventListener>* CurrentListeners = Listeners.Find(EventName);
        if (!CurrentListeners)
        {
            return;
        }

        FUIEventListener* Listener = CurrentListeners->FindByPredicate(
            [&Handle](const FUIEventListener& Candidate)
            {
                return Candidate.Handler.GetHandle() == Handle;
            });

        if (!Listener || !Listener->Handler.IsBound())
        {
            continue;
        }

        if (PayloadType)
        {
            if (Listener->PayloadType && Listener->PayloadType != PayloadType)
            {
                continue;
            }
        }
        else if (Listener->PayloadType != nullptr)
        {
            // 无类型 Broadcast 不会误触发认为自己是强类型监听的处理器。
            continue;
        }

        // PayloadType 为空的旧 Listen 通配任意事件载荷；强类型监听只接收同类型载荷。
        Listener->Handler.Execute(Payload);
    }
}

FDelegateHandle UUIEventBus::Listen(FName EventName, FUIEventHandler InHandler)
{
    return ListenInternal(EventName, InHandler, nullptr);
}

FDelegateHandle UUIEventBus::ListenInternal(
    FName EventName,
    FUIEventHandler InHandler,
    const UScriptStruct* PayloadType)
{
    if (!InHandler.IsBound())
    {
        UE_LOG(LogTemp, Warning, TEXT("[UIEventBus] Listen for '%s' with unbound handler, ignored"),
            *EventName.ToString());
        return FDelegateHandle();
    }

    TArray<FUIEventListener>& List = Listeners.FindOrAdd(EventName);
    const FDelegateHandle NewHandle = InHandler.GetHandle();

    for (const FUIEventListener& Existing : List)
    {
        if (Existing.Handler.GetHandle() == NewHandle)
        {
            UE_LOG(LogTemp, Warning, TEXT("[UIEventBus] Duplicate listener for '%s', ignored"),
                *EventName.ToString());
            return NewHandle;
        }
    }

    List.Add({ MoveTemp(InHandler), PayloadType });

    UE_LOG(LogTemp, Verbose, TEXT("[UIEventBus] Listening '%s' -> %d listener(s)"),
        *EventName.ToString(), List.Num());

    return NewHandle;
}

void UUIEventBus::Unlisten(FName EventName, FDelegateHandle InHandle)
{
    if (!InHandle.IsValid())
    {
        return;
    }

    TArray<FUIEventListener>* Found = Listeners.Find(EventName);
    if (!Found)
    {
        return;
    }

    for (int32 Index = Found->Num() - 1; Index >= 0; --Index)
    {
        if ((*Found)[Index].Handler.GetHandle() == InHandle)
        {
            Found->RemoveAt(Index);
            UE_LOG(LogTemp, Verbose, TEXT("[UIEventBus] Unlistened '%s' -> %d listener(s)"),
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
        TArray<FUIEventListener>& List = Pair.Value;

        for (int32 Index = List.Num() - 1; Index >= 0; --Index)
        {
            if (!List[Index].Handler.IsBound() || List[Index].Handler.GetUObject() == InObject)
            {
                List.RemoveAt(Index);
            }
        }
    }

    for (auto Iterator = Listeners.CreateIterator(); Iterator; ++Iterator)
    {
        if (Iterator.Value().Num() == 0)
        {
            Iterator.RemoveCurrent();
        }
    }

    UE_LOG(LogTemp, Verbose, TEXT("[UIEventBus] Unlistened all for object '%s'"),
        *InObject->GetName());
}

int32 UUIEventBus::GetListenerCount(FName EventName) const
{
    const TArray<FUIEventListener>* Found = Listeners.Find(EventName);
    if (!Found)
    {
        return 0;
    }

    int32 Count = 0;
    for (const FUIEventListener& Listener : *Found)
    {
        if (Listener.Handler.IsBound())
        {
            ++Count;
        }
    }
    return Count;
}
