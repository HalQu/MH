#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputComponent.h"
#include "MHCombatTypes.generated.h"

class AActor;
class UAnimMontage;
class UInputAction;
class UWeaponDataAsset;
class UCameraShakeBase;
class UParticleSystem;
class USoundBase;

UENUM(BlueprintType)
enum class EMHCombatState : uint8
{
	Locomotion UMETA(DisplayName = "Locomotion"),
	Attack UMETA(DisplayName = "Attack"),
	WeaponSwitch UMETA(DisplayName = "Weapon Switch")
};

UENUM(BlueprintType)
enum class EMHCombatMovePhase : uint8
{
	None UMETA(DisplayName = "None"),
	Startup UMETA(DisplayName = "Startup"),
	Charge UMETA(DisplayName = "Charge"),
	Active UMETA(DisplayName = "Active"),
	Recovery UMETA(DisplayName = "Recovery")
};

/**
 * 连招窗口状态。由蒙太奇上的 ComboWindow 动画通知状态驱动。
 * 服务器写入权威值；自主代理用本地蒙太奇预测，并在权威快照到达时重放校正。
 */
UENUM(BlueprintType)
enum class EMHCombatComboWindowState : uint8
{
	/** 没有动作在播放，不接受连招输入。 */
	Closed UMETA(DisplayName = "Closed"),

	/** 动作已开始但连招窗口还没开：输入可以缓冲，但不会立刻启动下一招。 */
	Pending UMETA(DisplayName = "Pending"),

	/** 窗口开启中：输入立刻启动下一招。 */
	Open UMETA(DisplayName = "Open"),

	/** 窗口刚关闭，仍处于 AttackInputBufferDuration 的宽限期内。 */
	Buffered UMETA(DisplayName = "Buffered")
};

UENUM(BlueprintType)
enum class EMHCombatNotifyType : uint8
{
	AttackStart UMETA(DisplayName = "Attack Start"),
	AttackHit UMETA(DisplayName = "Attack Hit"),
	RecoveryStart UMETA(DisplayName = "Recovery Start"),
	MoveEnd UMETA(DisplayName = "Move End")
};

UENUM(BlueprintType)
enum class EMHCombatNotifyStateType : uint8
{
	ChargeWindow UMETA(DisplayName = "Charge Window"),
	ComboWindow UMETA(DisplayName = "Combo Window"),
	WeaponSwitchAllowed UMETA(DisplayName = "Weapon Switch Allowed"),
	AttackHitWindow UMETA(DisplayName = "Attack Hit Window")
};

UENUM(BlueprintType)
enum class EMHCombatNotifyStateEvent : uint8
{
	Begin UMETA(DisplayName = "Begin"),
	End UMETA(DisplayName = "End")
};

/** 受击方向，用于从同一 ReactionId 中选择不同蒙太奇。 */
UENUM(BlueprintType)
enum class EMHHitReactionDirection : uint8
{
	Front UMETA(DisplayName = "Front"),
	Back UMETA(DisplayName = "Back"),
	Left UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right")
};

/** 一次受击反应的蒙太奇配置。 */
USTRUCT(BlueprintType)
struct MH_API FMHHitReactionMontage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction")
	FName SectionName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction", meta = (ClampMin = "0.01"))
	float PlayRate = 1.f;
};

/** 单个 ReactionId 的受击配置。方向蒙太奇为空时使用 DefaultMontage。 */
USTRUCT(BlueprintType)
struct MH_API FMHHitReactionDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction")
	FMHHitReactionMontage DefaultMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction")
	TMap<EMHHitReactionDirection, FMHHitReactionMontage> DirectionalMontages;

	/** 仅影响击退速度，不影响伤害。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction", meta = (ClampMin = "0.0"))
	float LaunchScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction")
	bool bLockMovement = true;
};

/** 复制到所有客户端的权威受击状态。客户端只看该状态播放表现，不自行决定受击。 */
USTRUCT(BlueprintType)
struct MH_API FMHHitReactionState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	int32 Sequence = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	FName ReactionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	EMHHitReactionDirection Direction = EMHHitReactionDirection::Front;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	TObjectPtr<AActor> Source = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	FVector HitLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	FVector HitNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	float HitStunDuration = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	float StartServerTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	float EndServerTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|HitReaction")
	bool bSuperArmor = false;
};

