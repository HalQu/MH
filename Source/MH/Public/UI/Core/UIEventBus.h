// Fill out your copyright notice in the Description page of Project Settings.

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
 * UI事件总线
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


    FDelegateHandle Listen(FName EventName, FUIEventHandler InHandler);
    void Unlisten(FName EventName, FDelegateHandle InHandle);
    void UnlistenAll(const UObject* InObject);

    int32 GetListenerCount(FName EventName) const;

private:

    TMap<FName, TArray<FUIEventHandler>> Listeners;

    /**
     * 正在广播中的事件名集合。
     * 用于防止同一事件的递归广播造成死循环；
     * 不同事件的嵌套广播（回调里广播另一个事件）是允许的。
     */
    TSet<FName> BroadcastingEvents;
};