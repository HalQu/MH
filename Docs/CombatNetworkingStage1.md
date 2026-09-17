# 联机战斗第一阶段实现说明

本文说明本次“最小联机战斗闭环”的代码改动。当前阶段的边界是：

```text
客户端输入
-> Server RPC
-> 服务器验证并选择动作
-> NetMulticast 播放动作
-> 复制核心战斗状态
-> 所有客户端看到相同动作
```

当前阶段还没有做客户端预测、回滚、伤害同步和完整的蓄力同步。

## 1. 先理解几个网络概念

### 1.1 Authority

服务器拥有游戏逻辑的最终决定权。

```cpp
GetOwner()->HasAuthority()
```

只有服务器上的这个结果为 `true`。动作选择、连招、命中判定等逻辑应该只在服务器执行。

### 1.2 Autonomous Proxy

客户端自己控制的角色，在客户端上称为 Autonomous Proxy。

```cpp
GetOwner()->GetLocalRole() == ROLE_AutonomousProxy
```

客户端玩家的按键会在这个角色上触发，但客户端不能直接把动作状态当成权威结果，所以输入需要发给服务器。

### 1.3 Simulated Proxy

其他玩家在本地客户端上的角色副本，称为 Simulated Proxy。

```cpp
GetOwner()->GetLocalRole() == ROLE_SimulatedProxy
```

它只负责显示，不接受本地输入，也不能修改权威战斗状态。

### 1.4 Server RPC

Server RPC 是客户端调用、服务器执行的远程函数。

这次新增的是：

```cpp
UFUNCTION(Server, Reliable)
void Server_HandleComboInput(
    const FSoftObjectPath& InputActionPath,
    ETriggerEvent TriggerEvent,
    FVector2D MoveInput,
    float HoldDuration,
    int32 ClientInputSequence);
```

实际函数体写成：

```cpp
void UCombatComponent::Server_HandleComboInput_Implementation(...)
```

`_Implementation` 是 Unreal 对 RPC 的命名约定。调用点仍然写 `Server_HandleComboInput(...)`。

`Reliable` 表示网络层会尽量保证这个事件送达。动作开始、动作结束这类不能丢失的事件适合使用可靠 RPC。

### 1.5 NetMulticast

NetMulticast 是服务器调用、服务器和所有相关客户端都会执行的函数。

这次新增：

```cpp
UFUNCTION(NetMulticast, Reliable)
void Multicast_PlayMove(
    const FSoftObjectPath& WeaponPath,
    int32 MoveIndex,
    FName SectionName,
    float PlayRate);

UFUNCTION(NetMulticast, Reliable)
void Multicast_StopMove(
    const FSoftObjectPath& WeaponPath,
    int32 MoveIndex);
```

Montage 不会因为我们只在服务器调用 `PlayAnimMontage()` 就自动在所有客户端播放，所以这里显式用 Multicast 通知所有端播放。

### 1.6 Replicated Property

Replicated Property 是服务器同步给客户端的属性。

服务器修改属性后，客户端会收到更新。只有服务器应该修改权威复制属性。

本次被复制的是：

```text
CurrentWeaponPath
CombatState
MovePhase
CurrentMoveId
CurrentMoveIndex
bComboWindowOpen
```

这些属性的复制声明集中在：

```cpp
void UCombatComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
```

### 1.7 RepNotify

RepNotify 是客户端收到复制属性后执行的回调。

例如：

```cpp
UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponPath)
FSoftObjectPath CurrentWeaponPath;
```

客户端收到新的武器路径后会执行：

```cpp
void UCombatComponent::OnRep_CurrentWeaponPath();
```

RepNotify 只负责把服务器状态转换成客户端表现，不应该重新执行一次权威逻辑。

### 1.8 FSoftObjectPath

网络同步不应该直接依赖裸的 `UObject*` 指针。这里使用 `FSoftObjectPath` 表示资源路径：

```text
/Game/.../DA_Weapon.DA_Weapon
```

服务器发送路径，各端再通过本地的同一个资源路径解析成真实对象。

本次使用 `FSoftObjectPath` 传输：

```text
UInputAction
UWeaponDataAsset
```

## 2. 改动文件总览

本次修改了三个源文件：

```text
Source/MH/Public/GamePlay/Combat/UCombatComponent.h
Source/MH/Private/GamePlay/Combat/UCombatComponent.cpp
Source/MH/Private/GamePlay/MHPlayerController.cpp
```

没有修改连招配置结构，也没有要求迁移武器数据表。

## 3. UCombatComponent 新增的变量

### 3.1 CurrentWeaponPath

```cpp
UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponPath)
FSoftObjectPath CurrentWeaponPath;
```

