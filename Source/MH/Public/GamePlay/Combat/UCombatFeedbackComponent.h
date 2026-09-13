#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UCombatFeedbackComponent.generated.h"

class UCombatComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatHitFeedbackReceived, const FMHCombatHitEvent&, HitEvent);

/**
 * 客户端打击反馈组件。
 *
 * 它只消费服务器确认的 FMHCombatHitEvent，不参与伤害结算。默认在每台客户端生成世界特效，
 * 只有攻击者的本地控制器会收到镜头震动。没有配置资源时对应步骤会静默跳过。
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent), BlueprintType)
class MH_API UCombatFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatFeedbackComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Feedback")
	FMHCombatHitFeedbackReceived OnHitFeedbackReceived;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	bool bEnableWorldEffects = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	bool bEnableCameraShake = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Feedback")
	bool bEnableImpactSound = true;

	/** 蓝图可继续接命中特效、顿帧、手柄震动等项目自己的表现。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Combat|Feedback", meta = (DisplayName = "On Hit Feedback"))
	void BP_OnHitFeedback(const FMHCombatHitEvent& HitEvent);

private:
	UFUNCTION()
	void HandleHitConfirmed(const FMHCombatHitEvent& HitEvent);

	void SpawnWorldFeedback(const FMHCombatHitEvent& HitEvent);
	void ApplyLocalPlayerFeedback(const FMHCombatHitEvent& HitEvent);

	TWeakObjectPtr<UCombatComponent> BoundCombatComponent;
};
