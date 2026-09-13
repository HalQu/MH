#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UHitReactionComponent.generated.h"

class ACharacter;
class UAnimInstance;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMHHitReactionStarted, FName, ReactionId, EMHHitReactionDirection, Direction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHHitReactionEnded, FName, ReactionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHHitReactionStateChanged, const FMHHitReactionState&, NewState);

/**
 * 服务器权威的受击反应组件。
 *
 * 服务器只写 ReactionState；客户端通过 OnRep 播放对应蒙太奇。这样受击不是一次性 RPC：
 * 迟到加入、丢包重传和相关性恢复都能从当前状态恢复表现。伤害本身仍由 HealthComponent 结算。
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

	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	bool IsMovementLocked() const;

	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	bool IsInvulnerable() const { return bInvulnerable; }

	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	bool IsSuperArmorActive() const { return bSuperArmor; }

	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	float GetCurrentPoise() const { return CurrentPoise; }

	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	FMHHitReactionState GetReactionState() const { return ReactionState; }

	/** 距离硬直结束还剩多少秒。用网络同步时间计算，服务器和客户端结果一致。 */
	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	float GetReactionRemainingTime() const;

	/** 蓝图可在闪避、无敌帧或调试时调用；Duration <= 0 表示一直持续到显式关闭。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat|HitReaction")
	void SetInvulnerable(bool bNewInvulnerable, float Duration = 0.f);

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction")
	FName DefaultReactionId = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction", meta = (ClampMin = "0.0"))
	float DefaultHitStunDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction|Poise", meta = (ClampMin = "0.01"))
	float MaxPoise = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction|Poise", meta = (ClampMin = "0.0"))
	float PoiseRecoveryPerSecond = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction|Poise", meta = (ClampMin = "0.0"))
	float PoiseRecoveryDelay = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction")
	bool bStartInvulnerable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|HitReaction", meta = (EditCondition = "bStartInvulnerable", ClampMin = "0.0"))
	float InitialInvulnerabilityDuration = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_ReactionState, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|HitReaction")
	FMHHitReactionState ReactionState;

	UPROPERTY(ReplicatedUsing = OnRep_Invulnerable, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|HitReaction")
	bool bInvulnerable = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|HitReaction")
	bool bSuperArmor = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|HitReaction")
	float CurrentPoise = 1.f;

	UFUNCTION()
	void OnRep_ReactionState();

	UFUNCTION()
	void OnRep_Invulnerable();

private:
	EMHHitReactionDirection ResolveHitDirection(const FMHDamageEvent& DamageEvent) const;
	const FMHHitReactionDefinition* ResolveReactionDefinition(FName ReactionId) const;
	const FMHHitReactionMontage* ResolveReactionMontage(const FMHHitReactionDefinition* Definition, EMHHitReactionDirection Direction) const;

	void StartHitReaction(const FMHDamageEvent& DamageEvent, EMHHitReactionDirection Direction, float Duration);
	void EndHitReaction();
	void PlayHitReactionPresentation(const FMHHitReactionState& State);
	void StopHitReactionPresentation(float BlendOutTime = 0.08f);
	void ApplyLaunch(const FMHDamageEvent& DamageEvent, const FMHHitReactionDefinition* Definition);
	void UpdateServerState(float DeltaTime);

	ACharacter* GetOwningCharacter() const;
	UAnimInstance* GetOwningAnimInstance() const;
	FName ResolveReactionId(FName RequestedId) const;

	/**
	 * 网络同步过的服务器时间（秒）。客户端的本地世界时间与服务器起点不同，
	 * 受击开始时刻和已经过去的时长都必须走 GameState 的同步时间，不能直接相减。
	 */
	double GetServerSyncedTimeSeconds() const;

	TWeakObjectPtr<ACharacter> CachedCharacter;
	TWeakObjectPtr<UAnimInstance> CachedAnimInstance;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PresentedReactionMontage = nullptr;

	int32 AppliedReactionSequence = INDEX_NONE;
	float InvulnerableEndTime = 0.f;
	float LastPoiseDamageTime = -1000.f;
};
