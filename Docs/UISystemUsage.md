# MH UI 系统使用手册

适用版本：Unreal Engine 5.8

本文档对应以下核心文件：

- `Source/MH/Public/UI/Core/UIManager.h`
- `Source/MH/Private/UI/Core/UIManager.cpp`
- `Source/MH/Public/UI/Core/BaseScreen.h`
- `Source/MH/Private/UI/Core/BaseScreen.cpp`
- `Source/MH/Public/UI/Core/BaseViewModel.h`
- `Source/MH/Private/UI/Core/BaseViewModel.cpp`
- `Source/MH/Public/UI/Core/Observable.h`
- `Source/MH/Public/UI/Core/UIEventBus.h`
- `Source/MH/Private/UI/Core/UIEventBus.cpp`
- `Source/MH/Public/UI/Core/UIScreenTypes.h`

## 1. 这套 UI 系统的职责

这套系统负责：

1. 为每个本地玩家维护独立的 UI 页面栈。
2. 创建、显示、覆盖、恢复和关闭 Widget。
3. 根据页面状态自动切换输入模式。
4. 管理 ESC 和手柄 B 的返回路由。
5. 在 PlayerController、Pawn 或数据源对象发生切换时，让页面重新解析并刷新数据。
6. 统一管理 ViewModel 的生命周期。
7. 提供一套可筛选载荷类型的 UI 事件总线。
8. 在切图、重连和 PlayerController 替换时清理旧 UI。

它不会替代业务数据本身的复制回调和增量更新。

这套系统不负责：

1. 页面的具体视觉设计和动画内容。
2. 游戏业务数据的生产和复制。
3. Enhanced Input 映射上下文的启停。
4. 玩家是否暂停、是否允许打开某个菜单等业务规则。

## 2. 核心类型

| 类型 | 基类 | 作用 |
| --- | --- | --- |
| `UUIManager` | `ULocalPlayerSubsystem` | 每个本地玩家一个实例，管理页面、输入和焦点 |
| `UBaseScreen` | `UUserWidget` | 所有页面的基类，连接 View 和 ViewModel |
| `UBaseViewModel` | `UObject` | 业务数据和 UI 输出之间的适配层 |
| `TBindedValue<T>` | C++ 模板 | ViewModel 的可订阅输出值 |
| `UUIEventBus` | `UGameInstanceSubsystem` | GameInstance 级 UI 事件总线 |
| `EUILayer` | `UENUM` | 页面层级 |
| `EUIScreenInputMode` | `UENUM` | 页面输入策略 |

### 2.1 页面层级

```cpp
enum class EUILayer : uint8
{
    World = 0,
    HUD = 10,
    Screen = 20,
    Popup = 30,
    Overlay = 40
};
```

数值同时作为基础 ZOrder，数值越大显示越靠前。

- `World`：为世界空间 UI 预留，目前没有专用管理接口。
- `HUD`：常驻 HUD、主菜单等，不进入页面栈。
- `Screen`：普通全屏页面。
- `Popup`：确认框、对话框等弹窗。
- `Overlay`：暂停菜单、加载界面等最高层页面。

`Screen`、`Popup`、`Overlay` 各自拥有独立栈。

当前激活页面按照以下优先级选取：

```text
Overlay > Popup > Screen
```

### 2.2 输入模式

```cpp
enum class EUIScreenInputMode : uint8
{
    GameOnly,
    UIOnly,
    GameAndUI
};
```

- `GameOnly`
  - 鼠标隐藏。
  - 游戏输入正常。
  - 适合游戏中的常驻 HUD。
- `UIOnly`
  - 鼠标显示。
  - 输入交给 UI。
  - 适合主菜单、暂停菜单、确认框。
- `GameAndUI`
  - 鼠标显示。
  - UI 和游戏都可以接收输入。
  - 适合需要同时操作角色和界面的场景。

多个页面同时存在时，管理器会合并所有常驻页面和当前激活栈顶页面的策略，强度顺序为：

```text
UIOnly > GameAndUI > GameOnly
```

例如常驻主菜单是 `UIOnly`，即使之后压入一个默认 `GameOnly` 的页面，最终仍然保持 `UIOnly`，避免主菜单失去输入焦点。

## 3. 五分钟快速接入

### 3.1 已有集成方式

项目当前已经提供以下入口：

```cpp
// 本机直接打开常驻页面
PC->OpenPersistentScreenLocal(ScreenID, ScreenClass, InputMode);

// 服务器通知指定客户端打开常驻页面
MHPC->Client_OpenPersistentScreen(ScreenID, ScreenClass, InputMode);

// 不关心调用位置，只在“本机控制器”上打开
UUIManager::OpenPersistentScreenForLocalPlayer(
    PC,
    ScreenID,
    ScreenClass,
    InputMode);
```

