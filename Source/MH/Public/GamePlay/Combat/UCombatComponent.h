#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UCombatComponent.generated.h"

class ACharacter;
class UAnimInstance;
class UAnimMontage;
class UCharacterMovementComponent;
class USkeletalMeshComponent;
class UWeaponDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatWeaponChanged, UWeaponDataAsset*, NewWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMHCombatStateChanged, EMHCombatState, NewState, EMHCombatMovePhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatAttackStarted, FName, MoveId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatAttackEnded, bool, bInterrupted);

/*
 * Authoritative combat state machine for one character.
 * Movement, animation and weapons are presentation/data layers driven from here.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent), BlueprintType)
class MH_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatComponent();

	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool TryAttack(bool bForceAirAttack = false);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool TryJumpAttack();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool CycleWeapon(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool EquipWeapon(UWeaponDataAsset* NewWeapon);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool EquipWeapon_Default();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void CancelCurrentAttack();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool AddWeaponToLoadout(UWeaponDataAsset* Weapon);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool RemoveWeaponFromLoadout(UWeaponDataAsset* Weapon);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void HandleCombatNotify(EMHCombatNotifyType NotifyType);

	UFUNCTION(BlueprintPure, Category = "Combat")
	UWeaponDataAsset* GetCurrentWeapon() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	FName GetCurrentWeaponId() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	FMHCombatMoveData GetCurrentMoveData() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	EMHCombatState GetCombatState() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	EMHCombatMovePhase GetMovePhase() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetCurrentMoveIndex() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetCurrentMoveTime() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAirborne() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool CanSwitchWeaponNow() const;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatWeaponChanged OnWeaponChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatStateChanged OnCombatStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatAttackStarted OnAttackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatAttackEnded OnAttackEnded;

	//input buffer
	void OnMove(const FVector2D& MoveInput);
	void OnYPressed(UInputAction* InputAction, ETriggerEvent TriggerEvent);
	void OnYReleased(UInputAction* InputAction, ETriggerEvent TriggerEvent);
	void OnBPressed(UInputAction* InputAction, ETriggerEvent TriggerEvent);
	void OnBReleased(UInputAction* InputAction, ETriggerEvent TriggerEvent);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Loadout")
	TObjectPtr<UWeaponDataAsset> DefaultWeapon = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Loadout")
	TArray<TObjectPtr<UWeaponDataAsset>> LoadoutWeapons;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings")
	FName DefaultHitOriginSocketName = TEXT("weapon_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings")
	bool bLockGroundMovementDuringAttack = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings", meta = (ClampMin = "0.0"))
	float AttackInputBufferDuration = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	TObjectPtr<UWeaponDataAsset> CurrentWeapon = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	TObjectPtr<UStaticMeshComponent> CurrentWeaponMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	EMHCombatState CombatState = EMHCombatState::Locomotion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	EMHCombatMovePhase MovePhase = EMHCombatMovePhase::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	FName CurrentMoveId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	float CurrentMoveTime = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 CurrentMoveIndex = INDEX_NONE;
	int32 CurrentWeaponIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	bool bIsAirMove = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	bool bComboWindowOpen = false;

protected:
	UPROPERTY(Transient)
	FMHCombatMoveData CurrentMoveData;

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> CachedCharacter = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> CachedMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CachedMovement = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimInstance> CachedAnimInstance = nullptr;

	bool bAttackInputQueued = false;
	bool bHitExecuted = false;
	bool bWeaponSwitchAllowed = false;
	bool bMontageDelegatesBound = false;
	bool bMovementLocked = false;
	EMovementMode SavedMovementMode = MOVE_Walking;
	TArray<TWeakObjectPtr<AActor>> HitActorsThisMove;

private:
	bool StartMove(const FMHCombatMoveData& Move, bool bAirMove, int32 MoveIndex);
	bool TryStartNextCombo();
	const FMHCombatMoveData* GetMoveForAttack(bool bAirAttack, int32 MoveIndex) const;
	void FinishCurrentMove(bool bInterrupted);
	void UpdateMoveTiming(float DeltaTime);
	void PerformHitCheck();
	void ApplyDamageToTarget(AActor* Target, const FVector& HitLocation, const FVector& HitNormal);
	float ResolveDamage(const FMHCombatMoveData& MoveData) const;
	void UpdateMovementLock(bool bLock);
	void SetCombatState(EMHCombatState NewState, EMHCombatMovePhase NewPhase);
	void BindMontageDelegates();
	void UnbindMontageDelegates();
	bool IsCurrentMontage(UAnimMontage* Montage) const;
	float GetCurrentMoveDuration() const;

	UFUNCTION()
	void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	//input buffer
	FVector2D CurrentMoveInput; 
	bool bYIsPressed = false;
	bool bBIsPressed = false;
	float YPressTime = 0.f;
	float BPressTime = 0.f; 
};
