#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UCombatFeedbackComponent.generated.h"

class UCombatComponent;

/** 收到一条服务器确认的命中事件。蓝图 / UI 层可以直接监听它做飘字、连击计数等。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatHitFeedbackReceived, const FMHCombatHitEvent&, HitEvent);

/**
 * 客户端打击反馈组件。
 *
 * 它只消费服务器确认的 FMHCombatHitEvent，不参与伤害结算。默认在每台客户端生成世界特效与音效，
 * 攻击者本机还会额外获得镜头震动和顿帧（卡肉）。没有配置资源时对应步骤会静默跳过。
 *
 * 为什么不自己发多播：命中事件已经由 UCombatComponent 复制到每台机器，
 * 这里只负责“在本机生成一次表现”，既保证各端看到同样的打击，也不会产生第二份复制开销。
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent), BlueprintType)
class MH_API UCombatFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatFeedbackComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 只在顿帧期间参与 Tick：每帧重新确认暂停，覆盖顿帧开始后才播放的蒙太奇。 */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 命中事件广播。命中点、伤害、是否击杀等都已由服务器确认。 */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Feedback")
	FMHCombatHitFeedbackReceived OnHitFeedbackReceived;

	/** 顿帧是否正在进行，调试 HUD 可以据此显示状态。 */
	UFUNCTION(BlueprintPure, Category = "Combat|Feedback")
	bool IsHitStopActive() const { return bHitStopActive; }

protected:
	/** 是否在命中点生成 Niagara 命中特效。特效在每个客户端本地生成，不走第二次复制。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	bool bEnableWorldEffects = true;

	/** 是否允许镜头震动。只有攻击者的本地控制器会真正触发（受击方的反馈走受击表现）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	bool bEnableCameraShake = true;

	/** 是否允许顿帧（卡肉）。关闭后各动作配置的 HitStopDuration 会被忽略，但仍保留在动作表里。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	bool bEnableHitStop = true;

	/**
	 * 顿帧期间受击方的时间膨胀倍率。受击方在攻击方本机是模拟代理，动一下会看起来像滑步，
	 * 因此把它的时间膨胀临时压低（默认 0.05，几乎不动）；1 = 不处理。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float HitStopTargetTimeDilation = 0.05f;

	/**
	 * 循环特效的兜底寿命（秒）。循环 Niagara 永远不会自己结束（bAutoDestroy 也没用），
	 * 动作没配 ImpactEffectLifetime 时就用这个值强制收尾。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback", meta = (ClampMin = "0.1"))
	float LoopingEffectFallbackLifetime = 2.f;

	/** 是否播放命中音效。两个打击反馈开关同时关闭时，本组件就只触发蓝图事件。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	bool bEnableImpactSound = true;

	/** 蓝图可继续接命中特效、顿帧、手柄震动等项目自己的表现。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Combat|Feedback", meta = (DisplayName = "On Hit Feedback"))
	void BP_OnHitFeedback(const FMHCombatHitEvent& HitEvent);

private:
	/** 顿帧期间一起定住的一个角色，以及它在被改写前的时间膨胀值。 */
	struct FHitStopParticipant
	{
		TWeakObjectPtr<AActor> Actor;
		float RestoredTimeDilation = 1.f;
	};

	/** 订阅 UCombatComponent::OnHitConfirmed；只在同角色上的战斗组件存在时绑定。 */
	UFUNCTION()
	void HandleHitConfirmed(const FMHCombatHitEvent& HitEvent);

	/** 只负责 Niagara 命中特效，包含循环特效的兜底寿命处理。 */
	void SpawnImpactEffect(const FMHCombatHitEvent& HitEvent, const FVector& ImpactNormal, const FRotator& ImpactRotation);

	/** 只负责命中音效：每台机器各随机抽一条候选播放。 */
	void PlayImpactSound(const FMHCombatHitEvent& HitEvent, const FRotator& ImpactRotation);

	/** 在世界里生成命中特效 / 音效；每台机器各生成一份，互不复制。 */
	void SpawnWorldFeedback(const FMHCombatHitEvent& HitEvent);

	/** 本机玩家专属反馈（镜头震动等），非本地控制的角色直接跳过。 */
	void ApplyLocalPlayerFeedback(const FMHCombatHitEvent& HitEvent);

	/** 攻击方本机的顿帧：暂停双方蒙太奇，计时结束后恢复。其它客户端不受影响。 */
	void ApplyHitStop(const FMHCombatHitEvent& HitEvent);

	/** 顿帧计时结束：恢复被暂停的蒙太奇并关闭 Tick。 */
	void FinishHitStop();

	/** 暂停 / 恢复一个角色身上所有蒙太奇，没有骨骼网格或没有动画实例时直接跳过。 */
	static void SetActorMontagePaused(AActor* Actor, bool bPaused);

	/** 登记一个顿帧参与方；bScaleTimeDilation 为 true 时同时压低它的时间膨胀。 */
	void AddHitStopParticipant(AActor* Actor, bool bScaleTimeDilation);

	/** 绑定的战斗组件弱引用，用于 EndPlay 时安全解绑。 */
	TWeakObjectPtr<UCombatComponent> BoundCombatComponent;

	/** 本次顿帧需要一起定住的角色（攻击方 + 受击方）。 */
	TArray<FHitStopParticipant> HitStopParticipants;

	/** 顿帧计时器；连续命中时只延长、不缩短已经在走的计时。 */
	FTimerHandle HitStopTimerHandle;

	/** 顿帧是否进行中，同时决定本组件是否 Tick。 */
	bool bHitStopActive = false;
};
