# MH UI 系统使用文档

> 基于 UMG 的分层 UI 框架：`UUIManager`（页面调度）+ `UBaseScreen`（界面基类）+ `UBaseViewModel`（数据适配）+ `TBindedValue`（可观察属性）+ `UUIEventBus`（事件总线）。
> 引擎版本：UE 5.8。代码位置：`Source/MH/Public/UI/Core` 与 `Source/MH/Private/UI/Core`。

---

## 1. 系统架构

### 1.1 组件职责

| 组件 | 类型 | 职责 |
|---|---|---|
| `UUIManager` | `UTickableWorldSubsystem` | 页面栈、层级、ZOrder、输入模式切换、返回键路由。**随关卡自动创建/销毁** |
| `UBaseScreen` | `UUserWidget` 基类 | 纯 View：持有 ViewModel、绑定控件、把用户操作转发给 ViewModel |
| `UBaseViewModel` | `UObject` 基类 | Model↔View 适配层：读 Model 格式化输出、接收 Screen 转发、监听 Model 变化 |
| `TBindedValue<T>` | 模板（`Observable.h`） | 可观察属性：值变化时广播给订阅者 |
| `UUIEventBus` | `UGameInstanceSubsystem` | 全局事件总线，**跨关卡存活**，用于 UI 与游戏逻辑解耦 |

### 1.2 界面层级（EUILayer）

| 层级 | ZOrder | 用途 | 是否压栈 |
|---|---|---|---|
| `EUILayer::World` | 0 | 3D 空间 UI（伤害数字、怪物名字） | 预留，暂无 API |
| `EUILayer::HUD` | 10 | 常驻 HUD（血量、耐力、道具栏） | 不压栈（`PersistentScreens` 表） |
| `EUILayer::Screen` | 20 | 全屏界面（主菜单、装备箱） | 独立栈 |
| `EUILayer::Popup` | 30 | 弹窗（确认框、对话框） | 独立栈，可叠放 |
| `EUILayer::Overlay` | 40 | 最高层（暂停菜单、加载界面） | 独立栈 |

### 1.3 页面生命周期（由 UIManager 驱动）

| 阶段 | 触发时机 | 默认行为 |
|---|---|---|
| `OnOpen(Param)` | Push / Open 时 | 创建 ViewModel → Initialize → OnActivated → RefreshAll → 播打开动画 |
| `OnCovered()` | 被同层/上层页面覆盖时 | 置为不可交互、暂停 ViewModel（OnDeactivated） |
| `OnRevealed()` | 上层关闭后重新可见 | 恢复交互、重新激活 ViewModel、RefreshAll |
| `OnClose()` | 页面被弹出时 | 销毁 ViewModel、播关闭动画；**由 UIManager 在 CloseAnimDuration 后统一移除** |

### 1.4 数据流

```
Model（PlayerState / GameInstance / 子系统）
   ▲                        │ 读取/订阅
   │ RequestXxx()           ▼
ViewModel（TBindedValue 输出属性）
   ▲                        │ Set() / Broadcast()
   │ OnChanged 绑定          ▼
BaseScreen（BindWidget 控件引用）
   ▲                        │ 用户操作
   └────────────────────────┘
```

---

## 2. 快速上手（3 分钟示例）

**第 1 步**：新建 ViewModel（C++）

```cpp
UCLASS()
class UHUDViewModel : public UBaseViewModel
{
    GENERATED_BODY()
public:
    TBindedValue<float> HealthPercent;   // 输出属性

    virtual void RefreshAll() override   // 打开/恢复时自动同步一次
    {
        if (AMHPlayerState* PS = GetDataSource<AMHPlayerState>())
        {
            HealthPercent.Set(MaxHealth > 0 ? CurrentHealth / MaxHealth : 0.f);
        }
    }
};
```

**第 2 步**：新建 Screen（C++），声明控件并绑定

```cpp
UCLASS()
class UHUDWidget : public UBaseScreen
{
    GENERATED_BODY()
public:
    UHUDWidget() { ViewModelClass = UHUDViewModel::StaticClass(); }

    UPROPERTY(meta = (BindWidget))
    UProgressBar* HealthBar;

    virtual void OnOpen(UObject* Param) override
    {
        Super::OnOpen(Param);
        BIND_VM_PROPERTY(HealthPercent, &UHUDWidget::SetHealthBar);
    }

    void SetHealthBar(float Percent) { HealthBar->SetPercent(Percent); }
};
```