/** 动作命中时由攻击者在本地生成的客户端表现配置。所有资源都可为空。 */
USTRUCT(BlueprintType)
struct MH_API FMHCombatHitFeedback
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	TSoftObjectPtr<UParticleSystem> ImpactParticle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	TSoftObjectPtr<USoundBase> ImpactSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback", meta = (ClampMin = "0.0"))
	float CameraShakeScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback", meta = (ClampMin = "0.0"))
	float HapticFeedbackStrength = 0.5f;
};

/** 伤害接口返回的结构化结果。服务器用实际结果生成命中事件。 */
USTRUCT(BlueprintType)
struct MH_API FMHDamageResult
{
	GENERATED_BODY()

	/** 命中请求已经被目标处理；即使因无敌实际伤害为 0，也为 true。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Damage")
	bool bHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Damage")
	bool bInvulnerable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Damage")
	bool bSuperArmorBlocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Damage")
	bool bReactionStarted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Damage")
	bool bInterruptedTarget = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Damage")
	float AppliedDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Damage")
	float RemainingHealth = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Damage")
	bool bKilled = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Damage")
	FName HitReactionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Damage")
	EMHHitReactionDirection ReactionDirection = EMHHitReactionDirection::Front;
};

/** 服务器确认命中后发送给客户端的稀疏事件。客户端只消费，不据此计算伤害。 */
USTRUCT(BlueprintType)
struct MH_API FMHCombatHitEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	int32 HitId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	TObjectPtr<AActor> Attacker = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	TObjectPtr<AActor> Target = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	TObjectPtr<UWeaponDataAsset> Weapon = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	int32 MoveIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	FName MoveId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	FName HitReactionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	EMHHitReactionDirection ReactionDirection = EMHHitReactionDirection::Front;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	FVector HitLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	FVector HitNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	float AppliedDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	float RemainingHealth = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	bool bKilled = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	bool bInvulnerable = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	bool bSuperArmorBlocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Hit")
	FMHCombatHitFeedback Feedback;
};

/**
 * 一次战斗输入命令。
 * 客户端按下/松开按键时生成：先本地执行一次（预测），再可靠发送给服务器。
 * 服务器处理后回报已处理序号，客户端用未确认命令做回滚重放。
 */
USTRUCT(BlueprintType)
struct MH_API FMHCombatInputCommand
{
	GENERATED_BODY()

	/** 客户端自增序号，服务器按序处理并回报。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
	int32 Sequence = INDEX_NONE;

	/** 触发输入的 Enhanced Input 动作（用软引用收发，避免对象引用相关问题）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
	TSoftObjectPtr<UInputAction> InputAction;

	/** Started / Triggered / Completed。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
	ETriggerEvent TriggerEvent = ETriggerEvent::None;

	/** 输入发生瞬间的移动输入，用于方向条件判定。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
	FVector2D MoveInput = FVector2D::ZeroVector;

	/** 按键持续时长（Completed/Triggered 时有效），用于长按条件判定。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
	float HoldDuration = 0.f;

	/** 按下瞬间是否在空中：起手动作按这个值选表，保证服务器与本地回放一致。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
	bool bAirborne = false;

	/** 客户端世界时间（秒），用于本地重放时判定时间相关的缓冲窗口。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
	float ClientWorldTime = 0.f;

	/** 解析出输入动作对象；拿不到时返回 nullptr。 */
	UInputAction* ResolveInputAction() const;

	bool IsValidInput() const { return !InputAction.IsNull() && TriggerEvent != ETriggerEvent::None; }
};

