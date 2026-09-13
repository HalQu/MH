#include "UI/Core/UIManager.h"

#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Core/BaseScreen.h"

namespace
{
    EUIScreenInputMode MergeInputMode(EUIScreenInputMode Current, EUIScreenInputMode Candidate)
    {
        if (Current == EUIScreenInputMode::UIOnly || Candidate == EUIScreenInputMode::UIOnly)
        {
            return EUIScreenInputMode::UIOnly;
        }

        if (Current == EUIScreenInputMode::GameAndUI || Candidate == EUIScreenInputMode::GameAndUI)
        {
            return EUIScreenInputMode::GameAndUI;
        }

        return EUIScreenInputMode::GameOnly;
    }
}

UUIManager::UUIManager()
    : FTickableGameObject(ETickableTickType::Never)
{
}

void UUIManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    LayerStacks.Empty();
    PersistentScreens.Empty();
    PendingRemovals.Empty();
    ActiveStackScreen.Reset();
    bIsShuttingDown = false;
    bRefreshDataContextNextTick = false;

    SetTickableTickType(ETickableTickType::Conditional);
    BindToPlayerController(GetOwningPlayerController());

    UE_LOG(LogTemp, Log, TEXT("[UIManager] Initialized for LocalPlayer=%s"),
        GetLocalPlayer() ? *GetLocalPlayer()->GetName() : TEXT("null"));
}

void UUIManager::Deinitialize()
{
    bIsShuttingDown = true;
    bRefreshDataContextNextTick = false;
    SetTickableTickType(ETickableTickType::Never);

    CloseAllScreens(true);
    UnbindFromPlayerController();

    Super::Deinitialize();

    UE_LOG(LogTemp, Log, TEXT("[UIManager] Deinitialized"));
}

void UUIManager::PlayerControllerChanged(APlayerController* NewPlayerController)
{
    Super::PlayerControllerChanged(NewPlayerController);

    // 无缝切图或重连时 LocalPlayer 会换控制器。旧的 UMG 属于旧世界，必须立即销毁。
    CloseAllScreens(true);
    UnbindFromPlayerController();
    BindToPlayerController(NewPlayerController);
}

UUIManager* UUIManager::GetUIManager(const UObject* WorldContextObject, int32 PlayerIndex)
{
    if (!WorldContextObject)
    {
        return nullptr;
    }

    if (const APlayerController* PlayerController = Cast<APlayerController>(WorldContextObject))
    {
        if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
        {
            return LocalPlayer->GetSubsystem<UUIManager>();
        }
    }

    if (const ULocalPlayer* LocalPlayer = Cast<ULocalPlayer>(WorldContextObject))
    {
        return LocalPlayer->GetSubsystem<UUIManager>();
    }

    if (const UUserWidget* Widget = Cast<UUserWidget>(WorldContextObject))
    {
        if (ULocalPlayer* LocalPlayer = Widget->GetOwningLocalPlayer())
        {
            return LocalPlayer->GetSubsystem<UUIManager>();
        }
    }

    if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(WorldContextObject, PlayerIndex))
    {
        if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
        {
            return LocalPlayer->GetSubsystem<UUIManager>();
        }
    }

    return nullptr;
}

UBaseScreen* UUIManager::OpenPersistentScreenForLocalPlayer(
    APlayerController* PlayerController,
    FName ScreenID,
    TSubclassOf<UBaseScreen> ScreenClass,
    EUIScreenInputMode InputMode)
{
    if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
    {
        return nullptr;
    }

    UUIManager* UIManager = GetUIManager(PlayerController);
    return UIManager
        ? UIManager->OpenPersistentScreen(ScreenID, ScreenClass, InputMode)
        : nullptr;
}

APlayerController* UUIManager::GetOwningPlayerController() const
{
    ULocalPlayer* LocalPlayer = GetLocalPlayer();
    return LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
}

// ============================================
// 常驻页面
// ============================================

