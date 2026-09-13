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

	/** 按 Loadout 顺序循环切武器，Delta 为 ±1；实际切换在服务器发生，客户端等复制。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool CycleWeapon(int32 Delta);

	/** 装备指定武器资产。客户端调用时转成 Server_RequestEquipWeapon 请求。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool EquipWeapon(UWeaponDataAsset* NewWeapon);

	/** 装备 DefaultWeapon。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool EquipWeapon_Default();

	/** 把武器加入 Loadout（不立即装备）。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool AddWeaponToLoadout(UWeaponDataAsset* Weapon);

	/** 从 Loadout 移除武器。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool RemoveWeaponFromLoadout(UWeaponDataAsset* Weapon);

	/** 当前是否处于允许切武器的窗口（由蒙太奇的 WeaponSwitchAllowed 通知状态驱动）。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool CanSwitchWeaponNow() const;

	/** 客户端主动请求下一次同步时完整重放未确认输入，主要用于调试和异常恢复。 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Network")
	void RequestPredictionReconcile();

	// ------------------------------------------------------------------
	// 动作控制
	// ------------------------------------------------------------------

	/** 主动取消当前攻击（服务器执行），常用于受击、死亡、切场景。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void CancelCurrentAttack();

	/** 关闭后不再接受输入、不再开新动作；进行中的动作会被中断（服务器）。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SetCombatEnabled(bool bEnabled);

	/** 战斗总开关。关闭后仍然会跟随服务器的动作状态，只是不再接受本地输入。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsCombatEnabled() const { return bCombatEnabled; }

	// ------------------------------------------------------------------
	// 动画通知入口
	// ------------------------------------------------------------------

	/** 旧式单帧通知入口（如 AttackStart），按 SourceMontage 过滤掉非当前动作的通知。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void HandleCombatNotify(EMHCombatNotifyType NotifyType, UAnimMontage* SourceMontage);

	/** 区间通知状态入口（蓄力窗口 / 连招窗口 / 命中窗口 / 允许切武器）。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void HandleCombatNotifyState(EMHCombatNotifyStateType StateType, EMHCombatNotifyStateEvent StateEvent, UAnimMontage* SourceMontage);

	// ------------------------------------------------------------------
	// 查询
	// ------------------------------------------------------------------

	/** 当前武器资产（客户端是复制过来的镜像）。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	UWeaponDataAsset* GetCurrentWeapon() const;

	/** 当前武器 Id，对应 UWeaponDataAsset 里的标识。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	FName GetCurrentWeaponId() const;

	/** 当前动作的数据行。只做查询，不包含运行时进度。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	FMHCombatMoveData GetCurrentMoveData() const;

	/** 只读镜像：本机当前看到的战斗状态（服务器权威 / 客户端预测）。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	EMHCombatState GetCombatState() const;

	/** 只读镜像：动作阶段（起手 / 蓄力 / 攻击 / 收招）。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	EMHCombatMovePhase GetMovePhase() const;

	/** 连招窗口状态（Closed / Pending / Open / Buffered），动画蓝图和 UI 可读。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	EMHCombatComboWindowState GetComboWindowState() const { return ComboWindowState; }

	/** 当前动作在武器动作表中的索引。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetCurrentMoveIndex() const;

	/** 当前动作 Id，用于日志与动画蓝图分支。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	FName GetCurrentMoveId() const { return CurrentMoveId; }

	/** 当前动作已播放的蒙太奇时间（秒）。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetCurrentMoveTime() const;

	/** 当前动作蒙太奇的总时长（秒）。没有动作时为 0。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetCurrentMoveLength() const;

	/** 服务器是否正在执行命中扫掠（由 AttackHitWindow 通知状态驱动）。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsHitWindowActive() const { return bHitWindowActive; }

	/** 权威动作序号。服务器每次换招 / 结束都自增，客户端用它判断是否需要重放表现。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetActionStateSequence() const { return ActionState.Sequence; }

	/** 最近一次服务器确认的命中事件。调试 HUD / UI 直接读它，不需要自己缓存。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	FMHCombatHitEvent GetLastConfirmedHit() const { return LastConfirmedHitEvent; }

	/** 当前是否离地（起手分支与部分动作条件会用到）。 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAirborne() const;

	/** 是否处于蓄力窗口（蒙太奇减速中）。 */
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

	/** 换武器后广播（两端一致，携带新武器资产）。 */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatWeaponChanged OnWeaponChanged;

	/** 战斗状态 / 阶段变化。只在真实变化时广播，可用于驱动动画蓝图。 */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatStateChanged OnCombatStateChanged;

	/** 动作开始（服务器与预测端都会触发，便于本地零延迟表现）。 */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatAttackStarted OnAttackStarted;

	/** 动作结束，bInterrupted 区分自然播完还是被打断。 */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatAttackEnded OnAttackEnded;

	/** 本地预测与服务器对齐后触发（只在自主代理上）。调试 HUD 用它显示错误纠正情况。 */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatPredictionReconciled OnPredictionReconciled;

	/** 服务器确认命中。UCombatFeedbackComponent 与调试 HUD 都订阅它。 */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FMHCombatHitConfirmed OnHitConfirmed;

