#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UHealthComponent.generated.h"

/** 血量变化。服务器和客户端的广播方式完全一致，只带结果不带原因。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMHHealthChanged, float, NewHealth, float, MaxHealth);

/** 血量归零。服务器和客户端都会收到，表现层可以直接用。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMHHealthDepleted);

/*
 * 角色血量。服务器是唯一写方，客户端通过 OnRep 拿到变化。
 * 玩家和怪物共用同一份实现；战斗组件不需要知道血量存在，
 * 死亡时由角色把 CombatComponent 关掉即可。
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent), BlueprintType)
class MH_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	float GetHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	float GetHealthPercent() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	bool IsDead() const { return bDead; }

	/** 服务器扣血，返回这次实际造成的伤害。客户端调用或已死亡时返回 0。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat|Health")
	float ApplyDamage(const FMHDamageEvent& DamageEvent);

	/** 服务器把血量补满再复活。NewMaxHealth <= 0 表示沿用当前 MaxHealth。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat|Health")
	void ResetHealth(float NewMaxHealth = -1.f);

	UPROPERTY(BlueprintAssignable, Category = "Combat|Health")
	FMHHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Health")
	FMHHealthDepleted OnDeath;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MaxHealth, Category = "Combat|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Health")
	float CurrentHealth = 100.f;

	/** 服务器写，客户端只读；变 true 时客户端也会收到 OnDeath。 */
	UPROPERTY(ReplicatedUsing = OnRep_bDead, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Health")
	bool bDead = false;

	UFUNCTION()
	void OnRep_CurrentHealth();

	UFUNCTION()
	void OnRep_MaxHealth();

	UFUNCTION()
	void OnRep_bDead();

private:
	/** 两端共用的落地逻辑，保证服务器和客户端表现一致。 */
	void BroadcastHealthChanged();
	void BroadcastDeath();

	/** 避免 OnDeath 在同一段生命周期里重复广播。 */
	bool bDeathHandled = false;
};
