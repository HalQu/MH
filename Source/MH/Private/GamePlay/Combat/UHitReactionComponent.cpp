#include "GamePlay/Combat/UHitReactionComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogMHHitReaction, Log, All);

UHitReactionComponent::UHitReactionComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UHitReactionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHitReactionComponent, ReactionState);
	DOREPLIFETIME(UHitReactionComponent, bInvulnerable);
	DOREPLIFETIME(UHitReactionComponent, bSuperArmor);
	DOREPLIFETIME(UHitReactionComponent, CurrentPoise);
}

void UHitReactionComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedCharacter = Cast<ACharacter>(GetOwner());
	CachedAnimInstance = GetOwningAnimInstance();

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	CurrentPoise = FMath::Max(MaxPoise, 0.01f);
	if (bStartInvulnerable)
	{
		SetInvulnerable(true, InitialInvulnerabilityDuration);
	}
}

void UHitReactionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopHitReactionPresentation(0.f);
	CachedAnimInstance.Reset();
	CachedCharacter.Reset();
	Super::EndPlay(EndPlayReason);
}

void UHitReactionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		UpdateServerState(DeltaTime);
	}
}

float UHitReactionComponent::GetReactionRemainingTime() const
{
	if (!ReactionState.bActive)
	{
		return 0.f;
	}

	return FMath::Max(ReactionState.EndServerTime - static_cast<float>(GetServerSyncedTimeSeconds()), 0.f);
}

bool UHitReactionComponent::IsMovementLocked() const
{
	if (!ReactionState.bActive)
	{
		return false;
	}

	const FMHHitReactionDefinition* Definition = ResolveReactionDefinition(ReactionState.ReactionId);
	return !Definition || Definition->bLockMovement;
}

void UHitReactionComponent::SetInvulnerable(bool bNewInvulnerable, float Duration)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bInvulnerable = bNewInvulnerable;
	if (bNewInvulnerable && Duration > 0.f)
	{
		InvulnerableEndTime = static_cast<float>(GetServerSyncedTimeSeconds()) + Duration;
	}
	else
	{
		InvulnerableEndTime = 0.f;
	}
}

void UHitReactionComponent::SetSuperArmor(bool bNewSuperArmor, bool bRefillPoise)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bSuperArmor = bNewSuperArmor;
	if (bRefillPoise)
	{
		CurrentPoise = FMath::Max(MaxPoise, 0.01f);
	}
}

void UHitReactionComponent::HandleConfirmedHit(const FMHDamageEvent& DamageEvent, FMHDamageResult& InOutResult)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || InOutResult.bKilled)
	{
		return;
	}

	if (ReactionState.bActive)
	{
		InOutResult.bSuperArmorBlocked = true;
		return;
	}

	if (bSuperArmor)
	{
		const float PoiseDamage = FMath::Max(0.f, DamageEvent.PoiseDamage);
		CurrentPoise = FMath::Max(0.f, CurrentPoise - PoiseDamage);
		LastPoiseDamageTime = static_cast<float>(GetServerSyncedTimeSeconds());

		if (CurrentPoise > 0.f)
		{
			InOutResult.bSuperArmorBlocked = true;
			OnHitReactionStateChanged.Broadcast(ReactionState);
			return;
		}
	}

	const EMHHitReactionDirection Direction = ResolveHitDirection(DamageEvent);
	const float Duration = FMath::Max(DamageEvent.HitStunDuration, DefaultHitStunDuration);
	StartHitReaction(DamageEvent, Direction, Duration);

	InOutResult.bReactionStarted = true;
	InOutResult.bInterruptedTarget = DamageEvent.bInterruptTarget;
	InOutResult.HitReactionId = ReactionState.ReactionId;
	InOutResult.ReactionDirection = ReactionState.Direction;
}