protected:
	/** 角色出生时自动装备的武器，也会被加入 Loadout。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Loadout")
	TObjectPtr<UWeaponDataAsset> DefaultWeapon = nullptr;

	/** 可循环切换的武器列表；CycleWeapon 按这个数组的顺序走。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Loadout")
	TArray<TObjectPtr<UWeaponDataAsset>> LoadoutWeapons;

	/** 命中扫掠起点使用的骨骼插槽名，找不到时回退到角色原点。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings")
	FName DefaultHitOriginSocketName = TEXT("weapon_r");

	/** 攻击期间是否锁住地面移动；默认关闭，让动作游戏保持走砍手感。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings")
	bool bLockGroundMovementDuringAttack = false;

	/** 连招窗口关闭之后，仍然接受连招输入的宽限时间（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Settings", meta = (ClampMin = "0.0"))
	float AttackInputBufferDuration = 0.20f;

	/** 全局伤害倍率，最终伤害 = 动作伤害 * DamageMultiplier。 */
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

	/** 由 CurrentWeaponPath 解析出来的本地资产镜像，客户端只读。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	TObjectPtr<UWeaponDataAsset> CurrentWeapon = nullptr;

	/**
	 * 当前武器的复制来源。用软路径而不是直接复制对象，是为了避开资产加载时机问题，
	 * 客户端在 OnRep 里才去解析资产并更新模型。
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponPath, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	FSoftObjectPath CurrentWeaponPath;

	/** 挂在角色骨骼上的武器模型组件，换武器时由服务器/客户端各自更新外观。 */
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

	/** 动作阶段（起手 / 蓄力 / 攻击 / 收招），与 CombatState 一起驱动动画蓝图。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	EMHCombatMovePhase MovePhase = EMHCombatMovePhase::None;

	/** 连招窗口是否开启（只读镜像，方便蓝图/动画蓝图查询）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	bool bComboWindowOpen = false;

	/** 当前动作的数据行 Id，复制给客户端用于查询动作表。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	FName CurrentMoveId = NAME_None;

	/** 当前动作已播放时间（秒）。服务器权威值，客户端预测时会本地推进。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	float CurrentMoveTime = 0.f;

	/** 当前动作在武器动作表中的下标，INDEX_NONE 表示没有动作。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 CurrentMoveIndex = INDEX_NONE;

	/** 当前武器在 LoadoutWeapons 中的下标，-1 表示不在列表中。 */
	int32 CurrentWeaponIndex = INDEX_NONE;

	/** 当前动作行的本地缓存，避免每帧查表；换动作时刷新。 */
	UPROPERTY(Transient)
	FMHCombatMoveData CurrentMoveData;

	/** 缓存组件，避免每帧 GetOwner 查找；CacheOwnerReferences 里赋值。 */
	UPROPERTY(Transient)
	TObjectPtr<ACharacter> CachedCharacter = nullptr;

	/** 角色骨骼网格，播放蒙太奇与查找命中插槽都走它。 */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> CachedMesh = nullptr;

	/** 移动组件，用于空中判定与攻击时锁定移动。 */
	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CachedMovement = nullptr;

	/** 动画实例，Montage_Play / Montage_SetPosition 等表现层操作的目标。 */
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

	/** 连招窗口状态机：Closed → Pending → Open，窗口外输入的进 Buffered。 */
	EMHCombatComboWindowState ComboWindowState = EMHCombatComboWindowState::Closed;

	/** 连招窗口关闭时所在的蒙太奇位置（秒）。 */
	float ComboWindowClosePosition = 0.f;

	/** 是否已经存下一条窗口外/前摇内抱住的连招输入。 */
	bool bHasBufferedComboInput = false;
	/** 抱住的连招输入内容，窗口开启后由 TryStartNextCombo 消费。 */
	FMHCombatInputCommand BufferedComboInput;
	/** 蓄力窗口（AnimNotifyState 区间）是否处于激活状态。 */
	bool bChargeWindowActive = false;

	// ------------------------------------------------------------------
	// 本地预测（自主代理）
	// ------------------------------------------------------------------

	/** 已发送给服务器、尚未被确认的输入命令，按序号升序。 */
	TArray<FMHCombatInputCommand> PendingInputCommands;

	/** 本机生成输入命令的自增序号；服务器按这个序号逐个确认。 */
	int32 LocalInputSequence = 0;
	/** 最近一次对齐时已经重放到哪个输入序号，避免重复回滚。 */
	int32 LastReconciledInputSequence = 0;
	/** 上一次 ReconcilePrediction 重放的输入条数，仅用于调试 HUD。 */
	int32 LastReconcileReplayedCount = 0;
	/** 标记：收到新的权威快照后，本帧末尾需要跑一次对齐。 */
	bool bPredictionReconcilePending = false;
	/** 已经应用过的 ActionState.Sequence，用来判断这次复制是不是新动作。 */
	int32 AppliedActionSequence = 0;

	// ------------------------------------------------------------------
	// 表现层
	// ------------------------------------------------------------------

	/** 当前真正在播放的蒙太奇（可能与逻辑层动作不一致，回滚对齐时用来收尾）。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PresentedMontage = nullptr;

	/** 当前表现层在播的动作下标（可能与逻辑层的 CurrentMoveIndex 短暂不一致）。 */
	int32 PresentedMoveIndex = INDEX_NONE;
	/** 当前表现层正在播的 Section，用于回滚对齐时判断要不要跳段。 */
	FName PresentedSectionName = NAME_None;

	/** 当前动作在本地时间轴上的起播时刻，用来在回滚后平滑地对齐进度。 */
	float PredictedMoveStartTime = -1.f;

	/** 本地是否已经播出过一帧动作画面，用于保证 AttackStarted/Ended 成对。 */
	bool bPresentationActive = false;

	/** 播放位置同步的累加器，达到 ActionPositionSyncInterval 才做一次对齐。 */
	float ActionPositionSyncAccumulator = 0.f;

	// ------------------------------------------------------------------
	// 服务器专用运行数据
	// ------------------------------------------------------------------

	/** 旧式单帧命中通知（AttackHit）是否已执行过；连续扫掠另由 bHitWindowActive 控制。 */
	bool bHitExecuted = false;
	/** 服务器命中窗口是否开启；只有开启时 Tick 才会做武器扫掠。 */
	bool bHitWindowActive = false;
	/** 命中事件自增 Id，用来在多播去重时区分不同次命中。 */
	int32 LastHitId = 0;
	/** 最近一次确认命中的事件，复制不确定，各端各自缓存给调试 HUD / UI 读取。 */
	FMHCombatHitEvent LastConfirmedHitEvent;
	/** 已经处理过的命中事件 Id；多播重发或乱序时靠它幂等去重。 */
	TSet<int32> ProcessedHitIds;
	/** 是否已有上一帧的扫掠起点；动作第一帧只记录起点，不做扫掠。 */
	bool bHasPreviousHitOrigin = false;
	/** 上一帧的武器命中点，与当前帧连成一条胶囊扫掠轨迹。 */
	FVector PreviousHitOrigin = FVector::ZeroVector;
	/** 由 WeaponSwitchAllowed 通知状态驱动；关闭时拒绝切武器。 */
	bool bWeaponSwitchAllowed = false;
	/** 防止蒙太奇结束/打断回调和重复绑定。 */
	bool bMontageDelegatesBound = false;
	/** 当前是否因为攻击而锁住了移动。 */
	bool bMovementLocked = false;
	/** 锁移动前保存的移动模式，解锁时恢复。 */
	EMovementMode SavedMovementMode = MOVE_Walking;
	/** 本次动作已经命中过的目标，保证一个动作对同一目标只结算一次。 */
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

	/** 尝试用一条输入开启新动作（查起手表 + 条件匹配）。 */
	bool TryStartAttack(const FMHCombatInputCommand& Input, UInputAction* InputAction, bool bReplay);

	/** 把输入存进连招缓冲，等窗口开启后再消费。 */
	bool BufferNextCombo(const FMHCombatInputCommand& Input, UInputAction* InputAction, float EvalTime);

	/** 若当前动作的连招表能匹配上抱住的输入，就跳到下一个动作。 */
	bool TryStartNextCombo(float EvalTime = -1.f, bool bReplay = false);

	/** 真正切到一条动作：重置命中/连招/蓄力状态，并推进动作序号。 */
	bool StartMove(const FMHCombatMoveData& Move, int32 MoveIndex, const FMHCombatInputCommand* SourceInput, int32 InputSequence, bool bReplay);

	/** 蓄力按键松开：逻辑上从蓄力阶段进入攻击阶段。 */
	bool ReleaseChargeLogic(UInputAction* InputAction);

	/** 服务器：写回权威快照。bNewAction 为 true 时自增动作序号并重播表现。 */
	void CommitActionState(bool bNewAction);

	/** 把当前逻辑状态写进 ActionState（不含表现层动作）。 */
	void WriteActionStateFromLogic();

	/** 客户端：把 ActionState 恢复成逻辑状态。 */
	void ApplyReplicatedLogicState();

	/** 自主代理：恢复权威状态 → 重放未确认输入 → 对齐表现。 */
	void ReconcilePrediction();

	/** 丢掉所有序号 <= AckedSequence 的本地待确认输入。 */
	void DiscardAcknowledgedInputs(int32 AckedSequence);
	/** 按条数和时长两个上限裁剪未确认输入队列，防止长时间丢包无限增长。 */
	void TrimPendingInputCommands();

	/** 表现层：整段重播（服务器换动作 / 客户端需要重新起播蒙太奇）。 */
	void ApplyActionState();

	/** 表现层：同一动作内的增量刷新（Section、蓄力速率），不重播蒙太奇。 */
	void SyncActionPresentation();

	/** 客户端：把逻辑状态对齐到表现层（必要时才重播或跳转 Section）。 */
	void AlignPresentationToLogicState();

	/** 播放/重播一条蒙太奇：设置 Section、播放速率和起始位置。 */
	void PlayMovePresentation(UWeaponDataAsset* MoveWeapon, int32 MoveIndex, FName SectionName, float PlayRate, float StartPosition = 0.f);
	/** 按逻辑状态重新计算蓄力减速倍率并应用到当前蒙太奇。 */
	void ApplyPresentationPlayRate();
	/** 收尾表现层：停蒙太奇、清缓存，并广播 AttackEnded。 */
	void ClearPresentationState(bool bInterrupted);

	/** 从复制的软路径解析出武器资产，处理还没加载完的情况。 */
	UWeaponDataAsset* ResolveCurrentWeaponFromPath();

	/** 当前是否允许接受新的连招输入（窗口开启 + 未在受击等）。 */
	bool IsComboInputAllowed(float EvalTime) const;
	/** 抱住的输入现在能不能接手（窗口已开且满足动作条件）。 */
	bool CanStartBufferedCombo(float EvalTime) const;
	/** 窗口是否已经超过 AttackInputBufferDuration 的宽限期。 */
	bool HasComboWindowExpired() const;
	/** 切换连招窗口状态，并在变化时同步复制用的 bComboWindowOpen。 */
	void SetComboWindowState(EMHCombatComboWindowState NewState);
	/** 取当前用于条件判定的时间：服务器用权威时间，客户端用本地预测时间。 */
	float ResolveEvalTime() const;
	/** 取当前蒙太奇播放位置（秒）；回滚对齐和连招窗口都用它。 */
	float ResolveMovePosition() const;

	/** 按动作表下标取动作行；越界返回 nullptr。 */
	const FMHCombatMoveData* GetMove(int32 MoveIndex) const;
	/** 在动作的连招表里找最合适的一条转移：优先精确按键，再回退到通配条件。 */
	int32 FindBestComboIndex(const TMap<FComboCondition, int32>& ComboMoves, const FMHCombatInputCommand& Input, UInputAction* InputAction) const;
	/** 单个 FComboCondition 是否与本次输入匹配（按键 + 触发方式 + 方向等）。 */
	bool MatchesComboCondition(const FComboCondition& Condition, const FMHCombatInputCommand& Input, UInputAction* InputAction) const;
	/** 清空抱住的连招输入与标记。 */
	void ClearBufferedComboInput();
	/** 结束当前动作：清逻辑/表现状态并回到 Locomotion；bInterrupted 区分自然结束与被中断。 */
	void FinishCurrentMove(bool bInterrupted);

	/** 缓存 Owner、骨骼网格、移动组件、动画实例等常用引用。 */
	void CacheOwnerReferences();
	/** 把武器资产上的模型挂到角色网格；NewWeapon 为空表示隐藏武器。 */
	void UpdateWeaponMesh(UWeaponDataAsset* NewWeapon);
	/** 按需锁定/恢复移动，并记录原来的移动模式。 */
	void UpdateMovementLock(bool bLock);
	/** 写 CombatState / MovePhase，变化时广播 OnCombatStateChanged。 */
	void SetCombatState(EMHCombatState NewState, EMHCombatMovePhase NewPhase);

	/** 绑定当前蒙太奇的结束/打断回调；已绑过则先解绑。 */
	void BindMontageDelegates();
	/** 解绑蒙太奇回调，动作结束或组件销毁时调用。 */
	void UnbindMontageDelegates();
	/** 过滤旧蒙太奇的延迟回调，避免上一段动作误结束当前动作。 */
	bool IsCurrentMontage(UAnimMontage* Montage) const;

	/** 服务器校验并真正执行武器切换。 */
	bool ApplyEquipWeapon(UWeaponDataAsset* NewWeapon);

	/** 是否应该跑本地预测（自主代理 + 开启开关 + 非独立运行）。 */
	bool IsPredictionReconcileEnabled() const;

	/** 受击硬直期间禁止开始或重放攻击动作。 */
	bool IsOwnerReacting() const;

	// 命中检测（只在服务器执行）

	/** AttackStart 通知：结束蓄力逻辑，把动作阶段从起手/蓄力切到 Active。 */
	void HandleAttackStart();
	/** 命中窗口内每次 Tick 调用，按服务端帧时间推进扫掠。 */
	void PerformHitCheck();
	/** 进入命中窗口：清空上一动作的命中缓存并记录扫掠起点。 */
	void BeginHitWindow();
	/** 离开命中窗口：停止扫掠并清掉轨迹调试数据。 */
	void EndHitWindow();
	/** 用上一帧与当前帧的武器位置做胶囊扫掠，并派发命中。 */
	void PerformHitSweep();
	/** 取武器命中点（骨骼插槽，找不到就用角色位置 + 高度偏移）。 */
	FVector ResolveHitOrigin() const;
	/** 对一段扫掠路径做实际碰撞查询，返回是否撞到东西。 */
	bool PerformHitQuery(const FVector& Start, const FVector& End);
	/** 按 CVar 开关画扫掠轨迹，便于调武器判定盒/速度。 */
	void DrawDebugHitSweep(const FVector& Start, const FVector& End, float Radius, bool bHit) const;
	/** 校验目标并结算一次命中；同一动作对同一目标只结算一次。 */
	bool TryApplyHit(AActor* Target, const FVector& HitLocation, const FVector& HitNormal);
	/** 把伤害/受击方向交给目标的 CombatTarget 接口，返回是否成功。 */
	bool ApplyDamageToTarget(AActor* Target, const FVector& HitLocation, const FVector& HitNormal);
	/** 计算最终伤害：动作基础伤害（或覆盖值） * DamageMultiplier。 */
	float ResolveDamage(const FMHCombatMoveData& MoveData) const;

	/**
	 * 服务器确认命中后向所有端广播同一份事件。
	 * 用 Reliable + 事件 Id 去重，保证打击反馈不丢、也不重复结算。
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_BroadcastHitEvent(const FMHCombatHitEvent& HitEvent);

	/** 统一处理确认命中：缓存事件、去重，并向 UI/反馈组件广播。 */
	void ProcessConfirmedHit(const FMHCombatHitEvent& HitEvent);

	/** 武器软路径复制到本端后：解析资产、刷新武器模型与伤害配置。 */
	UFUNCTION()
	void OnRep_CurrentWeaponPath();

	/** 收到权威动作快照：自主代理标记待对齐，模拟代理直接同步表现。 */
	UFUNCTION()
	void OnRep_ActionState();

	/** 服务器确认序号变化：丢弃已确认输入，需要时触发回滚重放。 */
	UFUNCTION()
	void OnRep_LastProcessedInputSequence();

	/** 蒙太奇开始被换出（BlendingOut）时的回调，用于处理被打断。 */
	UFUNCTION()
	void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	/** 蒙太奇真正播完时的回调，用于自然结束一个动作。 */
	UFUNCTION()
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** 最近一次移动输入，供 FComboCondition 做方向判定，并在重放时复用。 */
	FVector2D CurrentMoveInput = FVector2D::ZeroVector;
	/** 各攻击键按下时刻（本地时间），用于计算“按下到松开”的蓄力时长。 */
	TMap<UInputAction*, float> AttackInputPressTimes;
};
