# MH 战斗系统配置手册

> 本文档只对应当前源码实现。
> `Docs/CombatSystem.md` 已经过时，不作为配置依据。

---

## 1. 总体规则

当前战斗系统由三层组成：

| 层 | 负责内容 |
|---|---|
| `UCombatComponent` | 连招状态机、输入缓冲、阶段切换、武器切换、命中与伤害 |
| `UWeaponDataAsset` | 招式索引和招式数据，不包含战斗逻辑 |
| `UAnimMontage` | 动作播放、Section、动画通知时机 |

现在的时间信息全部来自蒙太奇通知，`FMHCombatMoveData` 中已经不存在：

- `StartupTime`
- `ActiveTime`
- `RecoveryTime`
- `HitMoment`
- `ComboWindowStart`
- `ComboWindowEnd`
- `CancelWindowStart`
- `CancelWindowEnd`

因此：

- 没有配置对应通知时，不会有对应的自动行为。
- `Montage` 为空时，动作无法启动。
- 引导攻击、蓄力、命中、连招窗口和切武器时机都必须配置在蒙太奇中。

---

## 2. Section Name 的用途

### 2.1 `FMHCombatMoveData::SectionName`

`SectionName` 是“这个招式开始播放时，从蒙太奇的哪个命名 Section 开始播放”。

当前代码调用：

```cpp
PlayAnimMontage(Montage, MontagePlayRate, SectionName);
```

作用：

- 一个蒙太奇可以包含多个 Section。
- 通过 `SectionName` 选择动作的入口段。
- 不控制连招、伤害或阶段切换。
- 留空 `None` 时，从蒙太奇的默认起始位置播放。
- Section 名称区分大小写，必须和蒙太奇中的名称完全一致。

示例：

| 使用方式 | `SectionName` |
|---|---|
| 蒙太奇只做一个完整攻击 | 留空，或填写默认 Section 名称 |
| 一个蒙太奇包含地面攻击、空中攻击两种入口 | 分别填写地面、空中 Section 名称 |
| 蓄力动作从蓄力段开始 | 填蓄力 Section 名称 |

### 2.2 `FMHCombatMoveData::AttackSectionName`

`AttackSectionName` 专门用于蓄力动作，表示“松开按键时跳到哪一段攻击 Section”。

当前代码：

```cpp
Montage_JumpToSection(AttackSectionName, Montage);
```

作用：

- 玩家在蓄力段松开按键时，立即跳转到攻击段。
- 跳转后由 `AttackSectionName` 开头的 `AttackStart` 通知把阶段切到 `Active`。
- 它只表示攻击段入口，不表示固定时间。
- 只有 `bIsChargeMove = true` 时才需要配置。

`SectionName` 和 `AttackSectionName` 不能混淆：

| 字段 | 用途 |
|---|---|
| `SectionName` | 动作开始播放时从哪个 Section 开始 |
| `AttackSectionName` | 蓄力动作松手后跳到哪个攻击 Section |

建议的蓄力蒙太奇结构：

```text
Charge Section                 Attack Section                 Recovery Section
[蓄力]                         [攻击]                         [收招]
                                │
                              松手
                              ── Montage_JumpToSection ──▶
```

如果玩家已经自然播放到攻击段，之后松手只会消费输入，不会再次回跳攻击段。

---

## 3. 武器 DataAsset 配置

打开 `UWeaponDataAsset`，例如：

```text
/Game/MH/Combat/DA_TrainingWeapon
```

### 3.1 基础字段

| 字段 | 说明 |
|---|---|
| `WeaponId` | 武器唯一名称，供蓝图和日志识别 |
| `DisplayName` | 编辑器显示名称 |
| `MeshAsset` | 武器静态网格 |
| `Moves` | 当前武器可用的全部招式数组 |
| `GroundStartMoves` | 地面起手映射 |
| `AirStartMoves` | 空中起手映射 |

### 3.2 `Moves` 与索引

`GroundStartMoves`、`AirStartMoves` 和每个招式的 `ComboChain` 中的 `int32`，都表示 `Moves` 数组的索引。

索引从 `0` 开始：

```text
Moves[0] = 第一招
Moves[1] = 第二招
Moves[2] = 蓄力攻击
```

索引越界时 `UCombatComponent` 找不到招式，连招不会启动。

### 3.3 `FMHCombatMoveData` 字段

| 字段 | 说明 |
|---|---|
| `MoveId` | 招式名称，用于事件和日志 |
| `Montage` | 该招式使用的 `UAnimMontage` |
| `SectionName` | 开始播放时的 Section |
| `MontagePlayRate` | 蒙太奇播放速率 |
| `bIsChargeMove` | 是否启用蓄力动作 |
| `AttackSectionName` | 蓄力松手后跳转的攻击 Section |
| `bCanChain` | 是否允许接下一招 |
| `Damage` | 命中伤害 |
| `HitRange` | 没有命中 Socket 时用于推算攻击原点 |
| `HitRadius` | 命中检测球体半径 |
| `HitOriginSocketName` | 命中检测原点所在 Socket |
| `LaunchImpulse` | 命中后对目标施加的冲量 |
| `ComboChain` | 当前招式可衔接的下一招映射 |

