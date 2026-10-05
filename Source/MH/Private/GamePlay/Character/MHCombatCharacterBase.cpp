#include "GamePlay/Character/MHCombatCharacterBase.h"

#include "Components/CapsuleComponent.h"
#include "GamePlay/Combat/UCombatComponent.h"
#include "GamePlay/Combat/UCombatFeedbackComponent.h"
#include "GamePlay/Combat/UHealthComponent.h"
#include "GamePlay/Combat/UHitReactionComponent.h"

AMHCombatCharacterBase::AMHCombatCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// 玩家与怪物都要参与网络复制：怪物由服务器跑 AI，客户端只做表现。
	bReplicates = true;

	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetCapsuleComponent()->SetGenerateOverlapEvents(false);

	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// 命中扫掠靠骨骼网格上的物理资产，因此这里必须开 Overlap 事件。
	GetMesh()->SetGenerateOverlapEvents(true);

	CombatComponent = CreateDefaultSubobject<UCombatComponent>(TEXT("CombatComponent"));
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	HitReactionComponent = CreateDefaultSubobject<UHitReactionComponent>(TEXT("HitReactionComponent"));
	CombatFeedbackComponent = CreateDefaultSubobject<UCombatFeedbackComponent>(TEXT("CombatFeedbackComponent"));
}

void AMHCombatCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AMHCombatCharacterBase::HandleDeath);
	}
}

FMHDamageResult AMHCombatCharacterBase::ReceiveDamage_Implementation(const FMHDamageEvent& DamageEvent)
{
	FMHDamageResult Result;
	if (!HealthComponent)
	{
		return Result;
	}

	Result.bHit = true;
	Result.HitReactionId = DamageEvent.HitReactionId;

	// Invulnerability is checked before health so an invulnerable target never loses HP.
	if (HitReactionComponent && !HitReactionComponent->CanReceiveHit())
	{
		Result.bInvulnerable = true;
		Result.RemainingHealth = HealthComponent->GetHealth();
		return Result;
	}

	Result.AppliedDamage = HealthComponent->ApplyDamage(DamageEvent);
	Result.RemainingHealth = HealthComponent->GetHealth();
	Result.bKilled = HealthComponent->IsDead();

	if (HitReactionComponent)
	{
		HitReactionComponent->HandleConfirmedHit(DamageEvent, Result);
	}

	if (Result.bInterruptedTarget && CombatComponent)
	{
		CombatComponent->CancelCurrentAttack();
	}

	return Result;
}

void AMHCombatCharacterBase::SetCombatEnabled(bool bEnabled)
{
	if (CombatComponent)
	{
		CombatComponent->SetCombatEnabled(bEnabled);
	}
}

void AMHCombatCharacterBase::HandleDeath()
{
	// 死亡后不再接受输入、也不再开新动作；进行中的动作由战斗组件自己收尾。
	SetCombatEnabled(false);
}
