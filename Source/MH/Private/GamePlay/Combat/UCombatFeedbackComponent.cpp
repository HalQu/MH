#include "GamePlay/Combat/UCombatFeedbackComponent.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraShakeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GamePlay/Combat/UCombatComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMHCombatFeedback, Log, All);

namespace
{
	/** 打击反馈日志：0 关闭，1 每次命中一行（排查“客户端看不到特效”时用）。 */
	static TAutoConsoleVariable<int32> CVarMHCombatFeedbackLog(
		TEXT("mh.Combat.FeedbackLog"),
		1,
		TEXT("Combat hit feedback log level: 0 off, 1 one line per confirmed hit."),
		ECVF_Cheat);
}

UCombatFeedbackComponent::UCombatFeedbackComponent()
{
	// 平时完全不需要 Tick，只有顿帧进行中才会临时打开。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UCombatFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UCombatComponent* CombatComponent = GetOwner() ? GetOwner()->FindComponentByClass<UCombatComponent>() : nullptr)
	{
		BoundCombatComponent = CombatComponent;
		CombatComponent->OnHitConfirmed.AddDynamic(this, &UCombatFeedbackComponent::HandleHitConfirmed);
	}
}

void UCombatFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 顿帧没走完就被销毁（死亡 / 换关卡）时先把双方蒙太奇放回去，避免角色卡在暂停状态。
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitStopTimerHandle);
	}
	FinishHitStop();

	if (UCombatComponent* CombatComponent = BoundCombatComponent.Get())
	{
		CombatComponent->OnHitConfirmed.RemoveDynamic(this, &UCombatFeedbackComponent::HandleHitConfirmed);
	}

	BoundCombatComponent.Reset();
	Super::EndPlay(EndPlayReason);
}

// 命中事件挂在攻击者的战斗组件上复制，因此每台机器只会经手一次；各端各自在本地生成一份表现。
void UCombatFeedbackComponent::HandleHitConfirmed(const FMHCombatHitEvent& HitEvent)
{
	OnHitFeedbackReceived.Broadcast(HitEvent);
	BP_OnHitFeedback(HitEvent);

	SpawnWorldFeedback(HitEvent);
	ApplyLocalPlayerFeedback(HitEvent);
	ApplyHitStop(HitEvent);
}

