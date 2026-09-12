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
	int32 ClientInputSequence = INDEX_NONE;
};

struct FMHCombatPredictedMove
{
	int32 InputSequence = INDEX_NONE;
	FSoftObjectPath WeaponPath;
	int32 MoveIndex = INDEX_NONE;
	FName SectionName = NAME_None;
	float PlayRate = 1.f;
	float StartTime = 0.f;
	UAnimMontage* Montage = nullptr;

	bool bHadPreviousMove = false;
	FMHCombatMoveData PreviousMoveData;
	int32 PreviousMoveIndex = INDEX_NONE;
	FName PreviousMoveId = NAME_None;
	UAnimMontage* PreviousMontage = nullptr;
	float PreviousMontagePosition = 0.f;
	float PreviousEffectivePlayRate = 1.f;
	bool bPreviousChargeMove = false;
	bool bPreviousCharging = false;
	bool bPreviousChargeInputHeld = false;
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

	/** 关闭后不再接受输入、不再开新动作；进行中的动作会被中断（服务器）。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SetCombatEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsCombatEnabled() const { return bCombatEnabled; }

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Network", meta = (ClampMin = "0.1"))
	float MovePredictionTimeout = 0.75f;

	/** 蓄力阶段蒙太奇的减速倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ChargePlayRateScale = 0.3f;

	/** 服务器向客户端同步蒙太奇播放位置的间隔（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Network", meta = (ClampMin = "0.0"))
	float ActionPositionSyncInterval = 0.1f;

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

	/** 权威动作状态：复制给所有客户端（含本机控制的客户端，用于确认本地预测）。 */
	UPROPERTY(ReplicatedUsing = OnRep_ActionState, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	FMHCombatActionState ActionState;

	/** 由 ActionState 派生，客户端在 OnRep 里写入。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	FName CurrentMoveId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	float CurrentMoveTime = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
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
	bool bHitWindowActive = false;
	bool bHasPreviousHitOrigin = false;
	FVector PreviousHitOrigin = FVector::ZeroVector;
	bool bWeaponSwitchAllowed = false;
	bool bMontageDelegatesBound = false;
	bool bMovementLocked = false;
	EMovementMode SavedMovementMode = MOVE_Walking;
	TArray<TWeakObjectPtr<AActor>> HitActorsThisMove;

private:
	UFUNCTION(Server, Reliable)
	void Server_HandleComboInput(const FSoftObjectPath& InputActionPath, ETriggerEvent TriggerEvent, FVector2D MoveInput, float HoldDuration, int32 ClientInputSequence);

	/** 服务器：自增 Sequence 并立即在本地应用（服务器不会收到自己的 OnRep）。 */
	void CommitActionState();

	/** 把权威动作状态应用到表现层：播片、停片、对齐进度、蓄力速率。 */
	void ApplyActionState();

	/** 同一动作内的增量刷新：只修正蓄力与进度，不会重播蒙太奇。 */
	void SyncActionPresentation();

	void ApplyChargePresentation();
	void ClearPresentationState(bool bInterrupted);
	UWeaponDataAsset* ResolveCurrentWeaponFromPath();

	bool StartMove(const FMHCombatMoveData& Move, int32 MoveIndex, UInputAction* SourceInputAction, int32 ClientInputSequence);
	bool TryStartAttack(const FMHCombatInputSnapshot& Input);
	bool TryPredictMove(const FMHCombatInputSnapshot& Input, bool bChargeRelease);
	void ConfirmPredictedMove();
	void CancelPredictedMove(bool bTimedOut, bool bRestorePrevious = true);
	bool BufferNextCombo(const FMHCombatInputSnapshot& Input);
	bool TryStartNextCombo();
	const FMHCombatMoveData* GetMove(int32 MoveIndex) const;
	int32 FindBestComboIndex(const TMap<FComboCondition, int32>& ComboMoves, const FMHCombatInputSnapshot& Input) const;
	bool MatchesComboCondition(const FComboCondition& Condition, const FMHCombatInputSnapshot& Input) const;
	void ClearBufferedComboInput();
	void FinishCurrentMove(bool bInterrupted);
	void CacheOwnerReferences();
	void PlayMovePresentation(UWeaponDataAsset* MoveWeapon, int32 MoveIndex, FName SectionName, float PlayRate, float StartPosition = 0.f);
	void UpdateWeaponMesh(UWeaponDataAsset* NewWeapon);
	void ReleaseCharge();
	void HandleAttackStart();
	void PerformHitCheck();
	void BeginHitWindow();
	void EndHitWindow();
	void PerformHitSweep();
	FVector ResolveHitOrigin() const;
	bool PerformHitQuery(const FVector& Start, const FVector& End);
	void DrawDebugHitSweep(const FVector& Start, const FVector& End, float Radius, bool bHit) const;
	bool TryApplyHit(AActor* Target, const FVector& HitLocation, const FVector& HitNormal);
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
	void OnRep_CombatState() const;

	UFUNCTION()
	void OnRep_ActionState();

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
	int32 AppliedActionSequence = 0;
	float ActionPositionSyncAccumulator = 0.f;
	int32 LocalInputSequence = 0;
	int32 LastReceivedInputSequence = 0;
	int32 PendingServerInputSequence = INDEX_NONE;
	FMHCombatPredictedMove PendingPredictedMove;
	bool bHasPendingPredictedMove = false;

	/** 本地是否已经播出过一帧动作画面，用于保证 AttackStarted/Ended 成对。 */
	bool bPresentationActive = false;

	/** 死亡等状态下整体关闭战斗，避免继续接受输入。 */
	bool bCombatEnabled = true;

	/** 本机控制的客户端已预测松手，在服务器确认前不被旧的蓄力状态拉回去。 */
	bool bChargeReleasePredicted = false;

};
