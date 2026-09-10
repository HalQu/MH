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