EMHHitReactionDirection UHitReactionComponent::ResolveHitDirection(const FMHDamageEvent& DamageEvent) const
{
	const ACharacter* Character = GetOwningCharacter();
	if (!Character)
	{
		return EMHHitReactionDirection::Front;
	}

	FVector SourceDirection = -DamageEvent.HitNormal.GetSafeNormal2D();
	if (DamageEvent.Source)
	{
		SourceDirection = (DamageEvent.Source->GetActorLocation() - Character->GetActorLocation()).GetSafeNormal2D();
	}

	if (SourceDirection.IsNearlyZero())
	{
		return EMHHitReactionDirection::Front;
	}

	const FVector LocalDirection = Character->GetActorTransform().InverseTransformVectorNoScale(SourceDirection);
	if (FMath::Abs(LocalDirection.X) >= FMath::Abs(LocalDirection.Y))
	{
		return LocalDirection.X >= 0.f ? EMHHitReactionDirection::Front : EMHHitReactionDirection::Back;
	}

	return LocalDirection.Y >= 0.f ? EMHHitReactionDirection::Right : EMHHitReactionDirection::Left;
}

FName UHitReactionComponent::ResolveReactionId(FName RequestedId) const
{
	return RequestedId.IsNone() ? DefaultReactionId : RequestedId;
}

const FMHHitReactionDefinition* UHitReactionComponent::ResolveReactionDefinition(FName ReactionId) const
{
	const FName ResolvedId = ResolveReactionId(ReactionId);
	if (const FMHHitReactionDefinition* Found = Reactions.Find(ResolvedId))
	{
		return Found;
	}

	if (ResolvedId != DefaultReactionId)
	{
		return Reactions.Find(DefaultReactionId);
	}

	return nullptr;
}

const FMHHitReactionMontage* UHitReactionComponent::ResolveReactionMontage(
	const FMHHitReactionDefinition* Definition,
	EMHHitReactionDirection Direction) const
{
	if (!Definition)
	{
		return nullptr;
	}

	if (const FMHHitReactionMontage* Directional = Definition->DirectionalMontages.Find(Direction))
	{
		if (Directional->Montage)
		{
			return Directional;
		}
	}

	return Definition->DefaultMontage.Montage ? &Definition->DefaultMontage : nullptr;
}