GameMode 中的标准模式：

```cpp
if (PlayerController->IsLocalController())
{
    UUIManager::OpenPersistentScreenForLocalPlayer(
        PlayerController,
        TEXT("HUD"),
        HuntingHUDClass,
        EUIScreenInputMode::GameOnly);
}
else if (AMHPlayerController* MHPC = Cast<AMHPlayerController>(PlayerController))
{
    MHPC->Client_OpenPersistentScreen(
        TEXT("HUD"),
        HuntingHUDClass,
        EUIScreenInputMode::GameOnly);
}
```

不要直接在服务器上对远程客户端调用 `OpenPersistentScreenForLocalPlayer`。该函数在远程控制器上会返回 `nullptr`。

### 3.2 接入检查表

1. GameMode 或 GameMode 蓝图里配置好页面类。
2. PlayerController 使用 `AMHPlayerController` 或其子类。
3. 页面继承 `UBaseScreen` 或基于它创建的 Widget Blueprint。
4. 页面设置好 `InputModePolicy` 和 `CloseAnimDuration`。
5. C++ ViewModel 继承 `UBaseViewModel`。
6. 页面 Class Defaults 中设置 `ViewModelClass`。
7. 服务器打开客户端 UI 时必须走 Client RPC。
8. 切图后不要复用旧 Widget 指针，应通过 GameMode 重新打开。

## 4. C++ 页面开发

### 4.1 创建页面类

头文件：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "UI/Core/BaseScreen.h"
#include "MHHealthHUD.generated.h"

class UProgressBar;

UCLASS()
class MH_API UMHHealthHUD : public UBaseScreen
{
    GENERATED_BODY()

public:
    UMHHealthHUD();

    virtual void OnOpen(UObject* Param) override;

protected:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UProgressBar> HealthBar;

    void SetHealthPercent(float NewPercent);
};
```

源文件：

```cpp
#include "UI/Screen/MHHealthHUD.h"

#include "Components/ProgressBar.h"
#include "UI/VM/VMHealthHUD.h"

UMHHealthHUD::UMHHealthHUD()
{
    ViewModelClass = UVMHealthHUD::StaticClass();
    InputModePolicy = EUIScreenInputMode::GameOnly;
    CloseAnimDuration = 0.25f;
}

void UMHHealthHUD::OnOpen(UObject* Param)
{
    Super::OnOpen(Param);

    UVMHealthHUD* VM = Cast<UVMHealthHUD>(GetViewModel());
    BIND_VM_PROPERTY(VM, HealthPercent, &UMHHealthHUD::SetHealthPercent);
}

