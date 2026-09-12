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
}

void UBaseViewModel::OnDeactivated()
{
    bIsActive = false;

    if (UUIEventBus* Bus = GetBus())
    {
        Bus->UnlistenAll(this);
    }
}

void UBaseViewModel::OnDestroy()
{
    OnDeactivated();
    DataSource.Reset();
    OuterWidget.Reset();
}

void UBaseViewModel::SetDataSource(UObject* InData)
{
    if (DataSource.Get() == InData)
    {
        return;
    }

    const bool bWasActive = bIsActive;
    if (bWasActive)
    {
        OnDeactivated();
    }

    DataSource = InData;

    if (bWasActive)
    {
        OnActivated();
    }
}

void UBaseViewModel::RefreshAll()
{
}

UUIEventBus* UBaseViewModel::GetBus() const
{
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