void UCombatFeedbackComponent::SpawnWorldFeedback(const FMHCombatHitEvent& HitEvent)
{
	if (!GetWorld())
	{
		return;
	}

	// 专用服务器没有画面也没有音箱，生成表现纯属浪费。
	if (GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 命中事件在每台机器上只会由攻击者的战斗组件处理一次（UCombatComponent 按 HitId 去重），
	// 所以这里不需要再判断“我是不是攻击者”：那只会让解析不到 Attacker 的客户端完全看不到特效。
	// 各端都在命中点本地生成一份，保证所有玩家屏幕上都能看到同一次打击。
	const FVector ImpactNormal = HitEvent.HitNormal.GetSafeNormal();
	// 特效沿命中法线立起来；法线为空（比如扫描型命中）时保持零旋转。
	const FRotator ImpactRotation = ImpactNormal.IsNearlyZero()
		? FRotator::ZeroRotator
		: FRotationMatrix::MakeFromZ(ImpactNormal).Rotator();

	SpawnImpactEffect(HitEvent, ImpactNormal, ImpactRotation);
	PlayImpactSound(HitEvent, ImpactRotation);

	if (CVarMHCombatFeedbackLog.GetValueOnAnyThread() >= 1)
	{
		UE_LOG(LogMHCombatFeedback, Log,
			TEXT("[Feedback] Owner=%s NetMode=%d LocalRole=%d"),
			*GetNameSafe(GetOwner()),
			static_cast<int32>(GetWorld()->GetNetMode()),
			GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : -1);

		UE_LOG(LogMHCombatFeedback, Log,
			TEXT("[Feedback] HitId=%d NetMode=%d Owner=%s Attacker=%s Effect=%s Sounds=%d HitStop=%.3f Invulnerable=%d SuperArmor=%d"),
			HitEvent.HitId,
			static_cast<int32>(GetWorld()->GetNetMode()),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(HitEvent.Attacker),
			*HitEvent.Feedback.ImpactEffect.ToString(),
			HitEvent.Feedback.ImpactSounds.Num(),
			HitEvent.Feedback.HitStopDuration,
			HitEvent.bInvulnerable ? 1 : 0,
			HitEvent.bSuperArmorBlocked ? 1 : 0);
	}
}

void UCombatFeedbackComponent::SpawnImpactEffect(const FMHCombatHitEvent& HitEvent, const FVector& ImpactNormal, const FRotator& ImpactRotation)
{
	if (!bEnableWorldEffects || HitEvent.Feedback.ImpactEffect.IsNull())
	{
		return;
	}

	UNiagaraSystem* Effect = HitEvent.Feedback.ImpactEffect.LoadSynchronous();
	if (!Effect)
	{
		return;
	}

	const float EffectScale = FMath::Max(HitEvent.Feedback.ImpactEffectScale, 0.01f);
	UNiagaraComponent* SpawnedEffect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		Effect,
		HitEvent.HitLocation,
		ImpactRotation,
		FVector(EffectScale),
		true,
		true,
		// 命中特效数量少，不走池化：池化会让“定时销毁”误伤到被回收后重新使用的组件。
		ENCPoolMethod::None,
		true);

	if (!SpawnedEffect)
	{
		return;
	}

	// 寿命：优先用动作配置；没配又是循环特效时，用兜底寿命强制收尾。
	float Lifetime = FMath::Max(HitEvent.Feedback.ImpactEffectLifetime, 0.f);
	if (Lifetime <= 0.f && Effect->IsLooping())
	{
		Lifetime = FMath::Max(LoopingEffectFallbackLifetime, 0.1f);
		UE_LOG(LogMHCombatFeedback, Log,
			TEXT("[Feedback] Impact effect loops forever; forcing %.2fs lifetime. Effect=%s"),
			Lifetime,
			*Effect->GetName());
	}

	if (Lifetime <= 0.f)
	{
		return;
	}

	if (CVarMHCombatFeedbackLog.GetValueOnAnyThread() >= 1)
	{
		UE_LOG(LogMHCombatFeedback, Log,
			TEXT("[Feedback] Impact effect spawned. Effect=%s Looping=%d Lifetime=%.2f Location=%s"),
			*Effect->GetName(),
			Effect->IsLooping() ? 1 : 0,
			Lifetime,
			*HitEvent.HitLocation.ToString());
	}

	// 组件不是 Actor，没有 SetLifeSpan，用弱引用定时器到点手动销毁。
	FTimerDelegate DestroyDelegate = FTimerDelegate::CreateWeakLambda(SpawnedEffect, [SpawnedEffect]()
	{
		if (IsValid(SpawnedEffect))
		{
			SpawnedEffect->DeactivateImmediate();
			SpawnedEffect->DestroyComponent();
		}
	});

	FTimerHandle EffectLifetimeHandle;
	GetWorld()->GetTimerManager().SetTimer(EffectLifetimeHandle, DestroyDelegate, Lifetime, false);
}

void UCombatFeedbackComponent::PlayImpactSound(const FMHCombatHitEvent& HitEvent, const FRotator& ImpactRotation)
{
	if (!bEnableImpactSound || HitEvent.Feedback.ImpactSounds.Num() <= 0)
	{
		return;
	}

	// 每台客户端各随机抽一条，避免所有人听到完全相同的采样。
	const int32 SoundIndex = FMath::RandHelper(HitEvent.Feedback.ImpactSounds.Num());
	if (USoundBase* Sound = HitEvent.Feedback.ImpactSounds[SoundIndex].LoadSynchronous())
	{
		UGameplayStatics::SpawnSoundAtLocation(
			GetWorld(),
			Sound,
			HitEvent.HitLocation,
			ImpactRotation,
			FMath::Max(HitEvent.Feedback.ImpactSoundVolume, 0.f));
	}
}

void UCombatFeedbackComponent::ApplyLocalPlayerFeedback(const FMHCombatHitEvent& HitEvent)
{
	if (!bEnableCameraShake
		|| !HitEvent.Attacker
		|| HitEvent.Attacker != GetOwner()
		|| !HitEvent.Feedback.CameraShakeClass)
	{
		return;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(OwnerPawn->GetController()))
	{
		PlayerController->ClientStartCameraShake(
			HitEvent.Feedback.CameraShakeClass,
			FMath::Max(HitEvent.Feedback.CameraShakeScale, 0.f));
	}
}

