# MH 战斗系统架构

> 当前实现的是第一版战斗骨架：一个通用的 `UCombatComponent` 状态机 + 数据驱动的 `UWeaponDataAsset`。
> 角色基础移动仍由 `ACharacter` / `UCharacterMovementComponent` 负责，`UPlayerAnimInstance` 只读取战斗状态作为动画输入。
> 引擎版本：UE 5.8。

---

## 1. 职责划分

| 模块 | 职责 |
|---|---|
| `AMHCharacter` | 输入入口、持有 `UCombatComponent`，负责向战斗组件转发输入 |
| `UCombatComponent` | **战斗权威**：连招状态机、输入缓冲、允许取消窗口、武器切换、命中与伤害计算 |
| `UWeaponDataAsset` | 被动的武器定义：地面连招、空中连招、伤害与 Montage 引用 |
| `FMHCombatMoveData` | 单招：时序、命中时机、Combo Window、Cancel Window、伤害、命中范围 |
| `IMHCombatTargetInterface` | 伤害目标统一接口；怪物、NPC、可破坏物实现 `ReceiveDamage` |
| `UMHCombatNotify` | 蒙太奇时间轴 → 战斗组件的回调桥 |
| `UPlayerAnimInstance` | 表现层：从战斗组件读取 `CombatState`、`MovePhase`、`CurrentWeaponId` |

---

## 2. 数据流

```text
Input
  → AMHCharacter
     → UCombatComponent::TryAttack / CycleWeapon
        ├─ 读取 UWeaponDataAsset 的 GroundCombo / AirCombo
        ├─ 驱动 ACharacter::PlayAnimMontage
        ├─ 接收 UMHCombatNotify 或时序回退
        ├─ Overlap 命中查询
        └─ IMHCombatTargetInterface::ReceiveDamage
             → UPlayerAnimInstance 读取战斗状态
```

关键约束：

- 战斗状态机只存在一份，位于 `UCombatComponent`。
- 武器不会各写一套连招逻辑，只提供数据。
- 蒙太奇不是状态机权威。即使没有配置 Montage，时序字段也能让状态机完成起手、判定、收招和连招。
- `UPlayerAnimInstance` 只读取状态，不修改战斗状态。

---

## 3. 当前已实现

- 地面连招与空中连招
- 攻击输入缓冲
- `ComboWindowStart / ComboWindowEnd` 内接下一招
- `CancelWindowStart / CancelWindowEnd` 内允许切武器
- 同一个 Montage 内任意攻击只命中同一目标一次
- 切武器时自动取消当前攻击
- 攻击期间锁定地面移动，空中攻击不强制中断空中移动
- 伤害事件通过 `IMHCombatTargetInterface` 发送
- 四个蓝图事件：`OnWeaponChanged`、`OnCombatStateChanged`、`OnAttackStarted`、`OnAttackEnded`

---

## 4. 配置步骤

### 4.1 角色

1. 打开 `BP_MHCharacter` 或当前玩家 Pawn，确认已添加 `UCombatComponent`。
2. 在 `Default Weapon` 或 `Loadout Weapons` 中放入武器数据资产。
3. 配置输入：
   - `IA_MH_Attack`
   - `IA_MH_Jump`
   - `IA_MH_NextWeapon`
   - `IA_MH_PreviousWeapon`
4. 把这些 Input Action 加入控制器的 Input Mapping Context。

### 4.2 武器 DataAsset

创建 `UWeaponDataAsset`，例如：

```text
/Game/MH/Combat/DA_TrainingWeapon
```

至少配置：

- `WeaponId`
- `DisplayName`
- `GroundCombo[0]`
- `AirCombo[0]`

每个 `FMHCombatMoveData` 可配置：

- `Montage`、`SectionName`、`MontagePlayRate`
- `StartupTime` / `ActiveTime` / `RecoveryTime`
- `HitMoment`
- `ComboWindowStart` / `ComboWindowEnd`
- `CancelWindowStart` / `CancelWindowEnd`
- `Damage`、`HitRange`、`HitRadius`、`LaunchImpulse`
- `HitOriginSocketName`

### 4.3 蒙太奇 Notify

在 Montage 中添加 `UMHCombatNotify`，建议位置：

| NotifyType | 建议时机 |
|---|---|
| `AttackHit` | 武器命中帧 |
| `ComboWindowOpen` | 可接下一击开始 |
| `ComboWindowClose` | 可接下一击结束 |
| `RecoveryStart` | 收招开始 |
| `WeaponSwitchAllowed` | 允许切武器帧 |

> 如果暂未配置 Notify，组件会使用 `FMHCombatMoveData` 中的时序字段自动推进状态。

### 4.4 伤害目标

怪物、敌人或可破坏物实现 `IMHCombatTargetInterface`：

```cpp
UCLASS()
class ATestEnemy : public AActor, public IMHCombatTargetInterface
{
    GENERATED_BODY()

public:
    virtual void ReceiveDamage_Implementation(const FMHDamageEvent& DamageEvent) override;
};
```

---

## 5. 下一阶段

- 把 `AWeaponActor` 作为视觉/挂载层加入，不承担连招状态机。
- 加入武器切出/切入 Montage 与 `WeaponSwitch` 状态。
- 将伤害结算扩展到部位、部位倍率、暴击、异常。
- 如果进入多人 PvE，再把 `UCombatComponent` 设为 Replicated，并用 Server RPC 执行状态机与伤害。
- 当招式规模明显变大时，评估迁移到 Gameplay Ability System（GAS）。
