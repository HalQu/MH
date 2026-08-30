// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Core/UIManager.h"
#include "UI/Core/BaseScreen.h"

void UUIManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    LayerStacks.Empty();
    PersistentScreens.Empty();
    PendingRemovals.Empty();

    UE_LOG(LogTemp, Log, TEXT("[UIManager] Initialized"));
}

void UUIManager::Deinitialize()
{
    // 清理所有页面
    for (auto& Pair : LayerStacks)
    {
        for (UBaseScreen* Screen : Pair.Value)
        {
            if (IsValid(Screen))
            {
                Screen->OnClose();
                Screen->RemoveFromParent();
            }
        }
    }
    LayerStacks.Empty();

    for (auto& Pair : PersistentScreens)
    {
        if (IsValid(Pair.Value))
        {
            Pair.Value->RemoveFromParent();
        }
    }
    PersistentScreens.Empty();

    for (const FPendingRemoval& Removal : PendingRemovals)
    {
        if (IsValid(Removal.Screen))
        {
            Removal.Screen->RemoveFromParent();
        }
    }
    PendingRemovals.Empty();

    UE_LOG(LogTemp, Log, TEXT("[UIManager] Deinitialized"));

    Super::Deinitialize();
}

void UUIManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 处理待移除的页面
    for (int32 i = PendingRemovals.Num() - 1; i >= 0; --i)
    {
        PendingRemovals[i].RemainingTime -= DeltaTime;
        if (PendingRemovals[i].RemainingTime <= 0.0f)
        {
            if (IsValid(PendingRemovals[i].Screen))
            {
                PendingRemovals[i].Screen->RemoveFromParent();
            }
            PendingRemovals.RemoveAt(i);
        }
    }
}

// ============================================
// 常驻页面
// ============================================

UBaseScreen* UUIManager::OpenPersistentScreen(FName ScreenID, TSubclassOf<UBaseScreen> ScreenClass)
{
    // 已存在则返回已有的
    if (UBaseScreen** Found = PersistentScreens.Find(ScreenID))
    {
        if (IsValid(*Found))
        {
            UE_LOG(LogTemp, Verbose, TEXT("[UIManager] Persistent '%s' already open"), *ScreenID.ToString());
            return *Found;
        }
    }

    if (!ScreenClass)
    {
        UE_LOG(LogTemp, Error, TEXT("[UIManager] OpenPersistentScreen '%s' with null class"), *ScreenID.ToString());
        return nullptr;
    }

    UBaseScreen* Screen = CreateAndAddScreen(ScreenClass, EUILayer::HUD);
    if (Screen)
    {
        Screen->OnOpen(nullptr);
        PersistentScreens.Add(ScreenID, Screen);

        UE_LOG(LogTemp, Log, TEXT("[UIManager] Opened persistent screen '%s'"), *ScreenID.ToString());
    }

    return Screen;
}

void UUIManager::ClosePersistentScreen(FName ScreenID)
{
    UBaseScreen* Screen = nullptr;
    PersistentScreens.RemoveAndCopyValue(ScreenID, Screen);

    if (IsValid(Screen))
    {
        Screen->OnClose();
        PendingRemovals.Add({ Screen, Screen->CloseAnimDuration });

        UE_LOG(LogTemp, Log, TEXT("[UIManager] Closed persistent screen '%s'"), *ScreenID.ToString());
    }
}

UBaseScreen* UUIManager::GetPersistentScreen(FName ScreenID) const
{
    return PersistentScreens.FindRef(ScreenID);
}

// ============================================
// 全屏页面 / 弹窗 / 最高层 — 统一压栈实现
// ============================================

UBaseScreen* UUIManager::PushScreen(TSubclassOf<UBaseScreen> ScreenClass, UObject* Param)
{
    return PushToLayer(EUILayer::Screen, ScreenClass, Param);
}

void UUIManager::PopScreen()
{
    PopFromLayer(EUILayer::Screen);
}

void UUIManager::PopToScreen(UBaseScreen* TargetScreen)
{
    PopFromLayer(EUILayer::Screen, TargetScreen);
}