/**
 * 权威动作状态：服务器写入并复制给所有客户端。
 * 它同时是“当前动作表现”和“逻辑状态快照”：客户端先恢复成这份状态，
 * 再重放尚未被服务器确认的输入命令，即可既与服务器一致、又保持本地响应。
 * 这是状态而不是事件，因此迟到加入、短暂离开相关性、丢包重传后都能自动补齐。
 */
USTRUCT(BlueprintType)
struct MH_API FMHCombatActionState
{
	GENERATED_BODY()

	/** 动作开始/结束时自增；同一动作内的阶段、蓄力、播放位置变化不会自增。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	int32 Sequence = 0;

	/** 触发本次动作的客户端输入序号，用于确认本地预测；服务器自行发起的动作为 INDEX_NONE。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	int32 InputSequence = INDEX_NONE;

	/** false 表示当前没有动作在播放。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	int32 MoveIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	FName SectionName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	float PlayRate = 1.f;

	/** 蓄力中：客户端按 ChargePlayRateScale 减速播放。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	bool bCharging = false;

	/** 当前蓄力窗口是否已经进入（由通知状态驱动，重放需要它来复现）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	bool bChargeWindowActive = false;

	/** 仅在 bActive 由 true 变 false 时有意义。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	bool bInterrupted = false;

	/** 服务器上蒙太奇的播放位置，用于迟到加入或重新进入相关性时对齐进度。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	float MontagePosition = 0.f;

	// ---- 以下字段与上面的动作字段一起，构成可回滚的完整逻辑状态快照 ----

	/** 当前战斗状态（Locomotion / Attack / WeaponSwitch）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	EMHCombatState CombatState = EMHCombatState::Locomotion;

	/** 当前动作所处阶段（起手 / 蓄力 / 攻击 / 收招）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	EMHCombatMovePhase MovePhase = EMHCombatMovePhase::None;

	/** 连招窗口状态。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	EMHCombatComboWindowState ComboWindowState = EMHCombatComboWindowState::Closed;

	/** 连招窗口关闭后剩余的输入缓冲秒数（仅 Buffered 状态有意义）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	float ComboWindowTimeRemaining = 0.f;

	/** 窗口关闭时服务器上的蒙太奇位置；客户端用它换算本地剩余宽限时间。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	float ComboWindowClosePosition = 0.f;

	/** 服务器是否仍认为蓄力输入处于按下状态。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	bool bChargeInputHeld = false;

	/** 触发蓄力的输入动作，用于判定“同一个按键松开”才释放蓄力。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	TSoftObjectPtr<UInputAction> ChargeInputAction;

	/** 当前动作是否已经缓存了一个连招输入。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	bool bHasBufferedInput = false;

	/** 缓存的连招输入（回滚重放要能复现它，因此必须一起复制）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	FMHCombatInputCommand BufferedInput;

	/** 当前是否处于允许切换武器的窗口（动画通知状态驱动，客户端表现与预测共用）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	bool bWeaponSwitchAllowed = false;
};

USTRUCT(BlueprintType)
struct MH_API FComboCondition
{
	GENERATED_BODY()

	FComboCondition() = default;

	FComboCondition(UInputAction* _InputAction, ETriggerEvent _TriggerEvent, FVector2D _MoveDirectionThreshold, float _Duration, int32 _Priority = 0) :
		InputAction(_InputAction),
		TriggerEvent(_TriggerEvent),
		Priority(_Priority),
		bCheckMoveDirection(true),
		MoveDirectionThreshold(_MoveDirectionThreshold),
		bCheckHoldDuration(true),
		MinHoldDuration(_Duration)
	{
	};

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ETriggerEvent TriggerEvent = ETriggerEvent::None; // Pressed / Released / None(忽略)

	/**
	 * 多个条件同时匹配时，数值更大的优先。
	 * 同优先级时才按条件具体程度和稳定排序键决定，保证服务器与客户端预测结果一致。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCheckMoveDirection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D MoveDirectionThreshold = FVector2D::ZeroVector; // 例如 (0, 0.5) 表示前向 > 0.5

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCheckHoldDuration = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MinHoldDuration = 0.0f;

	// 1. 重载相等运算符 (TMap 查找键时使用)
	bool operator==(const FComboCondition& Other) const
	{
		if (InputAction != Other.InputAction) return false;
		if (TriggerEvent != Other.TriggerEvent) return false;
		if (Priority != Other.Priority) return false;

		if (bCheckMoveDirection != Other.bCheckMoveDirection) return false;
		if (bCheckHoldDuration != Other.bCheckHoldDuration) return false;

		if (bCheckMoveDirection && MoveDirectionThreshold != Other.MoveDirectionThreshold) return false;
		if (bCheckHoldDuration && MinHoldDuration != Other.MinHoldDuration) return false;

		return true;
	}

	// 2. 重载哈希计算函数 (TMap 计算桶索引时使用)
	friend uint32 GetTypeHash(const FComboCondition& InCondition)
	{
		uint32 Hash = 0;

		// 计算必填字段的哈希
		Hash = HashCombine(Hash, GetTypeHash(InCondition.InputAction.Get()));
		Hash = HashCombine(Hash, GetTypeHash(InCondition.TriggerEvent));
		Hash = HashCombine(Hash, GetTypeHash(InCondition.Priority));
		Hash = HashCombine(Hash, GetTypeHash(InCondition.bCheckMoveDirection));
		Hash = HashCombine(Hash, GetTypeHash(InCondition.bCheckHoldDuration));

		// 仅当启用对应标志时，才将相关数值纳入哈希计算
		if (InCondition.bCheckMoveDirection)
		{
			Hash = HashCombine(Hash, GetTypeHash(InCondition.MoveDirectionThreshold));
		}
		if (InCondition.bCheckHoldDuration)
		{
			Hash = HashCombine(Hash, GetTypeHash(InCondition.MinHoldDuration));
		}

		return Hash;
	}
};

USTRUCT(BlueprintType)
struct MH_API FMHCombatMoveData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Move")
	FName MoveId = TEXT("Attack");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Move")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Move")
	FName SectionName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Move", meta = (ClampMin = "0.01"))
	float MontagePlayRate = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Charge")
	bool bIsChargeMove = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Charge", meta = (EditCondition = "bIsChargeMove"))
	FName AttackSectionName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Move")
	bool bCanChain = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Hit", meta = (ClampMin = "0.0"))
	float Damage = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Hit", meta = (ClampMin = "0.0"))
	float HitRange = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Hit", meta = (ClampMin = "0.0"))
	float HitRadius = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Hit")
	FName HitOriginSocketName = TEXT("weapon_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Hit")
	FVector LaunchImpulse = FVector::ZeroVector;

	/** 目标按该标识选择受击反应；未配置映射时仍执行硬直等逻辑。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Hit")
	FName HitReactionId = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Hit", meta = (ClampMin = "0.0"))
	float HitStunDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Hit", meta = (ClampMin = "0.0"))
	float PoiseDamage = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Hit")
	bool bInterruptTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Hit")
	FMHCombatHitFeedback HitFeedback;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Combo")
	TMap<FComboCondition, int32> ComboChain;
};

USTRUCT(BlueprintType)
struct MH_API FMHDamageEvent
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage")
	TObjectPtr<AActor> Source = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage")
	TObjectPtr<UWeaponDataAsset> Weapon = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage", meta = (ClampMin = "0.0"))
	float Damage = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage")
	FVector HitLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage")
	FVector HitNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage")
	FVector LaunchImpulse = FVector::ZeroVector;

	/** 服务器生成的稳定命中序号，用于客户端去重和调试追踪。 */
	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage")
	int32 HitId = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage")
	int32 MoveIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage")
	FName MoveId = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage")
	FName HitReactionId = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage", meta = (ClampMin = "0.0"))
	float PoiseDamage = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage", meta = (ClampMin = "0.0"))
	float HitStunDuration = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Combat|Damage")
	bool bInterruptTarget = true;
};