void UMHHealthHUD::SetHealthPercent(float NewPercent)
{
    if (HealthBar)
    {
        HealthBar->SetPercent(FMath::Clamp(NewPercent, 0.f, 1.f));
    }
}
```

要点：

- C++ 页面重写 `OnOpen` 时必须调用 `Super::OnOpen(Param)`。
- `ViewModelClass` 会在 `OnOpen` 中创建 ViewModel。
- `BIND_VM_PROPERTY` 会先解除该 Widget 的旧绑定，再建立新绑定并立即广播当前值。
- `UPROPERTY(meta = (BindWidget))` 要求 Widget Blueprint 中存在同名控件。
- `CloseAnimDuration` 应该不小于关闭动画的实际时长。

### 4.2 页面生命周期

C++ 回调：

| 回调 | 调用时机 | 建议用途 |
| --- | --- | --- |
| `OnOpen` | 页面首次打开时 | 获取 ViewModel、绑定输出属性 |
| `OnCovered` | 被更高层页面覆盖时 | 暂停高开销逻辑 |
| `OnRevealed` | 上层页面关闭后重新显示时 | 恢复逻辑、重新刷新 |
| `OnClose` | 页面开始关闭时 | 最后一次清理页面自身资源 |
| `OnBack_Implementation` | 按下 ESC 或手柄 B 时 | 默认关闭当前页面 |
| `PlayOpenAnimation_Implementation` | 页面打开状态建立后 | 播放打开动画 |
| `PlayCloseAnimation_Implementation` | 页面关闭状态建立后 | 播放关闭动画 |

打开顺序：

```text
创建 Widget
-> AddToViewport
-> OnOpen
-> 创建并初始化 ViewModel
-> ViewModel::OnActivated
-> ViewModel::RefreshAll
-> PlayOpenAnimation
-> BP_OnOpened
-> 加入页面栈
-> 应用输入模式和焦点
```

被覆盖：

```text
OnCovered
-> 页面变为不可交互
-> ViewModel::OnDeactivated
-> BP_OnCovered
```

重新显示：

```text
OnRevealed
-> 页面恢复可见和可交互
-> ViewModel::OnActivated
-> ViewModel::RefreshAll
-> BP_OnRevealed
```

关闭：

```text
OnClose
-> 页面立即变为不可交互
-> BP_OnClosed
-> ViewModel::OnDestroy
-> PlayCloseAnimation
-> 等待 CloseAnimDuration
-> RemoveFromParent
```

注意：

- `OnClose` 是立即调用，不是动画结束后才调用。
- `BP_OnClosed` 调用时 ViewModel 仍然存在。
- `PlayCloseAnimation` 调用时 ViewModel 已经被销毁，不要再通过 ViewModel 取数据。
- 管理器会保证 `OnClose` 对同一页面只执行一次。

### 4.3 C++ 覆盖返回键

`OnBack` 是 `BlueprintNativeEvent`，C++ 子类应覆盖 `_Implementation`：

```cpp
void UMyScreen::OnBack_Implementation()
{
    if (bHasUnsavedChanges)
    {
        ShowDiscardPopup();
        return;
    }

    Super::OnBack_Implementation();
}
```

如果不调用 `Super::OnBack_Implementation()`，就不会自动执行 `UUIManager::HandleBack()`。

## 5. Widget Blueprint 页面开发

### 5.1 创建 Widget Blueprint

1. 新建 Widget Blueprint。
2. Parent Class 选择 `UBaseScreen` 或自定义的 C++ 页面类。
3. Class Defaults 中设置：
   - `ViewModel Class`
   - `Input Mode Policy`
   - `Close Anim Duration`
4. 添加控件并设置与 C++ `BindWidget` 属性同名的控件名称。
5. 实现需要的蓝图生命周期事件。

纯蓝图页面不替换 `OnOpen`，而是实现以下事件：

| 蓝图事件 | 用途 |
| --- | --- |
| `On Screen Opened` | 初始化界面、绑定事件 |
| `On Screen Covered` | 页面被覆盖时处理 |
| `On Screen Revealed` | 页面重新显示时刷新 |
| `On Screen Closed` | 最后一次处理页面数据 |
| `On Data Context Changed` | 数据源对象、Pawn 或 Controller 切换后刷新 |
| `On Back` | 自定义返回逻辑 |
| `Play Open Animation` | 播放打开动画 |
| `Play Close Animation` | 播放关闭动画 |

`On Screen Opened` 会收到打开时传入的参数，参数类型是 `UObject*`。

### 5.2 蓝图如何绑定 ViewModel

`TBindedValue<T>` 是 C++ 模板，不能直接暴露给蓝图。当前 `UVMHuntingBaseHUD` 使用模式是：

- `GetHealthPercentValue()`：`BlueprintPure` 获取当前值。
- `OnHealthPercentChanged`：`BlueprintAssignable` 动态多播委托。
- C++ 页面内部继续使用 `TBindedValue`。

推荐的蓝图绑定方式：

```text
Event On Screen Opened
-> Get ViewModel
-> Cast To VMHuntingBaseHUD
-> Bind Event to On Health Percent Changed
-> Get Health Percent Value
-> Set Health Bar Percent

Custom Event On Health Percent Changed (float Percent)
-> Set Health Bar Percent
```

新增蓝图可访问的 VM 输出时，建议同时提供：

```cpp
UFUNCTION(BlueprintPure)
float GetValue() const;

UPROPERTY(BlueprintAssignable)
FMyValueChanged OnValueChanged;
```

### 5.3 蓝图覆盖返回事件

如果页面的 `On Back` 事件完全接管返回逻辑，可以不调用父实现。

如果执行完自定义逻辑后仍需默认关闭当前页面，需要调用父类的 `On Back`：

```text
Event On Back
-> 自定义逻辑
-> Parent: On Back
```

## 6. ViewModel 开发

### 6.1 创建 ViewModel

当前 `UBaseViewModel` 是 C++ 基类，不作为蓝图可继承 VM 使用。

```cpp
#pragma once

#include "CoreMinimal.h"
#include "UI/Core/BaseViewModel.h"
#include "UI/Core/Observable.h"
#include "VMHealthHUD.generated.h"

class UHealthComponent;

UCLASS()
class MH_API UVMHealthHUD : public UBaseViewModel
{
    GENERATED_BODY()

public:
    TBindedValue<float> HealthPercent;

    virtual void OnActivated() override;
    virtual void OnDeactivated() override;
    virtual void OnDestroy() override;
    virtual void RefreshAll() override;

private:
    UHealthComponent* ResolveHealthComponent() const;
    void BindHealthComponent(UHealthComponent* HealthComponent);
    void UnbindHealthComponent();