**第 3 步**：创建 Widget 蓝图（`Content/UI` 下），Parent Class 选 `UHUDWidget`，画一个命名 `HealthBar` 的进度条。

**第 4 步**：在关卡的 `PlayerController::BeginPlay` 或 `GameMode::BeginPlay` 打开：

```cpp
if (UUIManager* UI = GetWorld()->GetSubsystem<UUIManager>())
{
    UI->OpenPersistentScreen(FName("HUD"), UHUDWidget::StaticClass());
}
```

蓝图侧：`Get Subsystem (World)` → `UUIManager` → 对应函数，类参数选择 `WBP_xxx`。

---

## 3. UUIManager 接口

获取方式（任意有 World 的地方）：

```cpp
UUIManager* UI = GetWorld()->GetSubsystem<UUIManager>();
```

### 3.1 常驻页面（HUD）

| 接口 | 说明 |
|---|---|
| `UBaseScreen* OpenPersistentScreen(FName ScreenID, TSubclassOf<UBaseScreen> ScreenClass)` | 打开常驻页面；ScreenID 重复时返回已有实例，不重复创建 |
| `void ClosePersistentScreen(FName ScreenID)` | 关闭常驻页面（播关闭动画后移除） |
| `UBaseScreen* GetPersistentScreen(FName ScreenID) const` | 获取常驻页面，不存在返回 `nullptr` |

### 3.2 全屏页面（Screen 层）

| 接口 | 说明 |
|---|---|
| `UBaseScreen* PushScreen(TSubclassOf<UBaseScreen> ScreenClass, UObject* Param = nullptr)` | 压入全屏页，覆盖当前栈顶（自动 OnCovered）；自动切换 UI 输入模式并聚焦新页面 |
| `void PopScreen()` | 弹出栈顶页面 |
| `void PopToScreen(UBaseScreen* TargetScreen)` | 关闭到指定页面（含目标页）；目标不在栈中则忽略 |

### 3.3 弹窗（Popup 层）

| 接口 | 说明 |
|---|---|
| `UBaseScreen* ShowPopup(TSubclassOf<UBaseScreen> PopupClass, UObject* Param = nullptr)` | 打开弹窗；自动覆盖 Screen 层栈顶，可叠放多个弹窗 |
| `void ClosePopup()` | 关闭最顶层弹窗 |

### 3.4 最高层（Overlay 层）

| 接口 | 说明 |
|---|---|
| `UBaseScreen* PushOverlay(TSubclassOf<UBaseScreen> OverlayClass, UObject* Param = nullptr)` | 打开最高层页面（暂停菜单、加载界面） |
| `void PopOverlay()` | 关闭最顶层 Overlay |

### 3.5 返回键路由

| 接口 | 说明 |
|---|---|
| `void HandleBack()` | 返回键统一入口：按 **Overlay > Popup > Screen** 优先级关闭最顶层页面；全部为空时无操作 |

### 3.6 查询

| 接口 | 说明 |
|---|---|
| `UBaseScreen* GetTopScreen(EUILayer Layer) const` | 获取指定层级的栈顶页面 |
| `UBaseScreen* GetTopmostScreen() const` | 获取全局最顶层页面（Overlay > Popup > Screen），无页面返回 `nullptr` |
| `int32 GetScreenStackDepth(EUILayer Layer) const` | 指定层级的栈深度 |

### 3.7 内部方法（框架自用，外部不要调用）

`PushToLayer` / `PopFromLayer` / `CreateAndAddScreen` / `GetLayerBaseZOrder` / `SetInputModeUI` / `SetInputModeGame` / `UpdateInputMode`。

### 3.8 行为约定

- 打开任意 Screen/Popup/Overlay → `FInputModeUIOnly` + 鼠标显示 + 键盘焦点给到新页面；全部关闭 → `FInputModeGameOnly`。
- Popup/Overlay 打开时自动 `OnCovered` 覆盖 Screen 层栈顶；其全部关闭后自动恢复。
- 纯服务器（无本地玩家 Controller）不创建 Widget。
- `IsTickableWhenPaused` 返回 true：游戏暂停时关闭动画后的页面也能被移除。

