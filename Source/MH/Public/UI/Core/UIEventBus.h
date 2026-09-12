#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UIEventBus.generated.h"

USTRUCT(BlueprintType)
struct FUIEventPayload
{
    GENERATED_BODY()

    virtual ~FUIEventPayload() = default;
};

USTRUCT(BlueprintType)
struct FUIEmptyPayload : public FUIEventPayload
{
    GENERATED_BODY()
};

DECLARE_DELEGATE_OneParam(FUIEventHandler, const FUIEventPayload&);

/**
 * UI 事件总线。
 *
 * 推荐使用 BroadcastTyped/ListenTyped，广播时会校验 UScriptStruct，避免
 * 不同事件误用同一载荷类型。旧的无类型接口仍保留给简单调试事件。
 */
UCLASS()
class MH_API UUIEventBus : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    void Broadcast(FName EventName, const FUIEventPayload& Payload);
    void Broadcast(FName EventName);

    template<typename TPayload>
    void BroadcastTyped(FName EventName, const TPayload& Payload)
    {
        static_assert(TIsDerivedFrom<TPayload, FUIEventPayload>::Value,
            "UI事件载荷必须继承自 FUIEventPayload");
        BroadcastInternal(EventName, Payload, TPayload::StaticStruct());
    }

    FDelegateHandle Listen(FName EventName, FUIEventHandler InHandler);

    template<typename TPayload>
    FDelegateHandle ListenTyped(FName EventName, FUIEventHandler InHandler)
    {
        static_assert(TIsDerivedFrom<TPayload, FUIEventPayload>::Value,
            "UI事件载荷必须继承自 FUIEventPayload");
        return ListenInternal(EventName, InHandler, TPayload::StaticStruct());
    }

    void Unlisten(FName EventName, FDelegateHandle InHandle);
    void UnlistenAll(const UObject* InObject);

    int32 GetListenerCount(FName EventName) const;

private:
    struct FUIEventListener
    {
        FUIEventHandler Handler;
        const UScriptStruct* PayloadType = nullptr;
    };

    void BroadcastInternal(FName EventName, const FUIEventPayload& Payload, const UScriptStruct* PayloadType);
    FDelegateHandle ListenInternal(FName EventName, FUIEventHandler InHandler, const UScriptStruct* PayloadType);

private:
    TMap<FName, TArray<FUIEventListener>> Listeners;

    /**
     * 正在广播中的事件名集合。用于阻止同一事件的递归广播。
     */
    TSet<FName> BroadcastingEvents;
};
