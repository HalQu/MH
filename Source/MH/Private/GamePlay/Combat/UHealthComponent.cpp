#include "GamePlay/Combat/UHealthComponent.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "GamePlay/Combat/UWeaponDataAsset.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogMHHealth, Log, All);

UHealthComponent::UHealthComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHealthComponent, MaxHealth);
	DOREPLIFETIME(UHealthComponent, CurrentHealth);
	DOREPLIFETIME(UHealthComponent, bDead);
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	// Blueprint 子类里保存下来的组件模板可能仍是旧的不复制默认值。
	if (!GetIsReplicated())
	{
		UE_LOG(LogMHHealth, Warning, TEXT("[CombatHealth] Component replication flag was false at BeginPlay; enabling it now."));
		SetIsReplicated(true);
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// 生成时按配置值补满，并把初始血量广播一次，HUD 不用再单独查一遍。
	CurrentHealth = MaxHealth;
	bDead = CurrentHealth <= 0.f;
	bDeathHandled = false;
	BroadcastHealthChanged();

	UE_LOG(LogMHHealth, Log,
		TEXT("[CombatHealth] BeginPlay Owner=%s NetMode=%d Health=%.2f/%.2f"),
		*GetOwner()->GetName(),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
		CurrentHealth,
		MaxHealth);
}

float UHealthComponent::GetHealthPercent() const
{
	return MaxHealth > 0.f ? FMath::Clamp(CurrentHealth / MaxHealth, 0.f, 1.f) : 0.f;
}

float UHealthComponent::ApplyDamage(const FMHDamageEvent& DamageEvent)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead)
	{
		return 0.f;
	}

	const float Damage = FMath::Max(0.f, DamageEvent.Damage);
	if (Damage <= 0.f)
	{
		return 0.f;
	}

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.f, CurrentHealth - Damage);
	const float AppliedDamage = PreviousHealth - CurrentHealth;

	UE_LOG(LogMHHealth, Log,
		TEXT("[CombatHealth] Damage Owner=%s Source=%s Weapon=%s Applied=%.2f Health=%.2f/%.2f"),
		*GetOwner()->GetName(),
		DamageEvent.Source ? *DamageEvent.Source->GetName() : TEXT("null"),
		DamageEvent.Weapon ? *DamageEvent.Weapon->GetName() : TEXT("null"),
		AppliedDamage,
		CurrentHealth,
		MaxHealth);

	BroadcastHealthChanged();

	if (CurrentHealth <= 0.f)
	{
		bDead = true;
		BroadcastDeath();
	}

	return AppliedDamage;
}

void UHealthComponent::ResetHealth(float NewMaxHealth)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (NewMaxHealth > 0.f)
	{
		MaxHealth = NewMaxHealth;
	}

	CurrentHealth = MaxHealth;
	bDead = false;
	bDeathHandled = false;
	BroadcastHealthChanged();

	UE_LOG(LogMHHealth, Log,
		TEXT("[CombatHealth] ResetHealth Owner=%s Health=%.2f/%.2f"),
		*GetOwner()->GetName(),
		CurrentHealth,
		MaxHealth);
}

void UHealthComponent::OnRep_CurrentHealth()
{
	BroadcastHealthChanged();
}

void UHealthComponent::OnRep_MaxHealth()
{
	BroadcastHealthChanged();
}

void UHealthComponent::OnRep_bDead()
{
	if (bDead)
	{
		BroadcastDeath();
	}
	else
	{
		// 复活：允许下一次死亡继续广播。
		bDeathHandled = false;
	}
}

void UHealthComponent::BroadcastHealthChanged()
{
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void UHealthComponent::BroadcastDeath()
{
	if (bDeathHandled)
	{
		return;
	}

	bDeathHandled = true;

	UE_LOG(LogMHHealth, Log,
		TEXT("[CombatHealth] Death Owner=%s NetMode=%d Authority=%d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("null"),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
		GetOwner() && GetOwner()->HasAuthority() ? 1 : 0);

	OnDeath.Broadcast();
}
