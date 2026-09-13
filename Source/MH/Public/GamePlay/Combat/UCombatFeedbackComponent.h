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
 * 它只消费服务器确认的 FMHCombatHitEvent，不参与伤害结算。默认在每台客户端生成世界特效，
 * 只有攻击者的本地控制器会收到镜头震动。没有配置资源时对应步骤会静默跳过。
 *
 * 为什么不用 Multicast 自己播特效：命中事件本身已经是服务器多播过来的结果，
 * 每台机器在本地生成一次表现就能保证外观一致，又不会产生第二份复制开销。
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent), BlueprintType)
class MH_API UCombatFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatFeedbackComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 命中事件广播。命中点、伤害、是否击杀等都已由服务器确认。 */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Feedback")
	FMHCombatHitFeedbackReceived OnHitFeedbackReceived;

protected:
	/** 是否在命中点生成粒子。特效在每个客户端本地生成，不走第二次复制。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	bool bEnableWorldEffects = true;

	/** 是否允许镜头震动。只有攻击者的本地控制器会真正触发（受击方的反馈走受击表现）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	bool bEnableCameraShake = true;

	/** 是否播放命中音效。两个打击反馈开关同时关闭时，本组件就只触发蓝图事件。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	bool bEnableImpactSound = true;

	/** 蓝图可继续接命中特效、顿帧、手柄震动等项目自己的表现。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Combat|Feedback", meta = (DisplayName = "On Hit Feedback"))
	void BP_OnHitFeedback(const FMHCombatHitEvent& HitEvent);

private:
	/** 订阅 UCombatComponent::OnHitConfirmed；只在同角色上的战斗组件存在时绑定。 */
	UFUNCTION()
	void HandleHitConfirmed(const FMHCombatHitEvent& HitEvent);

	/** 在世界里生成命中特效 / 音效，只处理自己作为攻击者的那一份。 */
	void SpawnWorldFeedback(const FMHCombatHitEvent& HitEvent);

	/** 本机玩家专属反馈（镜头震动等），非本地控制的角色直接跳过。 */
	void ApplyLocalPlayerFeedback(const FMHCombatHitEvent& HitEvent);

	/** 绑定的战斗组件弱引用，用于 EndPlay 时安全解绑。 */
	TWeakObjectPtr<UCombatComponent> BoundCombatComponent;
};
