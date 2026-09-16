#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UHitReactionComponent.generated.h"

class ACharacter;
class UAnimInstance;
class UAnimMontage;

/** 受击开始：通知 UI / 音效层播放受击表现，Direction 是相对目标朝向的方向。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMHHitReactionStarted, FName, ReactionId, EMHHitReactionDirection, Direction);

/** 受击结束：硬直时间走完或被强制打断时触发，两端都会收到。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHHitReactionEnded, FName, ReactionId);

/** 受击状态快照变化（含无敌、霸体、削韧等只读镜像），方便 UI 直接刷新状态行。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHHitReactionStateChanged, const FMHHitReactionState&, NewState);

/**
 * 服务器权威的受击反应组件。
 *
 * 服务器只写 ReactionState；客户端通过 OnRep 播放对应蒙太奇。这样受击不是一次性 RPC：
 * 迟到加入、丢包重传和相关性恢复都能从当前状态恢复表现。伤害本身仍由 HealthComponent 结算。
 *
 * 职责边界：
 *  - 本组件只做「受击表现 + 硬直 / 无敌 / 霸体」判定，不扣血、不计算伤害；
 *  - 服务器调用 HandleConfirmedHit() 写入权威状态，客户端只读状态并播放蒙太奇；
 *  - 击退位移用 LaunchCharacter 在服务器施加，位置变化照常走移动复制。
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent), BlueprintType)
class MH_API UHitReactionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHitReactionComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 服务器在扣血前调用。无敌时返回 false，目标不应扣除任何生命值。 */
	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	bool CanReceiveHit() const { return !bInvulnerable; }

	/** 是否正在硬直。输入和移动会被锁定，直到权威状态结束。 */
	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	bool IsReacting() const { return ReactionState.bActive; }

	/** 硬直期间是否禁止移动。按 ReactionId 的配置决定，找不到配置时默认禁止。 */
	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	bool IsMovementLocked() const;

	/** 是否处于无敌帧。无敌期间命中会被丢弃，连扣血都不会发生。 */
	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	bool IsInvulnerable() const { return bInvulnerable; }

	/** 是否处于霸体状态。霸体会承受削韧，韧性归零后才会进入硬直。 */
	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	bool IsSuperArmorActive() const { return bSuperArmor; }

	/** 当前剩余韧性值，UI / 调试可以显示。 */
	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	float GetCurrentPoise() const { return CurrentPoise; }

	/** 当前权威受击状态快照（ReactionId、方向、剩余时间、来源等）。 */
	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	FMHHitReactionState GetReactionState() const { return ReactionState; }

	/** 距离硬直结束还剩多少秒。用网络同步时间计算，服务器和客户端结果一致。 */
	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	float GetReactionRemainingTime() const;

	/** 蓝图可在闪避、无敌帧或调试时调用；Duration <= 0 表示一直持续到显式关闭。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat|HitReaction")
	void SetInvulnerable(bool bNewInvulnerable, float Duration = 0.f);

	/** 开关霸体。bRefillPoise 为 true 时同时把韧性补满，常用于进入霸体状态时。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat|HitReaction")
	void SetSuperArmor(bool bNewSuperArmor, bool bRefillPoise = true);

	/** 服务器扣血后调用。函数只写反应状态，不会再次修改生命值。 */
	void HandleConfirmedHit(const FMHDamageEvent& DamageEvent, FMHDamageResult& InOutResult);

	UPROPERTY(BlueprintAssignable, Category = "Combat|HitReaction")
	FMHHitReactionStarted OnHitReactionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Combat|HitReaction")
	FMHHitReactionEnded OnHitReactionEnded;

	UPROPERTY(BlueprintAssignable, Category = "Combat|HitReaction")
	FMHHitReactionStateChanged OnHitReactionStateChanged;