// 顿帧是纯表现：服务器照常推进逻辑，只有攻击方本机把双方定住。
void UCombatFeedbackComponent::ApplyHitStop(const FMHCombatHitEvent& HitEvent)
{
	if (!bEnableHitStop || !GetWorld() || !HitEvent.Attacker || HitEvent.Attacker != GetOwner())
	{
		return;
	}

	// 这里用 IsLocallyControlled 而不是 HasAuthority：监听服务器上的主机自己也是本地玩家，同样要卡肉。
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	// 被无敌 / 霸体挡下时同样有卡肉，只是按动作里配置的挡下时长走。
	const bool bBlocked = HitEvent.bInvulnerable || HitEvent.bSuperArmorBlocked;
	const float Duration = bBlocked
		? HitEvent.Feedback.BlockedHitStopDuration
		: HitEvent.Feedback.HitStopDuration;

	if (Duration <= 0.f)
	{
		return;
	}

	if (!bHitStopActive)
	{
		HitStopParticipants.Reset();
		bHitStopActive = true;
		SetComponentTickEnabled(true);
	}

	// 攻击方是自己：不缩放时间膨胀，而是靠 IsHitStopActive() 挡住移动输入（见 AMHCharacter::Move）。
	AddHitStopParticipant(HitEvent.Attacker, false);
	// 本机攻击方：立刻清掉惯性速度。只挡住新输入不够，已经积累的速度会让角色继续滑一小段。
	if (ACharacter* AttackerCharacter = Cast<ACharacter>(HitEvent.Attacker))
	{
		if (UCharacterMovementComponent* Movement = AttackerCharacter->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}
	// 受击方在攻击方本机是模拟代理，不压时间膨胀就会在卡肉期间继续滑步。
	AddHitStopParticipant(HitEvent.Target, true);

	for (const FHitStopParticipant& Participant : HitStopParticipants)
	{
		SetActorMontagePaused(Participant.Actor.Get(), true);
	}

	// 新命中只延长、不缩短已经在走的计时，避免短顿帧把长顿帧截断。
	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	if (TimerManager.GetTimerRemaining(HitStopTimerHandle) <= Duration)
	{
		TimerManager.SetTimer(HitStopTimerHandle, this, &UCombatFeedbackComponent::FinishHitStop, Duration, false);
	}
}

void UCombatFeedbackComponent::FinishHitStop()
{
	if (!bHitStopActive)
	{
		return;
	}

	bHitStopActive = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitStopTimerHandle);
	}
	SetComponentTickEnabled(false);

	// 恢复所有被暂停的蒙太奇；期间已经被停掉的那些会被 Montage_Resume 自动忽略。
	for (const FHitStopParticipant& Participant : HitStopParticipants)
	{
		AActor* Actor = Participant.Actor.Get();
		if (!Actor)
		{
			continue;
		}

		// 还没播完就已经被停掉的蒙太奇会被 Montage_Resume 自动忽略，不用特殊处理。
		SetActorMontagePaused(Actor, false);
		Actor->CustomTimeDilation = Participant.RestoredTimeDilation;
	}
	HitStopParticipants.Reset();
}

// 顿帧期间每帧重新确认一次暂停，这样顿帧开始之后才播放的蒙太奇（例如稍晚到达的受击反应）也会一起定住。
void UCombatFeedbackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bHitStopActive)
	{
		return;
	}

	for (const FHitStopParticipant& Participant : HitStopParticipants)
	{
		SetActorMontagePaused(Participant.Actor.Get(), true);
	}
}

void UCombatFeedbackComponent::AddHitStopParticipant(AActor* Actor, bool bScaleTimeDilation)
{
	if (!Actor)
	{
		return;
	}

	// 已经登记过的角色不重复登记，否则会把被改写后的时间膨胀当成原值存起来。
	for (const FHitStopParticipant& Participant : HitStopParticipants)
	{
		if (Participant.Actor.Get() == Actor)
		{
			return;
		}
	}

	FHitStopParticipant& NewParticipant = HitStopParticipants.AddDefaulted_GetRef();
	NewParticipant.Actor = Actor;
	NewParticipant.RestoredTimeDilation = Actor->CustomTimeDilation;

	if (bScaleTimeDilation)
	{
		Actor->CustomTimeDilation = FMath::Clamp(HitStopTargetTimeDilation, 0.01f, 1.f);
	}
}

void UCombatFeedbackComponent::SetActorMontagePaused(AActor* Actor, bool bPaused)
{
	if (!Actor)
	{
		return;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Actor->GetComponents<USkeletalMeshComponent>(MeshComponents);

	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		UAnimInstance* AnimInstance = MeshComponent ? MeshComponent->GetAnimInstance() : nullptr;
		if (!AnimInstance)
		{
			continue;
		}

		// 只暂停蒙太奇，不整体停掉动画：既不吞掉动画通知，也不影响角色移动的复制与插值。
		if (bPaused)
		{
			AnimInstance->Montage_Pause();
		}
		else
		{
			AnimInstance->Montage_Resume(nullptr);
		}
	}
}