UBaseScreen* UUIManager::OpenPersistentScreen(
    FName ScreenID,
    TSubclassOf<UBaseScreen> ScreenClass,
    EUIScreenInputMode InputMode)
{
    if (ScreenID.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("[UIManager] OpenPersistentScreen called with NAME_None"));
        return nullptr;
    }

    if (UBaseScreen** Found = PersistentScreens.Find(ScreenID))
    {
        if (IsValid(*Found))
        {
            (*Found)->SetInputModePolicy(InputMode);
            UpdateInputMode();
            return *Found;
        }

        PersistentScreens.Remove(ScreenID);
    }

    UBaseScreen* Screen = CreateAndAddScreen(ScreenClass, EUILayer::HUD);
    if (!Screen)
    {
        return nullptr;
    }

    Screen->SetInputModePolicy(InputMode);
    Screen->OnOpen(nullptr);
    PersistentScreens.Add(ScreenID, Screen);

    UpdateInputMode();

    UE_LOG(LogTemp, Log, TEXT("[UIManager] Opened persistent screen '%s'"), *ScreenID.ToString());
    return Screen;
}

void UUIManager::ClosePersistentScreen(FName ScreenID)
{
    UBaseScreen* Screen = nullptr;
    PersistentScreens.RemoveAndCopyValue(ScreenID, Screen);

    if (IsValid(Screen))
    {
        CloseScreen(Screen, false);
        UpdateInputMode();

        UE_LOG(LogTemp, Log, TEXT("[UIManager] Closed persistent screen '%s'"), *ScreenID.ToString());
    }
}

UBaseScreen* UUIManager::GetPersistentScreen(FName ScreenID) const
{
    UBaseScreen* Screen = PersistentScreens.FindRef(ScreenID);
    return IsValid(Screen) ? Screen : nullptr;
}

// ============================================
// 页面栈
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

void UUIManager::CloseAllScreens(bool bImmediate)
{
    TSet<UBaseScreen*> ScreensToClose;

    for (auto& Pair : LayerStacks)
    {
        for (UBaseScreen* Screen : Pair.Value)
        {
            if (IsValid(Screen))
            {
                ScreensToClose.Add(Screen);
            }
        }
    }
    LayerStacks.Empty();
    ActiveStackScreen.Reset();

    for (auto& Pair : PersistentScreens)
    {
        if (IsValid(Pair.Value))
        {
            ScreensToClose.Add(Pair.Value);
        }
    }
    PersistentScreens.Empty();

    for (const FPendingRemoval& Removal : PendingRemovals)
    {
        if (bImmediate)
        {
            ApplyRemoveFromParent(Removal.Screen.Get());
        }
    }

    // 非立即关闭时，已经进入关闭动画的页面仍需由延迟队列完成卸载。
    if (bImmediate)
    {
        PendingRemovals.Empty();
    }

    for (UBaseScreen* Screen : ScreensToClose)
    {
        CloseScreen(Screen, bImmediate);
    }

    UpdateInputMode();
}

