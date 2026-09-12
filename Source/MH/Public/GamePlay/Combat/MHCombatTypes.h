#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputComponent.h"
#include "MHCombatTypes.generated.h"

class AActor;
class UAnimMontage;
class UWeaponDataAsset;

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
	WeaponSwitchAllowed UMETA(DisplayName = "Weapon Switch Allowed")
};

UENUM(BlueprintType)
enum class EMHCombatNotifyStateEvent : uint8
{
	Begin UMETA(DisplayName = "Begin"),
	End UMETA(DisplayName = "End")
};

/**
 * 权威动作状态：服务器写入并复制给所有客户端，客户端据此驱动蒙太奇表现。
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

	/** 仅在 bActive 由 true 变 false 时有意义。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	bool bInterrupted = false;

	/** 服务器上蒙太奇的播放位置，用于迟到加入或重新进入相关性时对齐进度。 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Action")
	float MontagePosition = 0.f;
};

USTRUCT(BlueprintType)
struct FComboCondition
{
	GENERATED_BODY()

	FComboCondition() = default;

	FComboCondition(UInputAction* _InputAction, ETriggerEvent _TriggerEvent, FVector2D _MoveDirectionThreshold, float _Duration) :
		InputAction(_InputAction),
		TriggerEvent(_TriggerEvent),
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
};