    UFUNCTION()
    void HandleHealthChanged(float NewHealth, float MaxHealth);

    TWeakObjectPtr<UHealthComponent> BoundHealthComponent;
};
```

实现原则：

- `OnActivated`、`OnDeactivated`、`OnDestroy` 中调用对应的 `Super`。
- `OnActivated` 中绑定 Model 或 Component 委托。
- `OnDeactivated` 中解除绑定。
- `OnDestroy` 中调用 `Super::OnDestroy()` 完成最终清理。
- `RefreshAll` 必须允许数据源为空。
- Model 引用使用 `TWeakObjectPtr`，不要强引用 Widget 或世界对象。

### 6.2 数据源

默认数据源由页面从 Owning PlayerController 获取 PlayerState：

```cpp
UObject* UBaseScreen::GetDataSource() const;
```

如果页面需要其他数据源，可以重写：

```cpp
UObject* UMyScreen::GetDataSource() const
{
    if (AMyPlayerController* PC = Cast<AMyPlayerController>(GetOwningPlayer()))
    {
        return PC->GetMyUIModel();
    }

    return Super::GetDataSource();
}
```

数据源规则：

- `DataSource` 和 `OuterWidget` 都是弱引用。
- ViewModel 不拥有数据源生命周期。
- 数据源改变时，管理器会调用 `SetDataSource`。
- 如果 ViewModel 当前激活，`SetDataSource` 会依次执行 `OnDeactivated -> 替换数据源 -> OnActivated`。
- 页面被覆盖时只更新数据源，不执行 `RefreshAll`。
- 页面重新显示时会执行 `OnActivated` 和 `RefreshAll`。

重生和网络复制时，`PlayerState->GetPawn()` 可能暂时为空。HUD 这类页面应使用本地 PlayerController 的 Pawn 作为兜底：

```cpp
APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;

if (!Pawn)
{
    if (const UUserWidget* Widget = Cast<UUserWidget>(OuterWidget.Get()))
    {
        if (APlayerController* PC = Widget->GetOwningPlayer())
        {
            Pawn = PC->GetPawn();
        }
    }
}
```

### 6.3 TBindedValue

```cpp
TBindedValue<float> HealthPercent;
```

写入：

```cpp
HealthPercent.Set(0.75f);
```

订阅：

```cpp
VM->HealthPercent.OnChanged.AddUObject(this, &UMyWidget::SetHealthPercent);
```

立即同步当前值：

```cpp
VM->HealthPercent.Broadcast();
```

页面中推荐使用：

```cpp
BIND_VM_PROPERTY(VM, HealthPercent, &UMyWidget::SetHealthPercent);
```

注意：

- 使用模板类型前需要包含 `UI/Core/Observable.h`。
- `T` 最好支持 `!=` 比较。
- 同一个 Widget 重复绑定时，`BIND_VM_PROPERTY` 会先移除旧绑定。

## 7. 打开和关闭页面

### 7.1 获取 UIManager

C++ 页面内部：

```cpp
UUIManager* UIManager = GetUIManager();
```

任意 C++ 对象：

```cpp
UUIManager* UIManager = UUIManager::GetUIManager(this);
```

指定本地玩家：

```cpp
UUIManager* UIManager = UUIManager::GetUIManager(this, PlayerIndex);
```

蓝图节点：

```text
Get UIManager
```

不要使用：

```cpp
GetGameInstance()->GetSubsystem<UUIManager>();
```

`UUIManager` 现在是 `ULocalPlayerSubsystem`，不是 `GameInstanceSubsystem`。

### 7.2 常驻页面

常驻页面不进入页面栈，适合 HUD 和主菜单。

```cpp
UIManager->OpenPersistentScreen(
    TEXT("HUD"),
    HUDClass,
    EUIScreenInputMode::GameOnly);