protected:
	/** ReactionId 到受击表现的映射。没有匹配项时仍然执行硬直和移动锁定。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction")
	TMap<FName, FMHHitReactionDefinition> Reactions;

	/** 动作没有填 HitReactionId、或填的 Id 在表里找不到时使用的兜底配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction")
	FName DefaultReactionId = TEXT("Default");

	/** 动作没有配 HitStunDuration 时的硬直时长。实际时长取「动作配置」与「蒙太奇长度」的较大值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction", meta = (ClampMin = "0.0"))
	float DefaultHitStunDuration = 0.35f;

	/** 霸体韧性上限。每次命中扣动作配置的 PoiseDamage，归零即破霸体进入硬直。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction|Poise", meta = (ClampMin = "0.01"))
	float MaxPoise = 1.f;

	/** 韧性恢复速度（点/秒），只在 PoiseRecoveryDelay 之后才开始生效。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction|Poise", meta = (ClampMin = "0.0"))
	float PoiseRecoveryPerSecond = 1.f;

	/** 最后一次被削韧后，等待多少秒才开始回韧。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction|Poise", meta = (ClampMin = "0.0"))
	float PoiseRecoveryDelay = 0.75f;

	/** 出生 / 复活时是否直接进入无敌帧（常见于登场保护）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction")
	bool bStartInvulnerable = false;

	/** 初始无敌时长。0 表示一直无敌，直到蓝图显式调用 SetInvulnerable(false)。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction", meta = (EditCondition = "bStartInvulnerable", ClampMin = "0.0"))
	float InitialInvulnerabilityDuration = 0.f;

	/** 权威受击状态。服务器写，客户端只读；Sequence 变化即代表一次新的受击或结束。 */
	UPROPERTY(ReplicatedUsing = OnRep_ReactionState, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|HitReaction")
	FMHHitReactionState ReactionState;

	/** 无敌标志。复制给客户端用于 UI 表现，判定只发生在服务器。 */
	UPROPERTY(ReplicatedUsing = OnRep_Invulnerable, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|HitReaction")
	bool bInvulnerable = false;

	/** 霸体标志。与 CurrentPoise 一起复制，客户端可据此显示霸体特效。 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|HitReaction")
	bool bSuperArmor = false;

	/** 当前韧性值（服务器权威，复制给客户端显示）。 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|HitReaction")
	float CurrentPoise = 1.f;

	/** 客户端收到受击状态后播放 / 停止对应蒙太奇。 */
	UFUNCTION()
	void OnRep_ReactionState();

	/** 无敌状态变化时只广播状态，不碰蒙太奇。 */
	UFUNCTION()
	void OnRep_Invulnerable();

private:
	/** 根据命中来源与自身朝向算出前 / 后 / 左 / 右，用于选择方向蒙太奇。 */
	EMHHitReactionDirection ResolveHitDirection(const FMHDamageEvent& DamageEvent) const;
	/** 算击退方向：优先背离攻击者，其次背离命中点，最后用命中法线反向兜底。 */
	FVector ResolveKnockbackDirection(const FMHDamageEvent& DamageEvent) const;
	/** 按 ReactionId 查配置，查不到时回落到 DefaultReactionId。 */
	const FMHHitReactionDefinition* ResolveReactionDefinition(FName ReactionId) const;
	/** 在配置里按方向挑蒙太奇，方向项为空时使用 DefaultMontage。 */
	const FMHHitReactionMontage* ResolveReactionMontage(const FMHHitReactionDefinition* Definition, EMHHitReactionDirection Direction) const;

	/** 服务器：写入新的受击状态（自增 Sequence），并按需施加击退。 */
	void StartHitReaction(const FMHDamageEvent& DamageEvent, EMHHitReactionDirection Direction, float Duration);
	/** 服务器：硬直时间到，自增 Sequence 并广播结束事件。 */
	void EndHitReaction();
	/** 两端共用：按状态播放蒙太奇；客户端会按同步时间追赶服务器进度。 */
	void PlayHitReactionPresentation(const FMHHitReactionState& State);
	/** 停止当前受击蒙太奇，BlendOutTime 控制淡出时间。 */
	void StopHitReactionPresentation(float BlendOutTime = 0.08f);
	/** 服务器：按 DamageEvent.LaunchStrength 施加击退，方向在这里按命中来源现算。 */
	void ApplyLaunch(const FMHDamageEvent& DamageEvent, const FMHHitReactionDefinition* Definition);
	/** 服务器每帧：处理无敌到期、硬直结束、韧性回复。 */
	void UpdateServerState(float DeltaTime);

	/** 缓存的角色 / 动画实例，避免每帧 Cast。 */
	ACharacter* GetOwningCharacter() const;
	UAnimInstance* GetOwningAnimInstance() const;
	/** ReactionId 为空时返回 DefaultReactionId。 */
	FName ResolveReactionId(FName RequestedId) const;

	/**
	 * 网络同步过的服务器时间（秒）。客户端的本地世界时间与服务器起点不同，
	 * 受击开始时刻和已经过去的时长都必须走 GameState 的同步时间，不能直接相减。
	 */
	double GetServerSyncedTimeSeconds() const;

	/** 弱引用缓存：对象被销毁后自动失效，访问时再回退到 GetOwner() 重新取。 */
	TWeakObjectPtr<ACharacter> CachedCharacter;
	TWeakObjectPtr<UAnimInstance> CachedAnimInstance;

	/** 本组件当前正在播放的受击蒙太奇，用于结束时精确停止，避免误停别人的蒙太奇。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PresentedReactionMontage = nullptr;

	/** 已经应用过的 ReactionState.Sequence，用来判断 OnRep 是「新受击」还是重复包。 */
	int32 AppliedReactionSequence = INDEX_NONE;

	/** 无敌到期时刻（同步服务器时间）；为 0 表示无限期无敌。 */
	float InvulnerableEndTime = 0.f;

	/** 最后一次被削韧的时间（同步服务器时间），用于计算回韧延迟。 */
	float LastPoiseDamageTime = -1000.f;
};
