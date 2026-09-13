#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UObject/SoftObjectPath.h"
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

/**
 * 本地预测与服务器对齐后广播（只在自主代理上触发）。
 * AckedSequence：服务器已经处理到的输入序号。
 * ReplayedCount：这次对齐后重新执行了几条未确认输入。
 * bCorrected：这次对齐是否改变了本机正在播放的动作（也就是玩家会看到一次校正）。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMHCombatPredictionReconciled, int32, AckedSequence, int32, ReplayedCount, bool, bCorrected);

/** 服务器确认命中后，在所有端广播同一份事件。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatHitConfirmed, const FMHCombatHitEvent&, HitEvent);

/*
 * 单个角色的战斗状态机。
 *
 * 网络模型（手写预测 + 回滚重放，不使用 GAS）：
 *  - 服务器是唯一权威：它按序处理客户端输入命令，并把「逻辑状态快照 + 已处理序号」复制下去。
 *  - 自主代理（本机控制的客户端）不直接盲目播放动作，而是：
 *      1. 输入发生时先把命令放进未确认队列，立刻本地执行一次（预测）；
 *      2. 收到权威快照后，把逻辑状态恢复成服务器那一份；
 *      3. 按时间顺序重放队列里所有「尚未被确认」的命令；
 *      4. 最后只对齐一次表现层（蒙太奇、Section、蓄力速率、播放位置）。
 *    这样丢包、乱序、服务器选择不同分支时都能自动收敛，同时保证本地零延迟响应。
 *  - 命中、伤害、死亡仍然只在服务器上发生，本组件不预测它们。
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

	// ------------------------------------------------------------------
	// 输入
	// ------------------------------------------------------------------

	/**
	 * 战斗输入统一入口（Enhanced Input 的 Started / Triggered / Completed 都从这里进来）。
	 * 自主代理会本地预测并发送命令；服务器收到 RPC 后按序执行；模拟代理直接忽略。
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool HandleComboInput(UInputAction* InputAction, ETriggerEvent TriggerEvent);

	/** 由角色转发移动输入，用于方向条件判定与重放。 */
	void OnMove(const FVector2D& MoveInput);

	// ------------------------------------------------------------------
	// 武器
	// ------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool CycleWeapon(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool EquipWeapon(UWeaponDataAsset* NewWeapon);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool EquipWeapon_Default();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool AddWeaponToLoadout(UWeaponDataAsset* Weapon);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool RemoveWeaponFromLoadout(UWeaponDataAsset* Weapon);

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool CanSwitchWeaponNow() const;

	/** 客户端主动请求下一次同步时完整重放未确认输入，主要用于调试和异常恢复。 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Network")
	void RequestPredictionReconcile();

	// ------------------------------------------------------------------
	// 动作控制
	// ------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void CancelCurrentAttack();

	/** 关闭后不再接受输入、不再开新动作；进行中的动作会被中断（服务器）。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SetCombatEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsCombatEnabled() const { return bCombatEnabled; }

	// ------------------------------------------------------------------
	// 动画通知入口
	// ------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void HandleCombatNotify(EMHCombatNotifyType NotifyType, UAnimMontage* SourceMontage);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void HandleCombatNotifyState(EMHCombatNotifyStateType StateType, EMHCombatNotifyStateEvent StateEvent, UAnimMontage* SourceMontage);

	// ------------------------------------------------------------------
	// 查询
	// ------------------------------------------------------------------

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
	EMHCombatComboWindowState GetComboWindowState() const { return ComboWindowState; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetCurrentMoveIndex() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	FName GetCurrentMoveId() const { return CurrentMoveId; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetCurrentMoveTime() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetCurrentMoveLength() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsHitWindowActive() const { return bHitWindowActive; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetActionStateSequence() const { return ActionState.Sequence; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	FMHCombatHitEvent GetLastConfirmedHit() const { return LastConfirmedHitEvent; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAirborne() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsCharging() const { return bIsCharging; }

	/** 服务器已经处理到的输入序号。 */
	UFUNCTION(BlueprintPure, Category = "Combat|Network")
	int32 GetLastProcessedInputSequence() const { return LastProcessedInputSequence; }

	/** 本机还有多少条已经发出、但服务器尚未确认的输入。 */
	UFUNCTION(BlueprintPure, Category = "Combat|Network")
	int32 GetPendingInputCount() const;

	/** 最近一次回滚重放重放了多少条输入。 */
	UFUNCTION(BlueprintPure, Category = "Combat|Network")
	int32 GetLastReplayedInputCount() const { return LastReconcileReplayedCount; }

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatWeaponChanged OnWeaponChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatStateChanged OnCombatStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatAttackStarted OnAttackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatAttackEnded OnAttackEnded;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatPredictionReconciled OnPredictionReconciled;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatHitConfirmed OnHitConfirmed;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Loadout")
	TObjectPtr<UWeaponDataAsset> DefaultWeapon = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Loadout")
	TArray<TObjectPtr<UWeaponDataAsset>> LoadoutWeapons;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings")
	FName DefaultHitOriginSocketName = TEXT("weapon_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings")
	bool bLockGroundMovementDuringAttack = false;

	/** 连招窗口关闭之后，仍然接受连招输入的宽限时间（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings", meta = (ClampMin = "0.0"))
	float AttackInputBufferDuration = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 1.f;

	/** 蓄力阶段蒙太奇的减速倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ChargePlayRateScale = 0.3f;

	// ------------------------------------------------------------------
	// 网络
	// ------------------------------------------------------------------

	/** 关闭后本机不再做预测重放，只跟随服务器状态（排查问题时用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Network")
	bool bEnablePrediction = true;

	/** 未确认输入队列最多保留多少条。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Network", meta = (ClampMin = "4"))
	int32 MaxPredictedInputHistory = 64;

	/** 未确认输入队列最长保留多少秒（防止长时间丢包导致内存增长）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Network", meta = (ClampMin = "0.1"))
	float PredictedInputHistorySeconds = 2.f;

	/** 服务器向客户端同步蒙太奇播放位置的间隔（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Network", meta = (ClampMin = "0.0"))
	float ActionPositionSyncInterval = 0.1f;

	/** 本地预测进度与服务器进度的偏差超过这个值（秒）时，才强行贴合进度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Network", meta = (ClampMin = "0.0"))
	float MontagePositionCorrectionThreshold = 0.35f;

	// ------------------------------------------------------------------
	// 复制状态
	// ------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	TObjectPtr<UWeaponDataAsset> CurrentWeapon = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponPath, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	FSoftObjectPath CurrentWeaponPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	TObjectPtr<UStaticMeshComponent> CurrentWeaponMesh = nullptr;

	/**
	 * 权威逻辑状态快照。服务器写入并复制给所有客户端（含本机控制的客户端）。
	 * 客户端只读：收到后先恢复成这份状态，再重放自己的未确认输入。
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ActionState, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	FMHCombatActionState ActionState;

	/** 服务器已经处理到的客户端输入序号（只复制给 Owner）。 */
	UPROPERTY(ReplicatedUsing = OnRep_LastProcessedInputSequence, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 LastProcessedInputSequence = 0;

	/** 当前战斗状态（服务器权威值 / 客户端预测值）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	EMHCombatState CombatState = EMHCombatState::Locomotion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	EMHCombatMovePhase MovePhase = EMHCombatMovePhase::None;

	/** 连招窗口是否开启（只读镜像，方便蓝图/动画蓝图查询）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	bool bComboWindowOpen = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	FName CurrentMoveId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	float CurrentMoveTime = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 CurrentMoveIndex = INDEX_NONE;

	int32 CurrentWeaponIndex = INDEX_NONE;

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

	// ------------------------------------------------------------------
	// 逻辑状态（服务器写；客户端在回滚重放里读写）
	// ------------------------------------------------------------------

	/** 当前动作要播放的 Section（蓄力松手后会变成 AttackSectionName）。 */
	FName CurrentSectionName = NAME_None;

	/** 当前动作是否属于蓄力动作。 */
	bool bIsChargeMove = false;

	/** 是否正在蓄力（蒙太奇减速）。 */
	bool bIsCharging = false;

	/** 触发蓄力的按键是否仍然被按下。 */
	bool bChargeInputHeld = false;

	/** 触发蓄力的输入动作（软引用，比较路径即可，无需加载）。 */
	TSoftObjectPtr<UInputAction> CurrentChargeInputAction;

	EMHCombatComboWindowState ComboWindowState = EMHCombatComboWindowState::Closed;

	/** 连招窗口关闭时所在的蒙太奇位置（秒）。 */
	float ComboWindowClosePosition = 0.f;

	bool bHasBufferedComboInput = false;
	FMHCombatInputCommand BufferedComboInput;
	bool bChargeWindowActive = false;

	// ------------------------------------------------------------------
	// 本地预测（自主代理）
	// ------------------------------------------------------------------

	/** 已发送给服务器、尚未被确认的输入命令，按序号升序。 */
	TArray<FMHCombatInputCommand> PendingInputCommands;

	int32 LocalInputSequence = 0;
	int32 LastReconciledInputSequence = 0;
	int32 LastReconcileReplayedCount = 0;
	bool bPredictionReconcilePending = false;
	int32 AppliedActionSequence = 0;

	// ------------------------------------------------------------------
	// 表现层
	// ------------------------------------------------------------------

	/** 当前真正在播放的蒙太奇（可能与逻辑层动作不一致，回滚对齐时用来收尾）。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PresentedMontage = nullptr;

	int32 PresentedMoveIndex = INDEX_NONE;
	FName PresentedSectionName = NAME_None;

	/** 当前动作在本地时间轴上的起播时刻，用来在回滚后平滑地对齐进度。 */
	float PredictedMoveStartTime = -1.f;

	/** 本地是否已经播出过一帧动作画面，用于保证 AttackStarted/Ended 成对。 */
	bool bPresentationActive = false;

	float ActionPositionSyncAccumulator = 0.f;

	// ------------------------------------------------------------------
	// 服务器专用运行数据
	// ------------------------------------------------------------------

	bool bHitExecuted = false;
	bool bHitWindowActive = false;
	int32 LastHitId = 0;
	FMHCombatHitEvent LastConfirmedHitEvent;
	TSet<int32> ProcessedHitIds;
	bool bHasPreviousHitOrigin = false;
	FVector PreviousHitOrigin = FVector::ZeroVector;
	bool bWeaponSwitchAllowed = false;
	bool bMontageDelegatesBound = false;
	bool bMovementLocked = false;
	EMovementMode SavedMovementMode = MOVE_Walking;
	TArray<TWeakObjectPtr<AActor>> HitActorsThisMove;

	/** 死亡等状态下整体关闭战斗，避免继续接受输入。 */
	bool bCombatEnabled = true;

	// ------------------------------------------------------------------
	// 内部实现
	// ------------------------------------------------------------------

	/** 服务器 RPC：按序接收并执行客户端命令。 */
	UFUNCTION(Server, Reliable)
	void Server_HandleComboInput(const FMHCombatInputCommand& Command);

	/**
	 * 客户端武器切换请求。武器切换暂不做本地预测，但所有判定仍在服务器完成，
	 * 客户端只在收到 CurrentWeaponPath 后更新表现，避免和动作回滚状态互相覆盖。
	 */
	UFUNCTION(Server, Reliable)
	void Server_RequestEquipWeapon(const FSoftObjectPath& WeaponPath);

	/** 纯逻辑执行：服务器（bReplay=false）与客户端重放（bReplay=true）共用同一套规则。 */
	bool ExecuteCombatCommand(const FMHCombatInputCommand& Command, UInputAction* InputAction, bool bReplay);

	bool TryStartAttack(const FMHCombatInputCommand& Input, UInputAction* InputAction, bool bReplay);
	bool BufferNextCombo(const FMHCombatInputCommand& Input, UInputAction* InputAction, float EvalTime);
	bool TryStartNextCombo(float EvalTime = -1.f, bool bReplay = false);
	bool StartMove(const FMHCombatMoveData& Move, int32 MoveIndex, const FMHCombatInputCommand* SourceInput, int32 InputSequence, bool bReplay);
	bool ReleaseChargeLogic(UInputAction* InputAction);

	/** 服务器：写回权威快照。bNewAction 为 true 时自增动作序号并重播表现。 */
	void CommitActionState(bool bNewAction);

	/** 把当前逻辑状态写进 ActionState（不含表现层动作）。 */
	void WriteActionStateFromLogic();

	/** 客户端：把 ActionState 恢复成逻辑状态。 */
	void ApplyReplicatedLogicState();

	/** 自主代理：恢复权威状态 → 重放未确认输入 → 对齐表现。 */
	void ReconcilePrediction();

	void DiscardAcknowledgedInputs(int32 AckedSequence);
	void TrimPendingInputCommands();

	/** 表现层：整段重播（服务器换动作 / 客户端需要重新起播蒙太奇）。 */
	void ApplyActionState();

	/** 表现层：同一动作内的增量刷新（Section、蓄力速率），不重播蒙太奇。 */
	void SyncActionPresentation();

	/** 客户端：把逻辑状态对齐到表现层（必要时才重播或跳转 Section）。 */
	void AlignPresentationToLogicState();

	void PlayMovePresentation(UWeaponDataAsset* MoveWeapon, int32 MoveIndex, FName SectionName, float PlayRate, float StartPosition = 0.f);
	void ApplyPresentationPlayRate();
	void ClearPresentationState(bool bInterrupted);

	UWeaponDataAsset* ResolveCurrentWeaponFromPath();

	bool IsComboInputAllowed(float EvalTime) const;
	bool CanStartBufferedCombo(float EvalTime) const;
	bool HasComboWindowExpired() const;
	void SetComboWindowState(EMHCombatComboWindowState NewState);
	float ResolveEvalTime() const;
	float ResolveMovePosition() const;

	const FMHCombatMoveData* GetMove(int32 MoveIndex) const;
	int32 FindBestComboIndex(const TMap<FComboCondition, int32>& ComboMoves, const FMHCombatInputCommand& Input, UInputAction* InputAction) const;
	bool MatchesComboCondition(const FComboCondition& Condition, const FMHCombatInputCommand& Input, UInputAction* InputAction) const;
	void ClearBufferedComboInput();
	void FinishCurrentMove(bool bInterrupted);

	void CacheOwnerReferences();
	void UpdateWeaponMesh(UWeaponDataAsset* NewWeapon);
	void UpdateMovementLock(bool bLock);
	void SetCombatState(EMHCombatState NewState, EMHCombatMovePhase NewPhase);

	void BindMontageDelegates();
	void UnbindMontageDelegates();
	bool IsCurrentMontage(UAnimMontage* Montage) const;

	/** 服务器校验并真正执行武器切换。 */
	bool ApplyEquipWeapon(UWeaponDataAsset* NewWeapon);

	/** 是否应该跑本地预测（自主代理 + 开启开关 + 非独立运行）。 */
	bool IsPredictionReconcileEnabled() const;

	/** 受击硬直期间禁止开始或重放攻击动作。 */
	bool IsOwnerReacting() const;

	// 命中检测（只在服务器执行）
	void HandleAttackStart();
	void PerformHitCheck();
	void BeginHitWindow();
	void EndHitWindow();
	void PerformHitSweep();
	FVector ResolveHitOrigin() const;
	bool PerformHitQuery(const FVector& Start, const FVector& End);
	void DrawDebugHitSweep(const FVector& Start, const FVector& End, float Radius, bool bHit) const;
	bool TryApplyHit(AActor* Target, const FVector& HitLocation, const FVector& HitNormal);
	bool ApplyDamageToTarget(AActor* Target, const FVector& HitLocation, const FVector& HitNormal);
	float ResolveDamage(const FMHCombatMoveData& MoveData) const;

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_BroadcastHitEvent(const FMHCombatHitEvent& HitEvent);

	void ProcessConfirmedHit(const FMHCombatHitEvent& HitEvent);

	UFUNCTION()
	void OnRep_CurrentWeaponPath();

	UFUNCTION()
	void OnRep_ActionState();

	UFUNCTION()
	void OnRep_LastProcessedInputSequence();

	UFUNCTION()
	void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	FVector2D CurrentMoveInput = FVector2D::ZeroVector;
	TMap<UInputAction*, float> AttackInputPressTimes;
};
