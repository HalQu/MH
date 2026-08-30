// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Core/BaseViewModel.h"
#include "UI/Core/UIEventBus.h"
void UBaseViewModel::Initialize(UObject* InOuter, UObject* InData)
{
    OuterWidget = InOuter;
    DataSource = InData;
    bIsActive = false;
}

void UBaseViewModel::OnActivated()
{
    bIsActive = true;
    // 子类在这注册 Model 回调
}

void UBaseViewModel::OnDeactivated()
{
    bIsActive = false;

    // 安全撤退：解绑所有 UIEventBus 监听
    if (UUIEventBus* Bus = GetBus())
    {
        Bus->UnlistenAll(this);
    }

    // 子类在这解绑 Model 回调
}

void UBaseViewModel::OnDestroy()
{
    OnDeactivated();
    DataSource.Reset();
    OuterWidget.Reset();
}

UUIEventBus* UBaseViewModel::GetBus() const
{
    // 从 OuterWidget 或 DataSource 推 World
    UWorld* World = nullptr;

    if (OuterWidget.IsValid())
    {
        World = OuterWidget->GetWorld();
    }
    else if (DataSource.IsValid())
    {
        World = DataSource->GetWorld();
    }

    if (World && World->GetGameInstance())
    {
        return World->GetGameInstance()->GetSubsystem<UUIEventBus>();
    }

    return nullptr;
}