作用：

```text
服务器把当前武器资源路径同步给客户端。
客户端收到路径后，在 OnRep_CurrentWeaponPath 中解析 UWeaponDataAsset。
```

`CurrentWeapon` 仍然是本地解析后的对象指针，不直接复制。

### 3.2 PendingServerHoldDuration

```cpp
float PendingServerHoldDuration = -1.f;
```

作用：

```text
客户端把按键按住时长一起发给服务器。
服务器在 Server RPC 中暂存这个值，再交给正常的 HandleComboInput 流程。
```

这是当前阶段的临时传递变量。后续实现服务器侧输入时间线后可以移除。

### 3.3 LocalInputSequence

```cpp
int32 LocalInputSequence = 0;
```

作用：

```text
客户端每发送一次攻击输入就递增一次。
这个数字用于后续确认、丢包识别和回滚重放。
```

当前阶段只用于给服务器提供一个递增序号。

### 3.4 LastReceivedInputSequence

```cpp
int32 LastReceivedInputSequence = 0;
```

作用：

```text
服务器记录已经从该客户端接受的最后一个输入序号。
如果收到的序号不大于它，就丢弃，避免重复处理。
```

当前不是完整的预测回滚确认机制，只是最小防重复处理。

## 4. UCombatComponent 修改的复制属性

这些变量没有改名字和类型，只增加了复制标记：

| 变量 | 原状态 | 新状态 | 作用 |
|---|---|---|---|
| `CombatState` | 普通属性 | `ReplicatedUsing = OnRep_CombatState` | 同步战斗阶段 |
| `MovePhase` | 普通属性 | `ReplicatedUsing = OnRep_CombatState` | 同步动作阶段 |
| `CurrentMoveId` | 普通属性 | `Replicated` | 同步当前动作标识 |
| `CurrentMoveIndex` | 普通属性 | `ReplicatedUsing = OnRep_CurrentMoveIndex` | 同步动作表索引 |
| `bComboWindowOpen` | 普通属性 | `Replicated` | 同步连招窗口是否打开 |

没有直接复制的内容：

```text
CurrentMoveData
CurrentMoveTime
CurrentWeaponMesh
CurrentChargeInputAction
bIsCharging
bChargeInputHeld
```

原因是这些内容要么是本地派生数据，要么与后续蓄力同步阶段有关。

## 5. UCombatComponent 新增的函数

### 5.1 GetLifetimeReplicatedProps

```cpp
virtual void GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const override;
```

作用：

```text
告诉 Unreal 哪些 UPROPERTY 需要参与网络复制。
```

本次注册了：

```cpp
DOREPLIFETIME(UCombatComponent, CurrentWeaponPath);
DOREPLIFETIME(UCombatComponent, CombatState);
DOREPLIFETIME(UCombatComponent, MovePhase);
DOREPLIFETIME(UCombatComponent, CurrentMoveId);
DOREPLIFETIME(UCombatComponent, CurrentMoveIndex);
DOREPLIFETIME(UCombatComponent, bComboWindowOpen);
```

### 5.2 Server_HandleComboInput

```cpp
UFUNCTION(Server, Reliable)
void Server_HandleComboInput(
    const FSoftObjectPath& InputActionPath,
    ETriggerEvent TriggerEvent,
    FVector2D MoveInput,
    float HoldDuration,
    int32 ClientInputSequence);
```

执行流程：

```text
1. 检查服务器权威。
2. 检查输入序号是否比上一次新。
3. 从路径解析 UInputAction。
4. 保存服务器侧移动输入和按住时长。
5. 调用原有的 HandleComboInput 逻辑。
```

### 5.3 Multicast_PlayMove

```cpp
UFUNCTION(NetMulticast, Reliable)
void Multicast_PlayMove(
    const FSoftObjectPath& WeaponPath,
    int32 MoveIndex,
    FName SectionName,
    float PlayRate);
```

作用：

```text
服务器决定播放哪个动作后，把这个动作通知所有端。
各端从 WeaponPath 解析同一个 UWeaponDataAsset，再读取 Moves[MoveIndex]。
```

### 5.4 Multicast_StopMove

```cpp
UFUNCTION(NetMulticast, Reliable)
void Multicast_StopMove(
    const FSoftObjectPath& WeaponPath,
    int32 MoveIndex);
```

作用：

```text
服务器结束动作时，让所有端停止对应 Montage。
```

### 5.5 OnRep_CurrentWeaponPath

```cpp
UFUNCTION()
void OnRep_CurrentWeaponPath();
```

作用：

```text
客户端收到武器路径后：
1. 加载 UWeaponDataAsset。
2. 设置 CurrentWeapon。
3. 创建或更新武器 Mesh。
4. 如果当前已有 MoveIndex，重新解析动作数据。
5. 触发 OnWeaponChanged。
```