命中检测优先读取 `CurrentMoveData.HitOriginSocketName`；该字段为空时使用 `UCombatComponent` 的 `DefaultHitOriginSocketName`。

---

## 4. `FComboCondition` 配置

`GroundStartMoves`、`AirStartMoves` 和 `ComboChain` 都使用 `TMap<FComboCondition, int32>`。

一个 `FComboCondition` 表示一组输入条件。

### 4.1 字段

| 字段 | 说明 |
|---|---|
| `InputAction` | 绑定的输入动作，例如 `IA_Attack_Y` |
| `TriggerEvent` | `Started`、`Triggered` 或 `Completed` |
| `bCheckMoveDirection` | 是否检查移动方向 |
| `MoveDirectionThreshold` | 方向阈值，例如 `(0, 0.5)` |
| `bCheckHoldDuration` | 是否检查按住时长 |
| `MinHoldDuration` | 最短按住时长 |

### 4.2 匹配语义

同一组条件中，所有启用项必须同时满足：

```text
InputAction 匹配
AND TriggerEvent 匹配
AND 移动方向匹配
AND 按住时长匹配
```

示例：

```text
InputAction = IA_Attack_Y
TriggerEvent = Started
bCheckMoveDirection = false
bCheckHoldDuration = false
```

表示“按下 `IA_Attack_Y` 时触发”。

另一个示例：

```text
InputAction = IA_Attack_B
TriggerEvent = Completed
bCheckMoveDirection = false
bCheckHoldDuration = true
MinHoldDuration = 0.6
```

表示“按住 `IA_Attack_B` 至少 0.6 秒后松手时触发”。

### 4.3 方向判断

`MoveDirectionThreshold` 使用二维向量：

- `X`：角色左右输入。
- `Y`：角色前后输入。
- 正值表示输入必须大于等于阈值。
- 负值表示输入必须小于等于阈值。
- 对应轴为 `0` 时不检查该轴。

例如 `(0, 0.5)` 表示前向输入必须大于等于 `0.5`。

### 4.4 注意

- 当前按住时长条件只在 `TriggerEvent = Completed` 时生效。
- 当前蓄力动作只会在 `TriggerEvent = Started` 时记录对应输入动作；蓄力招式应通过 `Started` 条件进入。
- `TMap` 的 Key 是完整条件，两个看起来相似但标志位或数值不同的条件不算同一个 Key。
- 不同条件之间不需要用`或`关系表达：`UCombatComponent` 会遍历所有条件，匹配其中满足的一条即可。
- 尽量避免配置多条同时满足且效果相同的映射，以免索引选择不确定。

---

## 5. 蒙太奇通知配置

### 5.1 两类通知

| 通知类 | 用途 |
|---|---|
| `UMHCombatNotify` | 单帧点事件 |
| `UMHCombatNotifyState` | 区间事件，有 Begin / End |

普通点事件 `UMHCombatNotify` 可配置：

| `NotifyType` | 作用 |
|---|---|
| `AttackStart` | 攻击阶段正式开始 |
| `AttackHit` | 执行伤害判定 |
| `RecoveryStart` | 进入收招阶段 |
| `MoveEnd` | 动作结束，尝试接下一招 |

区间事件 `UMHCombatNotifyState` 可配置：

| `StateType` | 作用 |
|---|---|
| `ComboWindow` | 连击窗口 |
| `WeaponSwitchAllowed` | 允许切武器窗口 |

### 5.2 普通动作推荐时间线

```text
Startup
  ├─ AttackStart            → MovePhase 变为 Active
Active
  ├─ AttackHit              → 执行伤害判定
  ├─ ComboWindow            → 连击窗口开始/结束
  ├─ WeaponSwitchAllowed    → 切武器窗口开始/结束
Recovery
  ├─ RecoveryStart          → MovePhase 变为 Recovery
  ├─ MoveEnd                → 结束或接下一招
```

建议：

- `AttackStart` 放在动作真正进入攻击段的位置。
- `AttackHit` 放在武器命中帧。
- `AttackHit` 必须位于 `AttackStart` 之后。
- 当前每个招式只执行一次伤害判定。
- `ComboWindow` 通常放在攻击判定和收招之间。
- `WeaponSwitchAllowed` 只放在业务上允许切武器的时间段。
- `MoveEnd` 放在动作末尾。
- 没有配置 `AttackStart` 时，阶段会一直停在 `Startup`，`AttackHit` 不会生效。

### 5.3 蓄力动作推荐时间线

蓄力动作建议使用三个 Section：

```text
Charge Section
Attack Section
Recovery Section
```