```

关闭：

```cpp
UIManager->ClosePersistentScreen(TEXT("HUD"));
```

查询：

```cpp
UBaseScreen* HUD = UIManager->GetPersistentScreen(TEXT("HUD"));
```

规则：

- `ScreenID` 是常驻页面的唯一标识。
- 使用同一个 `ScreenID` 重复打开不会创建第二个 Widget。
- 重复打开时会更新输入模式并返回已有页面。
- `ScreenID` 不能是 `NAME_None`。

### 7.3 普通全屏页面

```cpp
UMyParamObject* Param = NewObject<UMyParamObject>(this);
UIManager->PushScreen(MyScreenClass, Param);
```

关闭栈顶：

```cpp
UIManager->PopScreen();
```

关闭到指定页面，包含目标页面：

```cpp
UIManager->PopToScreen(TargetScreen);
```

`PopToScreen(nullptr)` 等同于关闭当前 Screen 栈顶。

### 7.4 弹窗

```cpp
UIManager->ShowPopup(ConfirmPopupClass, Payload);
UIManager->ClosePopup();
```

弹窗进入独立的 Popup 栈，可以连续叠加。

### 7.5 Overlay

```cpp
UIManager->PushOverlay(PauseMenuClass);
UIManager->PopOverlay();
```

Overlay 的输入优先级和显示层级都高于 Popup 和普通页面。

### 7.6 关闭全部页面

```cpp
UIManager->CloseAllScreens(true);
```

参数：

- `true`：立即移除，不等待关闭动画。
- `false`：执行关闭流程并按 `CloseAnimDuration` 延迟移除。

`CloseAllScreens` 会同时关闭：

- Screen 栈
- Popup 栈
- Overlay 栈
- 所有常驻页面
- 已进入延迟卸载队列的页面

### 7.7 API 总览

| API | 作用 |
| --- | --- |
| `GetUIManager` | 获取本地玩家的 UI 管理器 |
| `GetOwningPlayerController` | 获取管理器所属本地控制器 |
| `OpenPersistentScreenForLocalPlayer` | 仅在本地控制器上打开常驻页面 |
| `OpenPersistentScreen` | 打开或返回指定 ID 的常驻页面 |
| `ClosePersistentScreen` | 关闭指定 ID 的常驻页面 |
| `GetPersistentScreen` | 查询常驻页面 |
| `PushScreen` | 压入普通全屏页面 |
| `PopScreen` | 关闭普通页面栈顶 |
| `PopToScreen` | 关闭到指定普通页面 |
| `ShowPopup` | 压入弹窗 |
| `ClosePopup` | 关闭弹窗栈顶 |
| `PushOverlay` | 压入 Overlay |
| `PopOverlay` | 关闭 Overlay 栈顶 |
| `CloseAllScreens` | 关闭所有页面 |
| `RefreshAllScreensDataContext` | 手动刷新所有页面的数据源 |
| `HandleBack` | 按 Overlay、Popup、Screen 顺序返回 |
| `GetTopScreen` | 查询指定层栈顶 |
| `GetTopmostScreen` | 查询当前激活的栈顶页面 |
| `GetScreenStackDepth` | 查询指定层页面数量 |

## 8. 输入、焦点和返回键

### 8.1 管理器做了什么

1. Widget 创建后根节点会设置为可聚焦。
2. 根据页面策略调用 `FInputModeUIOnly`、`FInputModeGameAndUI` 或 `FInputModeGameOnly`。
3. `UIOnly` 和 `GameAndUI` 下会显示鼠标。
4. `GameOnly` 下会隐藏鼠标。
5. `UIOnly` 会优先聚焦当前激活页面。
6. ESC 和手柄 B 会进入 `UBaseScreen::NativeOnKeyDown`。

### 8.2 返回键路由

默认路由顺序：

```text
Overlay 栈顶
-> Popup 栈顶
-> Screen 栈顶
```

常驻页面不会被默认返回键关闭。

### 8.3 重要限制

- `GameOnly` 页面通常没有 UI 键盘焦点。
- 如果需要在纯游戏输入模式下处理返回键，建议在 PlayerController 的输入动作中调用：

```cpp
UIManager->HandleBack();
```

- `GameAndUI` 会同时允许 UI 和游戏输入，使用时要确认输入消费关系，避免一次按键同时触发 UI 和角色动作。
- 当前系统统一聚焦页面根 Widget。如果必须在打开后聚焦某个子控件，可以在页面打开后的下一帧调用该控件的 `SetKeyboardFocus`。
- 当前系统不自动启停 Enhanced Input Mapping Context。打开 `UIOnly` 页面后，如果需要禁用角色动作，应由业务层暂停输入或移除对应 Mapping Context。

## 9. 动画

`PlayOpenAnimation` 和 `PlayCloseAnimation` 都是 `BlueprintNativeEvent`。

蓝图实现：

```text
Event Play Open Animation
-> Play Animation (OpenAnim)

Event Play Close Animation
-> Play Animation (CloseAnim)
```

C++ 实现：

```cpp
void UMyScreen::PlayOpenAnimation_Implementation()
{
    PlayAnimation(OpenAnimation);
}