void UUIManager::RefreshAllScreensDataContext()
{
    TSet<UBaseScreen*> Screens;

    for (const auto& Pair : LayerStacks)
    {
        for (UBaseScreen* Screen : Pair.Value)
        {
            if (IsValid(Screen))
            {
                Screens.Add(Screen);
            }
        }
    }

    for (const auto& Pair : PersistentScreens)
    {
        if (IsValid(Pair.Value))
        {
            Screens.Add(Pair.Value);
        }
    }

    for (UBaseScreen* Screen : Screens)
    {
        Screen->RefreshDataContext();
    }
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
// 内部实现
// ============================================

UBaseScreen* UUIManager::PushToLayer(EUILayer Layer, TSubclassOf<UBaseScreen> ScreenClass, UObject* Param)
{
    if (!ScreenClass)
    {
        UE_LOG(LogTemp, Error, TEXT("[UIManager] PushToLayer(%d) with null class"), static_cast<int32>(Layer));
        return nullptr;
    }

    UBaseScreen* NewScreen = CreateAndAddScreen(ScreenClass, Layer);
    if (!NewScreen)
    {
        return nullptr;
    }

    NewScreen->OnOpen(Param);

    TArray<UBaseScreen*>& Stack = LayerStacks.FindOrAdd(Layer);
    Stack.Add(NewScreen);

    ApplyStackState();

    // 如果打开的是被更高层遮住的页面，它必须在 OnOpen 后立即进入 Covered 状态。
    if (GetActiveStackScreen() != NewScreen && NewScreen->IsTopmost())
    {
        NewScreen->OnCovered();
    }

    UE_LOG(LogTemp, Log, TEXT("[UIManager] Pushed screen to layer %d. Stack depth: %d"),
        static_cast<int32>(Layer), Stack.Num());

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
    int32 TargetIndex = Stack.Num() - 1;
    if (TargetScreen)
    {
        TargetIndex = Stack.IndexOfByKey(TargetScreen);
        if (TargetIndex == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("[UIManager] PopFromLayer: target not in layer %d"), static_cast<int32>(Layer));
            return;
        }
    }

    const int32 CountToClose = Stack.Num() - TargetIndex;
    TArray<UBaseScreen*> ScreensToClose;
    ScreensToClose.Reserve(CountToClose);
    for (int32 Index = TargetIndex; Index < Stack.Num(); ++Index)
    {
        ScreensToClose.Add(Stack[Index]);
    }

    // 先更新栈，再触发页面回调，允许 OnClose 中安全地继续操作 UI。
    Stack.RemoveAt(TargetIndex, CountToClose);

    for (UBaseScreen* Screen : ScreensToClose)
    {
        CloseScreen(Screen, false);
    }

    ApplyStackState();

    UE_LOG(LogTemp, Log, TEXT("[UIManager] Popped %d screen(s) from layer %d. Remaining: %d"),
        CountToClose, static_cast<int32>(Layer), Stack.Num());
}

UBaseScreen* UUIManager::CreateAndAddScreen(TSubclassOf<UBaseScreen> ScreenClass, EUILayer Layer)
{
    if (bIsShuttingDown || !ScreenClass)
    {
        return nullptr;
    }

    APlayerController* PlayerController = GetOwningPlayerController();
    if (!PlayerController || !PlayerController->GetLocalPlayer())
    {
        UE_LOG(LogTemp, Warning, TEXT("[UIManager] Cannot create screen without an owning local PlayerController"));
        return nullptr;
    }

    UBaseScreen* Screen = CreateWidget<UBaseScreen>(PlayerController, ScreenClass);
    if (!Screen)
    {
        UE_LOG(LogTemp, Error, TEXT("[UIManager] Failed to create widget from class '%s'"), *ScreenClass->GetName());
        return nullptr;
    }

    // 根节点兼作返回键接收器；UMG 会在 OnFocusReceived 时转发到 DesiredFocusWidget。
    Screen->SetIsFocusable(true);

    OwnedScreens.Add(Screen);
    Screen->AddToViewport(GetLayerBaseZOrder(Layer));
    return Screen;
}

void UUIManager::ApplyStackState()
{
    UBaseScreen* PreviousActive = ActiveStackScreen.Get();
    UBaseScreen* NewActive = GetActiveStackScreen();

    if (PreviousActive == NewActive)
    {
        UpdateInputMode();
        return;
    }

    if (IsValid(PreviousActive))
    {
        PreviousActive->OnCovered();
    }

    ActiveStackScreen = NewActive;

    if (NewActive)
    {
        NewActive->OnRevealed();
    }

    UpdateInputMode();
}

UBaseScreen* UUIManager::GetActiveStackScreen() const
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

EUIScreenInputMode UUIManager::GetDesiredInputMode() const
{
    EUIScreenInputMode DesiredMode = EUIScreenInputMode::GameOnly;
    for (const auto& Pair : PersistentScreens)
    {
        if (!IsValid(Pair.Value))
        {
            continue;
        }

        DesiredMode = MergeInputMode(DesiredMode, Pair.Value->GetInputModePolicy());
    }

    if (UBaseScreen* Active = GetActiveStackScreen())
    {
        DesiredMode = MergeInputMode(DesiredMode, Active->GetInputModePolicy());
    }

    return DesiredMode;
}