---

## 4. UBaseScreen 接口

### 4.1 生命周期（子类可重写）

| 接口 | 说明 |
|---|---|
| `virtual void OnOpen(UObject* Param = nullptr)` | 页面打开：创建 VM → Initialize → OnActivated → RefreshAll → PlayOpenAnimation。**子类重写时先调用 `Super::OnOpen(Param)` 再做绑定** |
| `virtual void OnCovered()` | 被覆盖：不可交互 + VM OnDeactivated |
| `virtual void OnRevealed()` | 恢复可见：可交互 + VM OnActivated + RefreshAll |
| `virtual void OnClose()` | 关闭：销毁 VM + PlayCloseAnimation（移除由 UIManager 负责） |
| `virtual void OnBack()` | 返回键默认行为：`OwnerUIManager->HandleBack()`；子类可重写拦截（如"确认后退出"） |

### 4.2 钩子

| 接口 | 说明 |
|---|---|
| `virtual UObject* GetDataSource() const` | 默认返回拥有玩家 PlayerState；可在子类重写（如返回 GameInstance） |
| `UFUNCTION(BlueprintNativeEvent) void PlayOpenAnimation()` | 打开动画，**蓝图可直接重写** |
| `UFUNCTION(BlueprintNativeEvent) void PlayCloseAnimation()` | 关闭动画，**蓝图可直接重写** |

### 4.3 按键处理

| 接口 | 说明 |
|---|---|
| `virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override` | ESC / 手柄 B（Gamepad_FaceButton_Right）→ `OnBack()`。需要页面获得键盘焦点（UIManager 打开页面时已自动聚焦） |

### 4.4 公开访问（BlueprintCallable）

| 接口 | 说明 |
|---|---|
| `UBaseViewModel* GetViewModel() const` | 获取当前 ViewModel |
| `bool IsScreenOpen() const` | 是否处于打开状态 |
| `bool IsTopmost() const` | 是否处于栈顶（未被覆盖） |

### 4.5 受保护成员（子类可用）

| 成员 | 说明 |
|---|---|
| `TSubclassOf<UBaseViewModel> ViewModelClass` | 蓝图 Class Defaults（Screen 分类）或 C++ 构造函数里设置 |
| `float CloseAnimDuration = 0.3f` | 关闭动画时长，UIManager 用它延迟移除页面（Class Defaults 可调） |
| `UBaseViewModel* ViewModel` | 当前 ViewModel 实例 |
| `UUIManager* OwnerUIManager` | 所属 UIManager（由 UIManager 注入） |
| `bool bIsOpen` / `bool bIsTopmost` | 运行状态 |

### 4.6 绑定宏

```cpp
// BaseScreen.h 中定义，用法（Screen 的 OnOpen 里）：
BIND_VM_PROPERTY(HealthPercent, &UHUDWidget::SetHealthBar);

// 等价于：
ViewModel->HealthPercent.OnChanged.AddUObject(this, &UHUDWidget::SetHealthBar);
```

> 注意：`OnOpen` 是纯 C++ 虚函数，**Widget 蓝图无法重写**。要使用 ViewModel 绑定，Screen 必须是 C++ 子类（布局可以在蓝图里做）。纯蓝图界面只能直接读数据（见第 9 节）。

---

## 5. UBaseViewModel 接口

### 5.1 生命周期（由 Screen 调用，子类可重写）

| 接口 | 说明 |
|---|---|
| `virtual void Initialize(UObject* InOuter, UObject* InData)` | 初始化：记录持有者（Screen）与数据源（默认 PlayerState） |
| `virtual void OnActivated()` | 激活：子类在此注册 Model 回调 / `GetBus()->Listen(...)` |
| `virtual void OnDeactivated()` | 暂停：**自动 `GetBus()->UnlistenAll(this)`**，子类在此解绑 Model 回调 |
| `virtual void OnDestroy()` | 销毁前最后一次清理（内部调用 OnDeactivated） |
| `virtual void RefreshAll()` | 强制同步 Model → 全部输出属性；打开/恢复时自动调用 |