void UMyScreen::PlayCloseAnimation_Implementation()
{
    PlayAnimation(CloseAnimation);
}
```

`CloseAnimDuration`：

- 单位是秒。
- 应匹配关闭动画实际时长。
- 如果为 `0`，页面会立即移除。
- 只是延迟移除时长，不会自动播放动画。

关闭开始时页面已经不可点击，但仍然可见，所以可以正常播放淡出、缩放等动画。

## 10. 事件总线

### 10.1 使用场景

适用于多个 UI 页面互相解耦，例如：

- 血量变化。
- 任务进度变化。
- 货币变化。
- 打开或关闭某类菜单。
- 奖励获得提示。

事件总线位于 GameInstance，生命周期长于关卡。监听者必须主动退订，否则切图后可能继续收到事件。

### 10.2 定义强类型载荷

```cpp
#pragma once

#include "CoreMinimal.h"
#include "UI/Core/UIEventBus.h"
#include "MHUIEvents.generated.h"

USTRUCT(BlueprintType)
struct FMHHealthChangedEvent : public FUIEventPayload
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    float Percent = 0.0f;
};
```

### 10.3 监听和广播

```cpp
// 实现文件额外包含：
// #include "Engine/GameInstance.h"
// #include "Engine/World.h"

namespace MHUIEvents
{
    inline const FName HealthChanged(TEXT("MH.UI.HealthChanged"));
}

// UMyObject 中需要一个 FDelegateHandle HealthHandle 成员。
UUIEventBus* UMyObject::ResolveEventBus() const
{
    UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance
        ? GameInstance->GetSubsystem<UUIEventBus>()
        : nullptr;
}

void UMyObject::Listen()
{
    // UBaseViewModel 可以直接使用 GetBus()。
    UUIEventBus* Bus = ResolveEventBus();

    if (!Bus)
    {
        return;
    }

    HealthHandle = Bus->ListenTyped<FMHHealthChangedEvent>(
        MHUIEvents::HealthChanged,
        FUIEventHandler::CreateUObject(this, &UMyObject::HandleHealthChanged));
}

void UMyObject::HandleHealthChanged(const FUIEventPayload& BasePayload)
{
    const FMHHealthChangedEvent& Event =
        static_cast<const FMHHealthChangedEvent&>(BasePayload);

    UpdateHealthBar(Event.Percent);
}