UBaseScreen* UUIManager::GetInputFocusScreen() const
{
    UBaseScreen* Active = GetActiveStackScreen();
    if (Active && Active->GetInputModePolicy() == EUIScreenInputMode::UIOnly)
    {
        return Active;
    }

    for (const auto& Pair : PersistentScreens)
    {
        if (!IsValid(Pair.Value))
        {
            continue;
        }

        if (Pair.Value->GetInputModePolicy() == EUIScreenInputMode::UIOnly)
        {
            return Pair.Value;
        }
    }

    if (Active && Active->GetInputModePolicy() == EUIScreenInputMode::GameAndUI)
    {
        return Active;
    }

    for (const auto& Pair : PersistentScreens)
    {
        if (IsValid(Pair.Value) && Pair.Value->GetInputModePolicy() == EUIScreenInputMode::GameAndUI)
        {
            return Pair.Value;
        }
    }

    return nullptr;
}

void UUIManager::UpdateInputMode()
{
    SetInputMode(GetDesiredInputMode(), GetInputFocusScreen());
}

void UUIManager::SetInputMode(EUIScreenInputMode InputMode, UBaseScreen* FocusScreen)
{
    APlayerController* PlayerController = GetOwningPlayerController();
    if (bIsShuttingDown || !PlayerController)
    {
        return;
    }

    if (InputMode == EUIScreenInputMode::UIOnly)
    {
        FInputModeUIOnly Mode;
        const TSharedPtr<SWidget> FocusWidget = FocusScreen ? FocusScreen->GetCachedWidget() : nullptr;
        if (FocusWidget.IsValid() && FocusWidget->SupportsKeyboardFocus())
        {
            Mode.SetWidgetToFocus(FocusWidget);
        }
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
        PlayerController->SetInputMode(Mode);
        PlayerController->SetShowMouseCursor(true);
        return;
    }

    if (InputMode == EUIScreenInputMode::GameAndUI)
    {
        FInputModeGameAndUI Mode;
        const TSharedPtr<SWidget> FocusWidget = FocusScreen ? FocusScreen->GetCachedWidget() : nullptr;
        if (FocusWidget.IsValid() && FocusWidget->SupportsKeyboardFocus())
        {
            Mode.SetWidgetToFocus(FocusWidget);
        }
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Mode.SetHideCursorDuringCapture(false);
        PlayerController->SetInputMode(Mode);
        PlayerController->SetShowMouseCursor(true);
        return;
    }

    FInputModeGameOnly Mode;
    PlayerController->SetInputMode(Mode);
    PlayerController->SetShowMouseCursor(false);
}

void UUIManager::CloseScreen(UBaseScreen* Screen, bool bImmediate)
{
    if (!IsValid(Screen))
    {
        return;
    }

    if (Screen->IsScreenOpen())
    {
        Screen->OnClose();
    }

    if (bImmediate)
    {
        ApplyRemoveFromParent(Screen);
    }
    else
    {
        ScheduleRemoval(Screen);
    }
}

void UUIManager::ScheduleRemoval(UBaseScreen* Screen)
{
    if (!IsValid(Screen))
    {
        return;
    }

    const bool bAlreadyScheduled = PendingRemovals.ContainsByPredicate(
        [Screen](const FPendingRemoval& Removal)
        {
            return Removal.Screen.Get() == Screen;
        });

    if (bAlreadyScheduled)
    {
        return;
    }

    const float Delay = Screen->GetCloseAnimDuration();
    if (Delay <= 0.f)
    {
        ApplyRemoveFromParent(Screen);
        return;
    }

    PendingRemovals.Add({ Screen, Delay });
}

void UUIManager::ApplyRemoveFromParent(UBaseScreen* Screen)
{
    if (IsValid(Screen))
    {
        Screen->RemoveFromParent();
    }

    OwnedScreens.Remove(Screen);
}

