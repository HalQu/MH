#include "UI/Core/BaseScreen.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerState.h"
#include "UI/Core/BaseViewModel.h"
#include "UI/Core/UIManager.h"

void UBaseScreen::OnOpen(UObject* Param)
{
    if (bIsOpen)
    {
        return;
    }

    bIsOpen = true;
    bIsTopmost = true;

    if (ViewModelClass && !ViewModel)
    {
        ViewModel = NewObject<UBaseViewModel>(this, ViewModelClass);
    }

    if (ViewModel)
    {
        ViewModel->Initialize(this, GetDataSource());
        ViewModel->OnActivated();
        ViewModel->RefreshAll();
    }

    PlayOpenAnimation();
    BP_OnOpened(Param);
}

void UBaseScreen::OnCovered()
{
    if (!bIsOpen || !bIsTopmost)
    {
        return;
    }

    bIsTopmost = false;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    SetIsEnabled(false);

    if (ViewModel)
    {
        ViewModel->OnDeactivated();
    }

    BP_OnCovered();
}

void UBaseScreen::OnRevealed()
{
    if (!bIsOpen || bIsTopmost)
    {
        return;
    }

    bIsTopmost = true;
    SetVisibility(ESlateVisibility::Visible);
    SetIsEnabled(true);

    if (ViewModel)
    {
        ViewModel->OnActivated();
        ViewModel->RefreshAll();
    }

    BP_OnRevealed();
}

void UBaseScreen::OnClose()
{
    if (!bIsOpen)
    {
        return;
    }

    // 先落状态，防止蓝图关闭事件再次调用 OnClose 造成重入。
    bIsOpen = false;
    bIsTopmost = false;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    SetIsEnabled(false);

    BP_OnClosed();


    if (ViewModel)
    {
        ViewModel->OnDestroy();
        ViewModel = nullptr;
    }

    PlayCloseAnimation();
}

void UBaseScreen::OnBack_Implementation()
{
    if (UUIManager* Manager = GetUIManager())
    {
        Manager->HandleBack();
    }
}

UObject* UBaseScreen::GetDataSource() const
{
    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        return PlayerController->GetPlayerState<APlayerState>();
    }

    return nullptr;
}

void UBaseScreen::RefreshDataContext()
{
    if (!ViewModel)
    {
        return;
    }

    ViewModel->SetDataSource(GetDataSource());
    // 被覆盖的页面只更新数据源；恢复显示时 OnRevealed 会再刷新一次。
    if (ViewModel->IsActive())
    {
        ViewModel->RefreshAll();
    }
    BP_OnDataContextChanged();
}

void UBaseScreen::SetInputModePolicy(EUIScreenInputMode NewInputMode)
{
    InputModePolicy = NewInputMode;
}

UUIManager* UBaseScreen::GetUIManager() const
{
    if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
    {
        return LocalPlayer->GetSubsystem<UUIManager>();
    }

    return nullptr;
}

FReply UBaseScreen::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape ||
        InKeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Right)
    {
        OnBack();
        return FReply::Handled();
    }

    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UBaseScreen::NativeDestruct()
{
    // 页面可能被外部销毁而不是经过 UIManager::CloseScreen，这里兜底解绑。
    if (ViewModel)
    {
        ViewModel->OnDestroy();
        ViewModel = nullptr;
    }

    bIsOpen = false;
    bIsTopmost = false;

    if (UUIManager* Manager = GetUIManager())
    {
        Manager->NotifyScreenDestroyed(this);
    }

    Super::NativeDestruct();
}

void UBaseScreen::PlayOpenAnimation_Implementation()
{
}

void UBaseScreen::PlayCloseAnimation_Implementation()
{
}