void UMyObject::BroadcastHealth(float Percent)
{
    FMHHealthChangedEvent Event;
    Event.Percent = Percent;

    // 这里同样通过 GetWorld() -> GameInstance 获取 Bus。
    if (UUIEventBus* Bus = ResolveEventBus())
    {
        Bus->BroadcastTyped(MHUIEvents::HealthChanged, Event);
    }
}
```

`FUIEventHandler` 的统一签名是：

```cpp
void Handler(const FUIEventPayload& Payload);
```

`ListenTyped<T>` 做的是运行时载荷类型筛选，不会改变回调参数类型。

### 10.4 退订

精确退订：

```cpp
Bus->Unlisten(MHUIEvents::HealthChanged, HealthHandle);
```

按对象退订：

```cpp
Bus->UnlistenAll(this);
```

`UBaseViewModel::OnDeactivated` 已经自动调用：

```cpp
Bus->UnlistenAll(this);
```

因此 ViewModel 使用 `CreateUObject(this, ...)` 注册的监听会在覆盖和关闭时自动移除。

Widget、Actor 或普通对象自行注册时需要手动退订。

### 10.5 路由规则

- `BroadcastTyped<T>` 只触发：
  - `ListenTyped<T>` 的监听者。
  - 旧的无类型 `Listen` 监听者。
- 无类型的 `Broadcast(Name, Payload)` 不会触发强类型监听。
- `Broadcast(Name)` 的载荷类型是 `FUIEmptyPayload`。
- 同名事件不允许递归广播。
- 不同事件可以嵌套广播。
- 广播期间新添加的监听者不会在当前这一轮收到事件。
- 广播期间已经被移除的监听者不会继续收到事件。

## 11. 网络和本地多人

### 11.1 基本原则

- `UUIManager` 只存在于有 `ULocalPlayer` 的客户端。
- Dedicated Server 没有本地 UI。
- 服务器不能直接为远程客户端创建 Widget。
- 服务器必须通过 Client RPC 通知客户端打开 UI。

### 11.2 服务器和客户端对应的页面类

使用 `TSubclassOf<UBaseScreen>` 通过 RPC 传页面类时：

1. 页面类必须可以在客户端加载。
2. 不要把只存在于服务器模块的类型传给客户端。
3. 蓝图类路径在客户端和服务器资源版本一致时最稳妥。

### 11.3 本地玩家和多分屏

每个 LocalPlayer 都有独立 `UUIManager`。

获取指定玩家的方法：

```cpp
UUIManager* UIManager = UUIManager::GetUIManager(this, PlayerIndex);
```

不要把所有玩家共享的 GameInstance 当成 UI 管理器。

### 11.4 切图和重连

以下情况会自动清理旧 UI：

- LocalPlayer 切换 PlayerController。
- Seamless Travel。
- 重连。
- Subsystem 退初始化。

清理后应在新关卡的 GameMode `BeginPlay` 或 `PostLogin` 中重新打开需要的 HUD 或菜单。

不要在切图对象中强引用旧 Widget 或旧 UIManager。推荐使用：

```cpp
TWeakObjectPtr<UBaseScreen> CachedScreen;
TWeakObjectPtr<UUIManager> CachedUIManager;
```

## 12. 常见页面模式

### 12.1 游戏 HUD

- 使用 `OpenPersistentScreen`。
- `ScreenID` 固定为 `"HUD"`。
- 输入模式使用 `GameOnly`。
- 页面通常不禁用角色输入。
- 数据源默认是 PlayerState，组件数据从 Pawn 解析。

### 12.2 主菜单

- 使用 `OpenPersistentScreen`。
- `ScreenID` 固定为 `"MainMenu"`。
- 输入模式使用 `UIOnly`。
- 由 GameMode 的 `BeginPlay` 和 `PostLogin` 打开。
- 切图后重新创建。

### 12.3 暂停菜单

- 使用 `PushOverlay`。
- 输入模式使用 `UIOnly`。
- 暂停世界和恢复世界由 GameMode 或 PlayerController 负责，不由 UIManager 负责。
- 关闭后可恢复 Enhanced Input Mapping Context。

### 12.4 确认框

- 使用 `ShowPopup`。
- 输入模式使用 `UIOnly`。
- 通过 `Param` 传递确认内容。
- 确认后调用 `ClosePopup`。

### 12.5 背包或装备页面

- 使用 `PushScreen`。
- 如果角色需要同时移动，使用 `GameAndUI`。
- 如果页面完全接管输入，使用 `UIOnly`。
- 返回时调用 `PopScreen` 或使用默认 `OnBack`。

### 12.6 加载界面

- 使用 `PushOverlay`。
- 输入模式使用 `UIOnly`。
- 加载完成后调用 `PopOverlay`。
- 加载流程本身不应依赖 UI 完成才能推进，避免 UI 异常导致流程死锁。

## 13. 常见问题排查

### UI 没有显示

检查：

1. `ScreenClass` 是否为空。
2. GameMode 的页面类属性是否配置。
3. 当前控制器是否是本地控制器。
4. 远程客户端是否走了 `Client_OpenPersistentScreen`。
5. `UIManager` 是否成功初始化。
6. 是否在 Dedicated Server 上尝试创建 UI。

日志关键字：

```text
[UIManager]
[BeginGameMode]
[MHGameMode_Hunting]
```

### ESC 或手柄 B 无法返回

检查：

1. 页面是否设置了 `UIOnly` 或 `GameAndUI`。
2. 根 Widget 是否被其他逻辑替换了焦点。
3. 蓝图 `On Back` 是否接管但没有调用父实现。
4. 是否是 `GameOnly` 页面，需要由 PlayerController 输入调用 `HandleBack`。

### 打开页面后角色还在动

`UIManager` 只切换输入模式，不自动移除 Enhanced Input Mapping Context。

需要在业务层：

- 暂停角色输入。
- 移除对应 Mapping Context。
- 设置角色移动状态。
- 或在 GameMode 中暂停游戏。

### 页面关闭后还能点击

当前系统在 `OnClose` 开始时会立即把页面设为 `HitTestInvisible` 并禁用。

如果仍能点击，检查：

1. 是否有其他全屏 Widget 没有关闭。
2. 是否绕过 UIManager 手动把 Widget 添加到了视口。
3. 是否有其他系统创建了重复页面。

### 页面关闭动画没有播完

检查：

1. `CloseAnimDuration` 是否大于等于动画时长。
2. 是否调用了 `CloseAllScreens(true)`，该模式会跳过动画。
3. 是否直接调用了 `RemoveFromParent`。
4. 关闭动画是否在 `PlayCloseAnimation` 中实现。

### HUD 血量不更新

检查：

1. ViewModel 是否重写 `OnActivated` 和 `OnDeactivated`。
2. 是否在 `OnActivated` 中绑定 `UHealthComponent::OnHealthChanged`。
3. 是否在 `OnDeactivated` 中解除绑定。
4. 是否在 `RefreshAll` 中读取初始值。
5. 重生后是否使用本地 PlayerController 的 Pawn 作为兜底。
6. 绑定前后是否调用了对应的 `Super` 实现。

### 事件没有收到

检查：

1. `BroadcastTyped<T>` 和 `ListenTyped<T>` 的 `T` 是否完全一致。
2. 事件名是否完全一致。
3. 监听者是否在广播前注册。
4. 是否在 `OnDeactivated` 或切图时已经被 `UnlistenAll`。
5. 是否使用旧的无类型 `Listen` 监听强类型广播。

### 切图后出现重复 UI

检查：

1. 旧页面是否通过 UIManager 正常关闭。
2. 是否缓存并重用了旧 Widget。
3. 是否在 GameMode `BeginPlay` 和 `PostLogin` 中重复打开了同一个常驻 ID。
4. 是否绕过了 `OpenPersistentScreen` 的唯一 ID 机制。

常驻页面使用固定 `ScreenID`，重复调用是安全的。

### 弹窗被普通页面挡住

检查：

1. 弹窗是否使用 `ShowPopup`，而不是 `PushScreen`。
2. Popup 的 ZOrder 高于 Screen。
3. 是否有 Overlay 仍然存在，Overlay 会覆盖 Popup。

## 14. 编码约定

### 页面

- 所有页面继承 `UBaseScreen`。
- C++ 页面重写生命周期时调用 `Super`。
- 纯蓝图页面使用 `On Screen ...` 事件。
- 页面只负责显示和转发操作，不直接改核心战斗状态。
- 页面不要自己把自己从 Viewport 移除。

### ViewModel

- 所有业务适配继承 `UBaseViewModel`。
- Model 引用使用弱引用。
- 生命周期回调必须配对绑定和解绑。
- `RefreshAll` 必须是幂等操作。
- 蓝图可访问的数据提供函数或动态委托。

### UIManager

- 所有页面打开、关闭和返回都通过 `UUIManager`。
- 常驻页面使用固定 ID。
- 栈页面使用 `Push`、`Pop`、`ShowPopup` 和 `PushOverlay`。
- 不要在业务层保存强引用 UIManager 或 Widget 跨越关卡。

### 事件总线

- 每个事件定义独立载荷类型。
- 事件名建立统一常量。
- 使用 `ListenTyped` 和 `BroadcastTyped`。
- 对象销毁、页面关闭或切图时清理监听。
- 不在载荷中长期保存裸指针。

## 15. 最小完整示例

### Screen

```cpp
UCLASS()
class MH_API UMHExampleScreen : public UBaseScreen
{
    GENERATED_BODY()

public:
    UMHExampleScreen()
    {
        ViewModelClass = UVMExample::StaticClass();
        InputModePolicy = EUIScreenInputMode::UIOnly;
        CloseAnimDuration = 0.2f;
    }