void UHitReactionComponent::StartHitReaction(
	const FMHDamageEvent& DamageEvent,
	EMHHitReactionDirection Direction,
	float Duration)
{
	++ReactionState.Sequence;
	ReactionState.bActive = true;
	ReactionState.ReactionId = ResolveReactionId(DamageEvent.HitReactionId);
	ReactionState.Direction = Direction;
	ReactionState.Source = DamageEvent.Source;
	ReactionState.HitLocation = DamageEvent.HitLocation;
	ReactionState.HitNormal = DamageEvent.HitNormal;
	ReactionState.HitStunDuration = FMath::Max(Duration, 0.05f);
	// 用网络同步时间，客户端才能正确换算出“这段受击已经过去多久”。
	ReactionState.StartServerTime = static_cast<float>(GetServerSyncedTimeSeconds());
	ReactionState.EndServerTime = ReactionState.StartServerTime + ReactionState.HitStunDuration;
	ReactionState.bSuperArmor = bSuperArmor;

	const FMHHitReactionDefinition* Definition = ResolveReactionDefinition(ReactionState.ReactionId);
	const FMHHitReactionMontage* ReactionMontage = ResolveReactionMontage(Definition, Direction);
	if (ReactionMontage && ReactionMontage->Montage)
	{
		const float MontageDuration = ReactionMontage->Montage->GetPlayLength() / FMath::Max(ReactionMontage->PlayRate, 0.01f);
		ReactionState.HitStunDuration = FMath::Max(ReactionState.HitStunDuration, MontageDuration);
		ReactionState.EndServerTime = ReactionState.StartServerTime + ReactionState.HitStunDuration;
	}

	CurrentPoise = FMath::Max(MaxPoise, 0.01f);
	LastPoiseDamageTime = ReactionState.StartServerTime;
	PlayHitReactionPresentation(ReactionState);
	ApplyLaunch(DamageEvent, Definition);

	UE_LOG(LogMHHitReaction, Log,
		TEXT("[HitReaction] Start Owner=%s Reaction=%s Direction=%d Duration=%.3f Source=%s"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("null"),
		*ReactionState.ReactionId.ToString(),
		static_cast<int32>(Direction),
		ReactionState.HitStunDuration,
		DamageEvent.Source ? *DamageEvent.Source->GetName() : TEXT("null"));

	OnHitReactionStarted.Broadcast(ReactionState.ReactionId, Direction);
	OnHitReactionStateChanged.Broadcast(ReactionState);
}

void UHitReactionComponent::EndHitReaction()
{
	if (!ReactionState.bActive)
	{
		return;
	}

	const FName FinishedReactionId = ReactionState.ReactionId;
	++ReactionState.Sequence;
	ReactionState.bActive = false;
	ReactionState.EndServerTime = static_cast<float>(GetServerSyncedTimeSeconds());
	StopHitReactionPresentation();

	UE_LOG(LogMHHitReaction, Log,
		TEXT("[HitReaction] End Owner=%s Reaction=%s"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("null"),
		*FinishedReactionId.ToString());

	OnHitReactionEnded.Broadcast(FinishedReactionId);
	OnHitReactionStateChanged.Broadcast(ReactionState);
}

void UHitReactionComponent::PlayHitReactionPresentation(const FMHHitReactionState& State)
{
	if (!State.bActive)
	{
		StopHitReactionPresentation();
		return;
	}

	CachedAnimInstance = GetOwningAnimInstance();
	if (!CachedAnimInstance.IsValid())
	{
		return;
	}

	const FMHHitReactionDefinition* Definition = ResolveReactionDefinition(State.ReactionId);
	const FMHHitReactionMontage* ReactionMontage = ResolveReactionMontage(Definition, State.Direction);
	if (!ReactionMontage || !ReactionMontage->Montage)
	{
		// 没有配置蒙太奇时仍然要中断当前攻击表现；硬直逻辑由状态计时器负责。
		CachedAnimInstance->Montage_Stop(0.06f);
		StopHitReactionPresentation(0.f);
		AppliedReactionSequence = State.Sequence;
		return;
	}

	const float PlayRate = FMath::Max(ReactionMontage->PlayRate, 0.01f);
	float StartPosition = 0.f;
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		// 只在客户端追赶进度。用的是网络同步过的服务器时间，两端世界时间起点不同也成立。
		const float Elapsed = static_cast<float>(GetServerSyncedTimeSeconds() - State.StartServerTime);
		const float MontageLength = ReactionMontage->Montage->GetPlayLength();
		const float ElapsedLimit = MontageLength > 0.f ? MontageLength / PlayRate : State.HitStunDuration;

		if (Elapsed > 0.f && Elapsed < ElapsedLimit)
		{
			StartPosition = Elapsed * PlayRate;
		}
		else if (Elapsed >= ElapsedLimit)
		{
			// 这段受击很可能已经在网络上播完了。从头补播会让人物停在受击姿势里，
			// 所以直接收尾；硬直仍由权威状态计时负责。只在极端丢包时才会走到这里。
			StopHitReactionPresentation(0.f);
			AppliedReactionSequence = State.Sequence;
			return;
		}
	}

	const float PlayLength = CachedAnimInstance->Montage_Play(
		ReactionMontage->Montage,
		PlayRate,
		EMontagePlayReturnType::MontageLength,
		StartPosition,
		true);

	if (PlayLength <= 0.f)
	{
		UE_LOG(LogMHHitReaction, Warning,
			TEXT("[HitReaction] Montage play failed. Owner=%s Montage=%s"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("null"),
			*ReactionMontage->Montage->GetName());
		return;
	}

	if (!ReactionMontage->SectionName.IsNone()
		&& ReactionMontage->Montage->IsValidSectionName(ReactionMontage->SectionName))
	{
		CachedAnimInstance->Montage_JumpToSection(ReactionMontage->SectionName, ReactionMontage->Montage);
	}

	PresentedReactionMontage = ReactionMontage->Montage;
	AppliedReactionSequence = State.Sequence;
}

void UHitReactionComponent::StopHitReactionPresentation(float BlendOutTime)
{
	if (PresentedReactionMontage)
	{
		if (UAnimInstance* AnimInstance = GetOwningAnimInstance())
		{
			AnimInstance->Montage_Stop(FMath::Max(BlendOutTime, 0.f), PresentedReactionMontage);
		}
		PresentedReactionMontage = nullptr;
	}
}

// 击退方向完全在受击时现算：动作表里只存强度，同一招打在任意朝向的目标身上都能正确推开。
FVector UHitReactionComponent::ResolveKnockbackDirection(const FMHDamageEvent& DamageEvent) const
{
	const ACharacter* Character = GetOwningCharacter();
	if (!Character)
	{
		return FVector::ZeroVector;
	}

	// 主要路径：背离攻击者（从攻击者指向自己）的水平方向。
	if (DamageEvent.Source)
	{
		const FVector AwayFromSource =
			(Character->GetActorLocation() - DamageEvent.Source->GetActorLocation()).GetSafeNormal2D();
		if (!AwayFromSource.IsNearlyZero())
		{
			return AwayFromSource;
		}
	}

	// 没有来源或与来源重叠（环境伤害 / 自伤类效果）：背离命中点推开。
	const FVector AwayFromHit =
		(Character->GetActorLocation() - DamageEvent.HitLocation).GetSafeNormal2D();
	if (!AwayFromHit.IsNearlyZero())
	{
		return AwayFromHit;
	}

	// 最后才用命中法线反向兜底，保证不会因为缺少位置信息而完全丢失击退。
	return -DamageEvent.HitNormal.GetSafeNormal2D();
}

void UHitReactionComponent::ApplyLaunch(const FMHDamageEvent& DamageEvent, const FMHHitReactionDefinition* Definition)
{
	ACharacter* Character = GetOwningCharacter();
	if (!Character || DamageEvent.LaunchStrength <= 0.f)
	{
		return;
	}

	FVector Direction = ResolveKnockbackDirection(DamageEvent);
	if (Direction.IsNearlyZero())
	{
		return;
	}
	Direction.Z += 0.5f; 
	const float LaunchScale = Definition ? FMath::Max(Definition->LaunchScale, 0.f) : 1.f;
	// bZOverride = false：只覆盖水平速度，空中受击时保留原有下落速度，不会被“钉”在空中。
	Character->LaunchCharacter(Direction * DamageEvent.LaunchStrength * LaunchScale, true, true);
}

void UHitReactionComponent::UpdateServerState(float DeltaTime)
{
	if (!GetWorld())
	{
		return;
	}

	const float Now = static_cast<float>(GetServerSyncedTimeSeconds());

	if (bInvulnerable && InvulnerableEndTime > 0.f && Now >= InvulnerableEndTime)
	{
		SetInvulnerable(false);
	}

	if (ReactionState.bActive && Now >= ReactionState.EndServerTime)
	{
		EndHitReaction();
	}

	if (bSuperArmor
		&& !ReactionState.bActive
		&& CurrentPoise < MaxPoise
		&& Now - LastPoiseDamageTime >= PoiseRecoveryDelay)
	{
		CurrentPoise = FMath::Min(MaxPoise, CurrentPoise + FMath::Max(PoiseRecoveryPerSecond, 0.f) * DeltaTime);
	}
}

double UHitReactionComponent::GetServerSyncedTimeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	// GameState 的服务器时间会复制到客户端并随本地时间外推；用各自的 World 时间相减毫无意义。
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}

	return World->GetTimeSeconds();
}