### 5.2 工具方法

| 接口 | 说明 |
|---|---|
| `template<typename T> T* GetDataSource() const` | 类型安全的取数据源，如 `GetDataSource<AMHPlayerState>()` |
| `UUIEventBus* GetBus() const` | 获取全局事件总线（从 GameInstance 取，跨关卡可用） |
| `bool IsActive() const` | 当前是否激活（能否收到 Model 回调） |

### 5.3 受保护成员

`TWeakObjectPtr<UObject> DataSource`（数据源，弱引用）、`TWeakObjectPtr<UObject> OuterWidget`（持有者）、`bool bIsActive`。

### 5.4 子类模板

```cpp
UCLASS()
class UMyViewModel : public UBaseViewModel
{
    GENERATED_BODY()
public:
    // 输出 — Screen 绑定这些
    TBindedValue<FString> DisplayText;
    TBindedValue<int32>   CurrentValue;

    // 输入 — Screen 转发用户操作到这里
    void RequestDoSomething(int32 Arg);

    // Model 回调 / 总线监听
    void OnModelDataChanged();

    virtual void RefreshAll() override { /* Model → 输出属性 */ }
};
```

---

## 6. TBindedValue\<T\> 接口

定义于 `Observable.h`。`T` 支持 `FString`、`FText`、`float`、`int32`、`bool`、枚举、指针等。

| 接口 | 说明 |
|---|---|
| `void Set(const T& NewValue)` | 设置值；**与当前值不同才广播**（初次 Set 一定会广播，因为初始为值初始化） |
| `const T& Get() const` | 读取当前值 |
| `void Broadcast() const` | 强制广播当前值（用于手动全量刷新） |
| `operator const T& () const` | 隐式转换，可直接当 T 用 |
| `TBindedValue& operator=(const T&)` | 赋值即 Set |
| `FOnChanged OnChanged` | 多播委托：`OnChanged.AddUObject(Widget, &X::SetY)` 订阅 |

---

## 7. UUIEventBus 接口

### 7.1 数据结构

```cpp
USTRUCT(BlueprintType)
struct FUIEventPayload            // 事件负载基类（可继承扩展）
{
    virtual ~FUIEventPayload() = default;
};

USTRUCT(BlueprintType)
struct FUIEmptyPayload : public FUIEventPayload  // 空负载
```

事件处理委托：`DECLARE_DELEGATE_OneParam(FUIEventHandler, const FUIEventPayload&)`。

### 7.2 接口

| 接口 | 说明 |
|---|---|
| `void Broadcast(FName EventName, const FUIEventPayload& Payload)` | 广播事件（带负载） |
| `void Broadcast(FName EventName)` | 广播事件（空负载） |
| `FDelegateHandle Listen(FName EventName, FUIEventHandler InHandler)` | 订阅；重复注册同一委托实例会被忽略 |
| `void Unlisten(FName EventName, FDelegateHandle InHandle)` | 按精确句柄退订 |
| `void UnlistenAll(const UObject* InObject)` | 退订某对象的所有监听（VM 的 OnDeactivated 自动调用） |
| `int32 GetListenerCount(FName EventName) const` | 查询监听者数量（调试用） |

### 7.3 使用示例

```cpp
// 订阅（通常在 VM::OnActivated）
FDelegateHandle Handle = GetBus()->Listen(
    FName("QuestCompleted"),
    FUIEventHandler::CreateUObject(this, &UQuestVM::OnQuestCompleted));

// 发布（游戏逻辑侧，任意对象）
GetBus()->Broadcast(FName("QuestCompleted"));
```

约定与限制：
- **每个事件名对应固定的负载类型**，监听方按约定 downcast，不要混用。
- 同一事件的递归广播会被跳过（防死循环）；回调里广播**其他事件**是允许的。
- `Broadcast` 内部复制监听列表，回调中退订安全。

---

## 8. 完整示例

### 8.1 主菜单压栈切换

```cpp
// 打开主菜单（GameMode::BeginPlay 或关卡蓝图）
UIManager->PushScreen(UWBP_MainMenu::StaticClass());   // 实际传 Widget 蓝图类

// 点"设置"：主菜单被覆盖（OnCovered），设置页接管输入
UIManager->PushScreen(UWBP_Settings::StaticClass());

// 返回：ESC 自动处理，或代码调用
UIManager->PopScreen();                                 // 设置页播关闭动画后移除
```

