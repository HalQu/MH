#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GamePlay/Combat/IMHCombatTargetInterface.h"
#include "MHCombatCharacterBase.generated.h"

class UCombatComponent;
class UHealthComponent;
class UHitReactionComponent;
class UCombatFeedbackComponent;

/**
 * 玩家与怪物共用的战斗角色基类。
 *
 * 这里只放“所有会打架的角色都需要”的东西：战斗 / 血量 / 受击 / 打击反馈四个组件，
 * 以及伤害接口与死亡收尾。这样怪物天生就有连招、蓄力、命中窗口、受击硬直、击退和打击反馈，
 * 不需要为了实现一遍伤害流程去复制玩家代码。
 *
 * 分工：
 *  - 本类：组件装配、ReceiveDamage、死亡收尾；
 *  - AMHCharacter：玩家输入、相机（玩家专属）；
 *  - 怪物子类：AI 决策所需的接口与死亡表现（怪物专属）。
 */
UCLASS(Abstract)
class MH_API AMHCombatCharacterBase : public ACharacter, public IMHCombatTargetInterface
{
	GENERATED_BODY()

public:
	AMHCombatCharacterBase();

	/** IMHCombatTargetInterface：收到伤害请求，实际扣血由 UHealthComponent 处理。 */
	virtual FMHDamageResult ReceiveDamage_Implementation(const FMHDamageEvent& DamageEvent) override;

	UFUNCTION(BlueprintPure, Category = "Combat")
	/** 连招状态机与命中判定。AI 不需要另建攻击接口，直接调 HandleComboInput 驱动同一套配置。 */
	UCombatComponent* GetCombatComponent() const { return CombatComponent; }

	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	UHitReactionComponent* GetHitReactionComponent() const { return HitReactionComponent; }

	UFUNCTION(BlueprintPure, Category = "Combat|Feedback")
	UCombatFeedbackComponent* GetCombatFeedbackComponent() const { return CombatFeedbackComponent; }

	/** 战斗总开关。死亡时由本类自动关闭；复活 / 生成器可以再打开。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SetCombatEnabled(bool bEnabled);

protected:
	virtual void BeginPlay() override;

	/**
	 * 血量归零的统一收尾：先关掉战斗，再交给子类做出生表现。
	 * 子类重写时必须调用 Super，否则会丢掉“死亡后不再接输入”这条保证。
	 */
	UFUNCTION()
	virtual void HandleDeath();

	// ---------------------------------------------------------------
	// 战斗组件
	// ---------------------------------------------------------------

	/** 连招状态机与命中判定。玩家和怪物共用同一套，AI 通过 HandleComboInput 驱动。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Health")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|HitReaction")
	TObjectPtr<UHitReactionComponent> HitReactionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Feedback")
	TObjectPtr<UCombatFeedbackComponent> CombatFeedbackComponent;
};