配置：

| 字段 | 示例值 |
|---|---|
| `bIsChargeMove` | `true` |
| `SectionName` | `Charge` |
| `AttackSectionName` | `Attack` |

蒙太奇通知：

```text
Charge Section
  无 AttackStart

Release
  Montage_JumpToSection("Attack")

Attack Section
  AttackStart
  AttackHit
  ComboWindow
  WeaponSwitchAllowed

Recovery Section
  RecoveryStart
  MoveEnd
```

注意事项：

- `AttackStart` 不能放在蓄力 Section 开头，否则按钮还没松开就会进入攻击阶段。
- 蓄力招式应由 `TriggerEvent = Started` 的起手或连招条件进入，组件才能识别对应的松手输入。
- 如果通过 `Triggered` 或 `Completed` 条件进入蓄力招式，当前实现无法自动执行“松手跳转攻击段”。
- `Release` 后跳转到 `AttackSectionName` 指向的 Section。
- `AttackHit` 放在该攻击 Section 的实际命中位置。
- 如果玩家已经进入攻击段后才松手，当前行为是只消费松手输入，不会重播攻击段。

---

## 6. `UCombatComponent` 配置

在角色或 Pawn 上打开 `UCombatComponent`：

| 字段 | 说明 |
|---|---|
| `DefaultWeapon` | 默认装备武器 |
| `LoadoutWeapons` | 可用武器列表 |
| `DefaultHitOriginSocketName` | 默认命中 Socket |
| `bLockGroundMovementDuringAttack` | 地面攻击期间是否锁定移动 |
| `AttackInputBufferDuration` | 连击输入缓冲时长 |
| `DamageMultiplier` | 伤害乘数 |

`AttackInputBufferDuration` 当前语义：

- 连击窗口开始前，符合条件的输入会被缓存。
- 窗口开启时立即尝试接招。
- 窗口关闭后，缓存输入仍可在该时长内尝试接招。

---

## 7. 地面与空中起手

当前逻辑通过 `UCharacterMovementComponent::IsFalling()` 实时判断起手状态：

| 状态 | 使用的映射 |
|---|---|
| 地面 | `GroundStartMoves` |
| 空中 | `AirStartMoves` |

当前只区分“起手”，连招中的 `ComboChain` 不区分地面和空中。

如果角色在攻击中途进入空中或落地，只会继续使用当前招式已配置的 `ComboChain`。

---

## 8. 配置示例

假设武器有四个招式：

```text
Moves[0] = NormalAttack
Moves[1] = HeavyAttack
Moves[2] = ChargeAttack
Moves[3] = AirAttack
```

地面起手：

```text
GroundStartMoves
  Key: IA_Attack_Y, Started
  Value: 0
```

空中起手：

```text
AirStartMoves
  Key: IA_Attack_Y, Started
  Value: 3
```

普通攻击连招：

```text
Moves[0].ComboChain
  Key: IA_Attack_Y, Started
  Value: 1
```

普通攻击接蓄力：

```text
Moves[1].ComboChain
  Key: IA_Attack_B, Started
  Value: 2
```

蓄力招式：

```text
Moves[2]
  Montage = ChargeAttackMontage
  SectionName = Charge
  bIsChargeMove = true
  AttackSectionName = Attack
```

---

## 9. 调试检查表

| 问题 | 检查位置 |
|---|---|
| 招式没有播放 | 检查 `Montage`、`SectionName`、`Moves` 索引 |
| `AttackHit` 无效 | 检查 `AttackStart` 是否先触发、`MovePhase` 是否为 `Active` |
| 伤害没有产生 | 检查目标是否实现 `IMHCombatTargetInterface`、Socket 是否存在 |
| 连招没有接上 | 检查 `ComboChain` 、`ComboWindow`、`bCanChain` |
| 蓄力不跳转 | 检查 `bIsChargeMove`、`AttackSectionName` 是否与蒙太奇 Section 完全一致 |
| 蓄力提前攻击 | 检查 `AttackStart` 是否错误地放到了蓄力 Section |
| 空中起手未生效 | 检查 `AirStartMoves`、当前角色的 `CharacterMovement::IsFalling()` |
| 无法切武器 | 检查 `WeaponSwitchAllowed` 区间是否覆盖当前时间点 |

---

## 10. 旧资产迁移

本次通知类型已经变化：

- 原来的 `ComboWindowOpen`、`ComboWindowClose` 不再用于普通通知。
- 区间窗口、切武器窗口改用 `UMHCombatNotifyState`。
- 普通点事件使用 `UMHCombatNotify`。

旧蒙太奇中的通知需要重新添加，不能仅依赖旧的 `CombatSystem.md` 或自动迁移。

推荐命名：

```text
普通蒙太奇
  AttackStart
  AttackHit
  RecoveryStart
  MoveEnd

区间蒙太奇
  ComboWindow
  WeaponSwitchAllowed
```