UBaseScreen* UUIManager::ShowPopup(TSubclassOf<UBaseScreen> PopupClass, UObject* Param)
{
    return PushToLayer(EUILayer::Popup, PopupClass, Param);
}

void UUIManager::ClosePopup()
{
    PopFromLayer(EUILayer::Popup);
}

UBaseScreen* UUIManager::PushOverlay(TSubclassOf<UBaseScreen> OverlayClass, UObject* Param)
{
    return PushToLayer(EUILayer::Overlay, OverlayClass, Param);
}

void UUIManager::PopOverlay()
{
    PopFromLayer(EUILayer::Overlay);
}

void UUIManager::HandleBack()
{
    if (GetTopScreen(EUILayer::Overlay))
    {
        PopFromLayer(EUILayer::Overlay);
    }
    else if (GetTopScreen(EUILayer::Popup))
    {
        PopFromLayer(EUILayer::Popup);
    }
    else if (GetTopScreen(EUILayer::Screen))
    {
        PopFromLayer(EUILayer::Screen);
    }
    else
    {
        UE_LOG(LogTemp, Verbose, TEXT("[UIManager] HandleBack: nothing to close"));
    }
}

// ============================================
// 内部方法
// ============================================

UBaseScreen* UUIManager::PushToLayer(EUILayer Layer, TSubclassOf<UBaseScreen> ScreenClass, UObject* Param)
{
    if (!ScreenClass)
    {
        UE_LOG(LogTemp, Error, TEXT("[UIManager] PushToLayer(%d) with null class"), (int32)Layer);
        return nullptr;
    }

    // 1. 覆盖同层当前栈顶（同一层叠放时，低层页面暂停）
    UBaseScreen* SameLayerTop = GetTopScreen(Layer);
    if (SameLayerTop)
    {
        SameLayerTop->OnCovered();
    }

    // 2. 高层页面（Popup/Overlay）打开时，覆盖 Screen 层栈顶
    if (Layer == EUILayer::Popup || Layer == EUILayer::Overlay)
    {
        if (UBaseScreen* ScreenTop = GetTopScreen(EUILayer::Screen))
        {
            ScreenTop->OnCovered();
        }
    }

    // 3. 创建新页面
    UBaseScreen* NewScreen = CreateAndAddScreen(ScreenClass, Layer);
    if (!NewScreen)
    {
        // 创建失败：回滚覆盖状态
        if (SameLayerTop)
        {
            SameLayerTop->OnRevealed();
        }
        if (Layer == EUILayer::Popup || Layer == EUILayer::Overlay)
        {
            if (UBaseScreen* ScreenTop = GetTopScreen(EUILayer::Screen))
            {
                ScreenTop->OnRevealed();
            }
        }
        return nullptr;
    }

    NewScreen->OnOpen(Param);

    // 4. 压栈
    TArray<UBaseScreen*>& Stack = LayerStacks.FindOrAdd(Layer);
    Stack.Add(NewScreen);

    // 5. 输入模式：UI Only，聚焦新页面
    SetInputModeUI(NewScreen);

    UE_LOG(LogTemp, Log, TEXT("[UIManager] Pushed screen to layer %d. Stack depth: %d"),
        (int32)Layer, Stack.Num());

    return NewScreen;
}

