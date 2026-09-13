#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UIEventBus.generated.h"

/** 事件载荷基类。自定义载荷继承它，才能用 Typed 接口做类型校验。 */
USTRUCT(BlueprintType)
struct FUIEventPayload
{
    GENERATED_BODY()

    virtual ~FUIEventPayload() = default;
};

/** 无参数事件的占位载荷，配合 BroadcastTyped/ListenTyped 使用。 */
USTRUCT(BlueprintType)
struct FUIEmptyPayload : public FUIEventPayload
{
    GENERATED_BODY()
};

/** 事件回调签名；Payload 的运行时类型由总线在派发前校验。 */
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
    /** 子系统初始化；可以在这里做监听表等状态的复位。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** 子系统销毁：清空所有监听，避免悬挂回调。 */
    virtual void Deinitialize() override;

    /** 无类型广播：只按事件名分发，不做载荷类型校验（旧接口）。 */
    void Broadcast(FName EventName, const FUIEventPayload& Payload);
    /** 广播一个空载荷事件。 */
    void Broadcast(FName EventName);

    /** 类型安全广播：只有登记了同一 PayloadType 的监听者会收到。 */
    template<typename TPayload>
    void BroadcastTyped(FName EventName, const TPayload& Payload)
    {
        static_assert(TIsDerivedFrom<TPayload, FUIEventPayload>::Value,
            "UI事件载荷必须继承自 FUIEventPayload");
        BroadcastInternal(EventName, Payload, TPayload::StaticStruct());
    }

    /** 无类型监听；返回的句柄用于 Unlisten。 */
    FDelegateHandle Listen(FName EventName, FUIEventHandler InHandler);

    /** 类型安全监听：广播端载荷类型不一致时不会触发。 */
    template<typename TPayload>
    FDelegateHandle ListenTyped(FName EventName, FUIEventHandler InHandler)
    {
        static_assert(TIsDerivedFrom<TPayload, FUIEventPayload>::Value,
            "UI事件载荷必须继承自 FUIEventPayload");
        return ListenInternal(EventName, InHandler, TPayload::StaticStruct());
    }

    /** 按句柄移除单个监听。 */
    void Unlisten(FName EventName, FDelegateHandle InHandle);
    /** 移除某个对象注册的全部监听，适合在 Screen 关闭时批量退订。 */
    void UnlistenAll(const UObject* InObject);

    /** 调试/测试用：当前事件名下的监听数量。 */
    int32 GetListenerCount(FName EventName) const;

private:
    /** 一条监听记录：回调 + 它期望的载荷类型（nullptr 表示不校验）。 */
    struct FUIEventListener
    {
        FUIEventHandler Handler;
        const UScriptStruct* PayloadType = nullptr;
    };

    /** 实际派发：遍历事件名下的监听，按 PayloadType 过滤后调用。 */
    void BroadcastInternal(FName EventName, const FUIEventPayload& Payload, const UScriptStruct* PayloadType);
    /** 实际注册：记录监听者期望的载荷类型。 */
    FDelegateHandle ListenInternal(FName EventName, FUIEventHandler InHandler, const UScriptStruct* PayloadType);

private:
    /** 事件名 -> 监听者列表。 */
    TMap<FName, TArray<FUIEventListener>> Listeners;

    /**
     * 正在广播中的事件名集合。用于阻止同一事件的递归广播。
     */
    TSet<FName> BroadcastingEvents;
};
