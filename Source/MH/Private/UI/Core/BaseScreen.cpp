// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Core/BaseScreen.h"
#include "UI/Core/BaseViewModel.h"
#include "UI/Core/UIManager.h"
#include "GameFramework/PlayerState.h"

void UBaseScreen::OnOpen(UObject* Param)
{
    bIsOpen = true;
    bIsTopmost = true;

    // 1. 创建 ViewModel
    if (ViewModelClass && !ViewModel)
    {
        ViewModel = NewObject<UBaseViewModel>(this, ViewModelClass);
    }

    // 2. 初始化 ViewModel
    if (ViewModel)
    {
        ViewModel->Initialize(this, GetDataSource());
        ViewModel->OnActivated();
        ViewModel->RefreshAll();
    }

    // 3. 播放打开动画
    PlayOpenAnimation();
}

void UBaseScreen::OnCovered()
{
    bIsTopmost = false;

    SetVisibility(ESlateVisibility::HitTestInvisible);
    SetIsEnabled(false);

    if (ViewModel)
    {
        ViewModel->OnDeactivated();
    }
}

void UBaseScreen::OnRevealed()
{
    bIsTopmost = true;

    SetVisibility(ESlateVisibility::Visible);
    SetIsEnabled(true);

    if (ViewModel)
    {
        ViewModel->OnActivated();
        ViewModel->RefreshAll();
    }
}

void UBaseScreen::OnClose()
{
    bIsOpen = false;

    if (ViewModel)
    {
        ViewModel->OnDestroy();
        ViewModel = nullptr;
    }

    PlayCloseAnimation();

    // 不在这里移除自己：UIManager 的 PendingRemovals 会在 CloseAnimDuration 后统一移除，
    // 保证关闭动画完整播完，也避免和 UIManager 重复移除。
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

void UBaseScreen::OnBack()
{
    if (OwnerUIManager)
    {
        OwnerUIManager->HandleBack();
    }
}

UObject* UBaseScreen::GetDataSource() const
{
    if (APlayerController* PC = GetOwningPlayer())
    {
        return PC->GetPlayerState<APlayerState>();
    }
    return nullptr;
}

// BlueprintNativeEvent 的 C++ 默认实现（蓝图未重写时使用）
void UBaseScreen::PlayOpenAnimation_Implementation()
{
}

void UBaseScreen::PlayCloseAnimation_Implementation()
{
}