    virtual void OnOpen(UObject* Param) override
    {
        Super::OnOpen(Param);

        UVMExample* VM = Cast<UVMExample>(GetViewModel());
        BIND_VM_PROPERTY(VM, DisplayText, &UMHExampleScreen::SetDisplayText);
    }

private:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TitleText;

    void SetDisplayText(FText Value)
    {
        if (TitleText)
        {
            TitleText->SetText(Value);
        }
    }
};
```

### ViewModel

```cpp
UCLASS()
class MH_API UVMExample : public UBaseViewModel
{
    GENERATED_BODY()

public:
    TBindedValue<FText> DisplayText;

    virtual void OnActivated() override
    {
        Super::OnActivated();
    }

    virtual void OnDeactivated() override
    {
        Super::OnDeactivated();
    }

    virtual void RefreshAll() override
    {
        Super::RefreshAll();

        if (AMHPlayerState* PlayerState = GetDataSource<AMHPlayerState>())
        {
            DisplayText.Set(FText::FromString(PlayerState->GetPlayerName()));
        }
    }
};
```

### 打开页面

```cpp
if (UUIManager* UIManager = UUIManager::GetUIManager(this))
{
    UIManager->PushScreen(UMHExampleScreen::StaticClass());
}
```

## 16. 最终接入检查

发布前建议逐项确认：

- [ ] 每个本地玩家有独立 UI，不共享 Widget。
- [ ] Dedicated Server 不创建 Widget。
- [ ] 远程客户端 UI 通过 Client RPC 打开。
- [ ] 页面输入模式符合操作需求。
- [ ] `CloseAnimDuration` 和关闭动画匹配。
- [ ] 页面生命周期成对绑定和解绑委托。
- [ ] ViewModel 的 Model 引用是弱引用。
- [ ] 常驻页面使用稳定 ScreenID。
- [ ] 切图和重连后重新打开所需 UI。
- [ ] 事件总线监听在对象销毁前清理。
- [ ] HUD 能处理 Pawn 重生和 PlayerState 复制顺序。
- [ ] 所有 UI 入口都经过 `UUIManager`，没有旁路 AddToViewport。