### 5.6 OnRep_CombatState

```cpp
UFUNCTION()
void OnRep_CombatState();
```

作用：

```text
客户端收到 CombatState 或 MovePhase 后，广播 OnCombatStateChanged。
```

### 5.7 OnRep_CurrentMoveIndex

```cpp
UFUNCTION()
void OnRep_CurrentMoveIndex();
```

作用：

```text
客户端收到当前动作索引后，从 CurrentWeapon->Moves 中恢复 CurrentMoveData。
```

这样客户端的 Tick、Montage 位置查询和表现逻辑仍然可以读取当前动作数据。

### 5.8 PlayMovePresentation

```cpp
void PlayMovePresentation(
    UWeaponDataAsset* MoveWeapon,
    int32 MoveIndex,
    FName SectionName,
    float PlayRate);
```

作用：

```text
所有机器共用的动作表现入口。
它根据武器和动作索引找到 Montage，然后调用 PlayAnimMontage。
```

服务器和客户端都通过这个函数播 Montage，避免两条互不相同的播放路径。

### 5.9 StopMovePresentation

```cpp
void StopMovePresentation(
    UWeaponDataAsset* MoveWeapon,
    int32 MoveIndex);
```

作用：

```text
所有机器共用的动作停止入口。
它根据武器和动作索引找到 Montage，并停止播放。
```

### 5.10 UpdateWeaponMesh

```cpp
void UpdateWeaponMesh(UWeaponDataAsset* NewWeapon);
```

作用：

```text
根据当前武器数据创建和挂载本地 StaticMeshComponent。
这个 Mesh 不需要复制，各端根据复制下来的武器路径本地创建。
```

## 6. UCombatComponent 修改的函数

### 6.1 构造函数

新增：

```cpp
SetIsReplicatedByDefault(true);
```

意义：

```text
让 CombatComponent 本身参与 Actor 复制。
如果组件本身不复制，它内部标记的复制属性也不会正常同步。
```

### 6.2 BeginPlay

修改前：

```text
所有端都会执行 EquipWeapon_Default。
```

修改后：

```text
服务器负责装备默认武器。
客户端等待 CurrentWeaponPath 从服务器复制回来。
```

这样客户端不会先本地装备一个武器，再从服务器覆盖成另一个状态。

如果服务器在 BeginPlay 前已经设置了 `CurrentWeapon`，代码也会补上 `CurrentWeaponPath`。

### 6.3 HandleComboInput

函数签名没有改变：

```cpp
bool HandleComboInput(
    UInputAction* InputAction,
    ETriggerEvent TriggerEvent);
```

内部增加了角色判断：

```text
服务器：
    继续执行原有权威连招逻辑。

本地客户端角色：
    计算 HoldDuration，递增 LocalInputSequence。
    调用 Server_HandleComboInput，等待服务器处理。

其他角色：
    直接返回，不接受输入。
```

客户端蓄力释放仍然保留了一部分本地表现处理，用于即时恢复本地 Montage 播放速率。

### 6.4 EquipWeapon

函数签名没有改变。

内部变化：

```text
1. 设置 CurrentWeaponPath = FSoftObjectPath(NewWeapon)。
2. 使用 UpdateWeaponMesh 统一创建武器 Mesh。
3. 原有 CurrentWeapon 和 OnWeaponChanged 逻辑保留。
```

这使客户端可以通过复制路径重新创建武器表现。

### 6.5 HandleCombatNotify

函数签名没有改变。

新增服务器检查：

```cpp
if (!GetOwner() || !GetOwner()->HasAuthority())
{
    return;
}
```

意义：

```text
AttackStart、AttackHit、RecoveryStart、MoveEnd 这些会修改权威游戏状态的动画通知，
只在服务器执行。
```

### 6.6 HandleCombatNotifyState

函数签名没有改变。

修改范围：

```text
ComboWindow：
    只在服务器打开或关闭连招窗口。

WeaponSwitchAllowed：
    只在服务器改变武器切换窗口。

ChargeWindow：
    仍然允许客户端处理本地播放速率表现。
```

原因是当前阶段还没有完成蓄力状态的跨端同步。

### 6.7 StartMove

函数签名没有改变：

```cpp
bool StartMove(
    const FMHCombatMoveData& Move,
    int32 MoveIndex,
    UInputAction* SourceInputAction);
```

内部变化：

```text
1. 增加 Authority 检查。
2. 服务器仍然负责设置 CurrentMoveData、CurrentMoveIndex、CurrentMoveId。
3. 服务器更新复制状态和 MovePhase。
4. 原来的直接 PlayAnimMontage 改为 Multicast_PlayMove。
5. OnAttackStarted 广播移动到 PlayMovePresentation 中统一处理。
```