### 8.2 弹窗与 Overlay

```cpp
// 确认框（自动覆盖下层 Screen，ESC 关闭）
UBaseScreen* Confirm = UIManager->ShowPopup(UWBP_ConfirmDialog::StaticClass());

// 暂停菜单（最高层）
UIManager->PushOverlay(UWBP_PauseMenu::StaticClass());
UIManager->PopOverlay();
```

### 8.3 带参数的打开与 VM 读取

```cpp
// 打开装备详情，Param 传物品数据（任意 UObject，如 UItemData*）
UIManager->PushScreen(UWBP_ItemDetail::StaticClass(), ItemData);

// Screen::OnOpen 里取出并传给 VM
void UItemDetailScreen::OnOpen(UObject* Param)
{
    Super::OnOpen(Param);
    if (UItemViewModel* VM = Cast<UItemViewModel>(GetViewModel()))
    {
        VM->SetItem(Cast<UItemData>(Param));
    }
}
```

### 8.4 事件总线联机刷新

```cpp
// PlayerState 血量变化（服务器端 OnRep 或属性变更回调里）
GetBus()->Broadcast(FName("HealthChanged"));

// HUDViewModel::OnActivated 里订阅
GetBus()->Listen(FName("HealthChanged"),
    FUIEventHandler::CreateUObject(this, &UHUDViewModel::OnHealthChanged));

// HUDViewModel::OnHealthChanged 里
HealthPercent.Set(PS->CurrentHealth / PS->MaxHealth);   // 控件自动刷新
```

---

## 9. 纯蓝图快速路径

- 直接继承 `UBaseScreen` 建 Widget 蓝图，布局与逻辑全在蓝图。
- 数据直接读：`Get Player State` / `Get Game Instance` / `Get Subsystem` 节点。
- 返回键：蓝图里重写 `OnBack`（Event）即可自定义关闭行为。
- 打开/关闭动画：直接重写 `PlayOpenAnimation` / `PlayCloseAnimation`（BlueprintNativeEvent）。
- 局限：`OnOpen` 与 `TBindedValue` 不可在蓝图使用，数据刷新需自行处理（Tick/委托/Event 分发）。正式功能建议走 C++ ViewModel 流程。

---

## 10. 联机与多玩家

- UIManager 在服务器上存在但**不创建 Widget**（无本地玩家时自动跳过）。
- UI 数据只读**复制属性**（`ReplicatedUsing` + OnRep），权威修改只发生在服务器。
- 建议：ViewModel 只做"读并格式化"，不做任何写权限判断；写操作走 `Server RPC` / `GameInstance` 会话接口。
- 事件总线是 GameInstanceSubsystem，客户端各自独立，不会跨端广播。

---

## 11. 常见问题

**Q1：为什么切关卡后 HUD 没了？**
UIManager 是 WorldSubsystem，随关卡销毁。请在**每个关卡**的 GameMode / PlayerController `BeginPlay` 里重新 `OpenPersistentScreen`。

**Q2：为什么按钮点了没反应？**
页面被覆盖时 `OnCovered` 会禁用交互；检查 `IsTopmost()`。另外弹窗打开时会自动覆盖 Screen 层。

**Q3：ESC 没触发 OnBack？**
页面必须持有键盘焦点——UIManager 打开页面时会自动聚焦；若手动 `SetInputMode` 覆盖了输入模式，需要重新 `SetInputModeUI(页面)`。

**Q4：绑定后界面不刷新？**
`TBindedValue::Set` 值相同不广播；强制刷新用 `Broadcast()` 或 `RefreshAll()`。检查绑定是否在 `OnOpen` 里（VM 创建之后）执行。

**Q5：中文乱码？**
源码保持 UTF-8（无 BOM），注释与日志中的中文不受影响。

**Q6：编译报错找不到 FReply 等 Slate 符号？**
`SlateCore` 已加入 `MH.Build.cs` 直接依赖；若新增模块，记得直接依赖你用到符号的模块（不要只依赖传递依赖）。