void UUIManager::PopFromLayer(EUILayer Layer, UBaseScreen* TargetScreen)
{
    TArray<UBaseScreen*>* StackPtr = LayerStacks.Find(Layer);
    if (!StackPtr || StackPtr->Num() == 0)
    {
        return;
    }

    TArray<UBaseScreen*>& Stack = *StackPtr;

    // 找到目标位置
    int32 TargetIndex = Stack.Num() - 1; // 默认只关栈顶
    if (TargetScreen)
    {
        TargetIndex = Stack.IndexOfByKey(TargetScreen);
        if (TargetIndex == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("[UIManager] PopFromLayer: target not in layer %d"), (int32)Layer);
            return;
        }
    }

    // 关闭目标及之上所有页面
    const int32 CountToClose = Stack.Num() - TargetIndex;

    for (int32 i = Stack.Num() - 1; i >= TargetIndex; --i)
    {
        UBaseScreen* Screen = Stack[i];
        if (IsValid(Screen))
        {
            Screen->OnClose();

            // 加入待移除队列，等动画播完（UIManager Tick 统一移除）
            PendingRemovals.Add({ Screen, Screen->CloseAnimDuration });
        }
    }

    Stack.RemoveAt(TargetIndex, CountToClose);

    UE_LOG(LogTemp, Log, TEXT("[UIManager] Popped %d screen(s) from layer %d. Remaining: %d"),
        CountToClose, (int32)Layer, Stack.Num());

    // 恢复本层新的栈顶
    if (Stack.Num() > 0)
    {
        Stack.Last()->OnRevealed();
    }
    else if (Layer == EUILayer::Popup || Layer == EUILayer::Overlay)
    {
        // 高层页面全部关闭，恢复之前被覆盖的 Screen 层栈顶
        if (UBaseScreen* ScreenTop = GetTopScreen(EUILayer::Screen))
        {
            ScreenTop->OnRevealed();
        }
    }

    // 同步输入模式
    UpdateInputMode();
}

// ============================================
// 查询
// ============================================

UBaseScreen* UUIManager::GetTopScreen(EUILayer Layer) const
{
    const TArray<UBaseScreen*>* Stack = LayerStacks.Find(Layer);
    if (Stack && Stack->Num() > 0)
    {
        return Stack->Last();
    }
    return nullptr;
}

UBaseScreen* UUIManager::GetTopmostScreen() const
{
    if (UBaseScreen* Overlay = GetTopScreen(EUILayer::Overlay))
    {
        return Overlay;
    }
    if (UBaseScreen* Popup = GetTopScreen(EUILayer::Popup))
    {
        return Popup;
    }
    return GetTopScreen(EUILayer::Screen);
}

int32 UUIManager::GetScreenStackDepth(EUILayer Layer) const
{
    const TArray<UBaseScreen*>* Stack = LayerStacks.Find(Layer);
    return Stack ? Stack->Num() : 0;
}

// ============================================
// 创建 / 输入模式
// ============================================

UBaseScreen* UUIManager::CreateAndAddScreen(TSubclassOf<UBaseScreen> ScreenClass, EUILayer Layer)
{
    if (!GetWorld() || !ScreenClass)
    {
        return nullptr;
    }

    // 纯服务器没有本地玩家，不创建 Widget
    if (!GetWorld()->GetFirstPlayerController())
    {
        UE_LOG(LogTemp, Verbose, TEXT("[UIManager] Skip UI creation on server (no local player controller)"));
        return nullptr;
    }

    UBaseScreen* Screen = CreateWidget<UBaseScreen>(GetWorld(), ScreenClass);
    if (!Screen)
    {
        UE_LOG(LogTemp, Error, TEXT("[UIManager] Failed to create widget from class"));
        return nullptr;
    }

    // 注入 UIManager 引用
    Screen->OwnerUIManager = this;

    // 挂到视口指定 ZOrder
    Screen->AddToViewport(GetLayerBaseZOrder(Layer));

    return Screen;
}

int32 UUIManager::GetLayerBaseZOrder(EUILayer Layer)
{
    return static_cast<int32>(Layer);
}

void UUIManager::SetInputModeUI(UBaseScreen* FocusScreen)
{
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        FInputModeUIOnly Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);

        // 聚焦新页面，让 ESC / 手柄B 能路由到 OnBack
        if (FocusScreen && FocusScreen->GetCachedWidget().IsValid())
        {
            Mode.SetWidgetToFocus(FocusScreen->GetCachedWidget());
        }

        PC->SetInputMode(Mode);
        PC->SetShowMouseCursor(true);
    }
}

void UUIManager::SetInputModeGame()
{
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        FInputModeGameOnly Mode;
        PC->SetInputMode(Mode);
        PC->SetShowMouseCursor(false);
    }
}

void UUIManager::UpdateInputMode()
{
    if (UBaseScreen* Topmost = GetTopmostScreen())
    {
        SetInputModeUI(Topmost);
    }
    else
    {
        SetInputModeGame();
    }
}