### 6.8 FinishCurrentMove

函数签名没有改变。

内部变化：

```text
1. 增加 Authority 检查。
2. 在清空当前动作数据前保存 WeaponPath 和 MoveIndex。
3. 调用 Multicast_StopMove，让所有端停止 Montage。
4. 然后清理服务器上的动作状态。
```

### 6.9 HandleMontageBlendingOut

函数签名没有改变。

新增 Authority 检查，避免客户端 Montage 混合结束影响服务器连招状态。

### 6.10 HandleMontageEnded

函数签名没有改变。

新增 Authority 检查，避免客户端 Montage 自然结束导致客户端自行解锁或切换连招。

### 6.11 MHPlayerController 构造函数

修改：

```cpp
bReplicates = false;
```

改为：

```cpp
bReplicates = true;
```

意义：

```text
PlayerController 需要正常参与网络复制。
角色本身原本已经设置了 bReplicates = true，但 PlayerController 不能保持关闭。
```

## 7. 两条完整的网络调用链

### 7.1 主机玩家攻击

```text
主机输入
-> HandleComboInput
-> OwnerRole == ROLE_Authority
-> TryStartAttack / BufferNextCombo
-> StartMove
-> 修改复制状态：CombatState、MovePhase、CurrentMoveIndex 等
-> Multicast_PlayMove
-> 服务器和所有客户端执行 PlayMovePresentation
```

### 7.2 客户端玩家攻击

```text
客户端输入
-> HandleComboInput
-> OwnerRole == ROLE_AutonomousProxy
-> Server_HandleComboInput
-> 服务器 HandleComboInput
-> 服务器 TryStartAttack / BufferNextCombo
-> 服务器 StartMove
-> 修改复制状态
-> Multicast_PlayMove
-> 服务器和所有客户端执行 PlayMovePresentation
```

### 7.3 其他玩家在本地看到的情况

```text
其他玩家的角色是 ROLE_SimulatedProxy。
它不接受本地攻击输入。
服务器复制状态并通过 Multicast 通知播放动作。
Simulated Proxy 只负责播放动画和更新表现。
```

## 8. 当前阶段的限制

### 8.1 没有客户端预测

客户端按下攻击后，要等服务器处理并通过 Multicast 返回，才会真正开始播放动作。

所以当前会有网络延迟：

```text
本地按键
-> 等待一个网络往返
-> 动作开始
```

客户端预测、输入序号确认、服务器校正和回滚重放是下一阶段内容。

### 8.2 伤害还没有同步

`AttackHit` 现在只在服务器执行 `PerformHitCheck()`。

这保证了伤害不会在多个客户端重复结算，但敌人的生命值、命中结果和伤害事件还没有复制。

### 8.3 蓄力只完成了基础兼容

当前处理了：

```text
客户端蓄力释放时本地恢复播放速率。
服务器收到释放输入后恢复服务器上的播放速率。
```

还没有完成：

```text
其他客户端同步蓄力开始和结束。
服务器权威的蓄力进度复制。
断线重连或迟加入时的蓄力状态恢复。
```

### 8.4 武器切换还没有完整 Server RPC 化

当前已经复制了：

```text
CurrentWeaponPath
CurrentWeaponMesh 的本地创建
```

但 `EquipWeapon()`、`CycleWeapon()` 还没有完整的服务器请求入口和防作弊校验。

### 8.5 迟加入时正在播放的动作

迟加入的客户端可以从复制属性知道当前动作索引，但还没有收到一次完整的“从当前 Montage 位置开始播放”的追赶通知。

### 8.6 Triggered 输入当前会频繁发送

Enhanced Input 的 `Triggered` 可能每帧触发一次。

当前实现会为这些事件发送可靠 Server RPC，功能上可以工作，但网络上偏重。后续可以改成：

```text
Started / Completed 使用 Reliable RPC
Triggered 使用 Unreliable RPC 或按固定频率发送
```

## 9. 建议测试顺序

1. 单机模式确认原有连招和蓄力攻击没有回归。
2. Listen Server + 一个客户端，主机攻击，客户端观察动作。
3. 客户端攻击，主机观察动作。
4. 客户端连续输入第二段连招，确认所有端播放同一个动作。
5. 客户端蓄力攻击，确认服务器收到释放并恢复正常播放速率。
6. 两个客户端互相观察，确认远端角色不是只在本地播放，而是由服务器广播触发。
7. 检查服务器日志中的 `TryStartAttack` 和 `StartMove` 是否只在服务器执行。
