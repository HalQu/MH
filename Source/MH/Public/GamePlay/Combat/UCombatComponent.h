#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UCombatComponent.generated.h"

class ACharacter;
class UAnimInstance;
class UAnimMontage;
class UCharacterMovementComponent;
class UInputAction;
class USkeletalMeshComponent;
class UWeaponDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatWeaponChanged, UWeaponDataAsset*, NewWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMHCombatStateChanged, EMHCombatState, NewState, EMHCombatMovePhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatAttackStarted, FName, MoveId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatAttackEnded, bool, bInterrupted);

struct FMHCombatInputSnapshot
{
	UInputAction* InputAction = nullptr;
	ETriggerEvent TriggerEvent = ETriggerEvent::None;
	FVector2D MoveInput = FVector2D::ZeroVector;
	float HoldDuration = 0.f;
};

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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool HandleComboInput(UInputAction* InputAction, ETriggerEvent TriggerEvent);

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
	void HandleCombatNotify(EMHCombatNotifyType NotifyType, UAnimMontage* SourceMontage);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void HandleCombatNotifyState(EMHCombatNotifyStateType StateType, EMHCombatNotifyStateEvent StateEvent, UAnimMontage* SourceMontage);

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


	void OnMove(const FVector2D& MoveInput);

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

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponPath, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	FSoftObjectPath CurrentWeaponPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	TObjectPtr<UStaticMeshComponent> CurrentWeaponMesh = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_CombatState, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	EMHCombatState CombatState = EMHCombatState::Locomotion;

	UPROPERTY(ReplicatedUsing = OnRep_CombatState, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	EMHCombatMovePhase MovePhase = EMHCombatMovePhase::None;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	FName CurrentMoveId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	float CurrentMoveTime = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentMoveIndex, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 CurrentMoveIndex = INDEX_NONE;
	int32 CurrentWeaponIndex = INDEX_NONE;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
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

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CurrentChargeInputAction = nullptr;

	bool bHitExecuted = false;
	bool bWeaponSwitchAllowed = false;
	bool bMontageDelegatesBound = false;
	bool bMovementLocked = false;
	EMovementMode SavedMovementMode = MOVE_Walking;
	TArray<TWeakObjectPtr<AActor>> HitActorsThisMove;

private:
	UFUNCTION(Server, Reliable)
	void Server_HandleComboInput(const FSoftObjectPath& InputActionPath, ETriggerEvent TriggerEvent, FVector2D MoveInput, float HoldDuration, int32 ClientInputSequence);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayMove(const FSoftObjectPath& WeaponPath, int32 MoveIndex, FName SectionName, float PlayRate);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopMove(const FSoftObjectPath& WeaponPath, int32 MoveIndex);

	bool StartMove(const FMHCombatMoveData& Move, int32 MoveIndex, UInputAction* SourceInputAction);
	bool TryStartAttack(const FMHCombatInputSnapshot& Input);
	bool BufferNextCombo(const FMHCombatInputSnapshot& Input);
	bool TryStartNextCombo();
	const FMHCombatMoveData* GetMove(int32 MoveIndex) const;
	int32 FindBestComboIndex(const TMap<FComboCondition, int32>& ComboMoves, const FMHCombatInputSnapshot& Input) const;
	bool MatchesComboCondition(const FComboCondition& Condition, const FMHCombatInputSnapshot& Input) const;
	void ClearBufferedComboInput();
	void FinishCurrentMove(bool bInterrupted);
	void CacheOwnerReferences();
	void PlayMovePresentation(UWeaponDataAsset* MoveWeapon, int32 MoveIndex, FName SectionName, float PlayRate);
	void StopMovePresentation(UWeaponDataAsset* MoveWeapon, int32 MoveIndex);
	void UpdateWeaponMesh(UWeaponDataAsset* NewWeapon);
	void ReleaseCharge();
	void HandleAttackStart();
	void PerformHitCheck();
	void ApplyDamageToTarget(AActor* Target, const FVector& HitLocation, const FVector& HitNormal);
	float ResolveDamage(const FMHCombatMoveData& MoveData) const;
	void UpdateMovementLock(bool bLock);
	void SetCombatState(EMHCombatState NewState, EMHCombatMovePhase NewPhase);
	void BindMontageDelegates();
	void UnbindMontageDelegates();
	bool IsCurrentMontage(UAnimMontage* Montage) const;
	bool IsComboInputAllowed() const;
	bool CanStartBufferedCombo() const;

	UFUNCTION()
	void OnRep_CurrentWeaponPath();

	UFUNCTION()
	void OnRep_CombatState();

	UFUNCTION()
	void OnRep_CurrentMoveIndex();

	UFUNCTION()
	void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);


	FVector2D CurrentMoveInput;
	FMHCombatInputSnapshot BufferedComboInput;
	bool bHasBufferedComboInput = false;
	bool bComboWindowPending = false;
	bool bComboWindowClosed = false;
	float ComboWindowCloseTime = 0.f;
	TMap<UInputAction*, float> AttackInputPressTimes;
	bool bIsChargeMove = false;
	bool bIsCharging = false;
	bool bChargeInputHeld = false;
	float PendingServerHoldDuration = -1.f;
	int32 LocalInputSequence = 0;
	int32 LastReceivedInputSequence = 0;

};