// 关闭动画可能跨帧，这里只做倒计时；归零后再从视口和 OwnedScreens 中真正移除。
void UUIManager::Tick(float DeltaTime)
{
    for (int32 Index = PendingRemovals.Num() - 1; Index >= 0; --Index)
    {
        FPendingRemoval& Removal = PendingRemovals[Index];
        Removal.RemainingTime -= DeltaTime;

        if (Removal.RemainingTime <= 0.f)
        {
            ApplyRemoveFromParent(Removal.Screen.Get());
            PendingRemovals.RemoveAt(Index);
        }
    }
    if (bRefreshDataContextNextTick)
    {
        bRefreshDataContextNextTick = false;
        RefreshAllScreensDataContext();
    }
}

bool UUIManager::IsTickable() const
{
    return !IsTemplate() && (PendingRemovals.Num() > 0 || bRefreshDataContextNextTick);
}

ETickableTickType UUIManager::GetTickableTickType() const
{
    return ETickableTickType::Conditional;
}

UWorld* UUIManager::GetTickableGameObjectWorld() const
{
    const ULocalPlayer* LocalPlayer = GetLocalPlayer();
    return LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
}

// ============================================
// 查询
// ============================================

UBaseScreen* UUIManager::GetTopScreen(EUILayer Layer) const
{
    const TArray<UBaseScreen*>* Stack = LayerStacks.Find(Layer);
    if (!Stack || Stack->Num() == 0)
    {
        return nullptr;
    }

    for (int32 Index = Stack->Num() - 1; Index >= 0; --Index)
    {
        if (UBaseScreen* Screen = (*Stack)[Index]; IsValid(Screen))
        {
            return Screen;
        }
    }

    return nullptr;
}

UBaseScreen* UUIManager::GetTopmostScreen() const
{
    return GetActiveStackScreen();
}

int32 UUIManager::GetScreenStackDepth(EUILayer Layer) const
{
    const TArray<UBaseScreen*>* Stack = LayerStacks.Find(Layer);
    return Stack ? Stack->Num() : 0;
}

int32 UUIManager::GetLayerBaseZOrder(EUILayer Layer)
{
    return static_cast<int32>(Layer);
}

// ============================================
// PlayerController / Pawn 绑定
// ============================================

void UUIManager::BindToPlayerController(APlayerController* PlayerController)
{
    if (BoundPlayerController.Get() == PlayerController)
    {
        RequestDataContextRefresh();
        UpdateInputMode();
        return;
    }

    UnbindFromPlayerController();
    BoundPlayerController = PlayerController;

    if (PlayerController)
    {
        PlayerController->OnPossessedPawnChanged.AddDynamic(this, &UUIManager::HandlePossessedPawnChanged);
    }

    RequestDataContextRefresh();
    UpdateInputMode();
}

void UUIManager::UnbindFromPlayerController()
{
    if (APlayerController* PlayerController = BoundPlayerController.Get())
    {
        PlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &UUIManager::HandlePossessedPawnChanged);
    }

    BoundPlayerController.Reset();
}

void UUIManager::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
    RequestDataContextRefresh();
}

void UUIManager::RequestDataContextRefresh()
{
    RefreshAllScreensDataContext();
    bRefreshDataContextNextTick = true;
}

void UUIManager::NotifyScreenDestroyed(UBaseScreen* Screen)
{
    if (!Screen)
    {
        return;
    }

    bool bStackChanged = false;
    for (auto Iterator = LayerStacks.CreateIterator(); Iterator; ++Iterator)
    {
        TArray<UBaseScreen*>& Stack = Iterator.Value();
        bStackChanged |= Stack.RemoveSingle(Screen) > 0;

        if (Stack.Num() == 0)
        {
            Iterator.RemoveCurrent();
        }
    }

    for (auto Iterator = PersistentScreens.CreateIterator(); Iterator; ++Iterator)
    {
        if (Iterator.Value() == Screen)
        {
            Iterator.RemoveCurrent();
        }
    }

    PendingRemovals.RemoveAll(
        [Screen](const FPendingRemoval& Removal)
        {
            return Removal.Screen.Get() == Screen;
        });
    OwnedScreens.Remove(Screen);

    if (ActiveStackScreen.Get() == Screen)
    {
        ActiveStackScreen.Reset();
    }

    if (bStackChanged)
    {
        ApplyStackState();
    }
    else
    {
        UpdateInputMode();
    }
}