ACharacter* UHitReactionComponent::GetOwningCharacter() const
{
	return CachedCharacter.IsValid() ? CachedCharacter.Get() : Cast<ACharacter>(GetOwner());
}

UAnimInstance* UHitReactionComponent::GetOwningAnimInstance() const
{
	if (CachedAnimInstance.IsValid())
	{
		return CachedAnimInstance.Get();
	}

	const ACharacter* Character = GetOwningCharacter();
	return Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
}

// 客户端表现入口：Sequence 变化才算一次新的受击/结束，避免复制重发导致蒙太奇反复重播。
void UHitReactionComponent::OnRep_ReactionState()
{
	const bool bNewReactionState = ReactionState.Sequence != AppliedReactionSequence;
	if (ReactionState.bActive)
	{
		if (bNewReactionState)
		{
			PlayHitReactionPresentation(ReactionState);
		}
	}
	else if (bNewReactionState)
	{
		StopHitReactionPresentation();
		AppliedReactionSequence = ReactionState.Sequence;
		OnHitReactionEnded.Broadcast(ReactionState.ReactionId);
	}

	OnHitReactionStateChanged.Broadcast(ReactionState);
}

void UHitReactionComponent::OnRep_Invulnerable()
{
	OnHitReactionStateChanged.Broadcast(ReactionState);
}
