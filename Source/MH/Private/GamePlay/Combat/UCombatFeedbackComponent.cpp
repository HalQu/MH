#include "GamePlay/Combat/UCombatFeedbackComponent.h"

#include "Camera/CameraShakeBase.h"
#include "Particles/ParticleSystemComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GamePlay/Combat/UCombatComponent.h"
#include "Kismet/GameplayStatics.h"

UCombatFeedbackComponent::UCombatFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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
	if (UCombatComponent* CombatComponent = BoundCombatComponent.Get())
	{
		CombatComponent->OnHitConfirmed.RemoveDynamic(this, &UCombatFeedbackComponent::HandleHitConfirmed);
	}

	BoundCombatComponent.Reset();
	Super::EndPlay(EndPlayReason);
}

void UCombatFeedbackComponent::HandleHitConfirmed(const FMHCombatHitEvent& HitEvent)
{
	OnHitFeedbackReceived.Broadcast(HitEvent);
	BP_OnHitFeedback(HitEvent);

	SpawnWorldFeedback(HitEvent);
	ApplyLocalPlayerFeedback(HitEvent);
}

void UCombatFeedbackComponent::SpawnWorldFeedback(const FMHCombatHitEvent& HitEvent)
{
	if (!bEnableWorldEffects || !GetWorld() || !HitEvent.Attacker || HitEvent.Attacker != GetOwner())
	{
		return;
	}

	const FTransform HitTransform(HitEvent.HitNormal.Rotation(), HitEvent.HitLocation);

	if (HitEvent.Feedback.ImpactParticle)
	{
		if (UParticleSystem* Particle = HitEvent.Feedback.ImpactParticle.LoadSynchronous())
		{
			if (UParticleSystemComponent* SpawnedEffect = UGameplayStatics::SpawnEmitterAtLocation(
				GetWorld(),
				Particle,
				HitTransform,
				true,
				EPSCPoolMethod::AutoRelease,
				true))
			{
				if (AActor* EffectOwner = SpawnedEffect->GetOwner())
				{
					EffectOwner->SetReplicates(false);
				}
				// The hit event already reaches every client. Prevent the server-spawned
				// effects from being replicated a second time to remote clients.
			}
		}
	}

	if (bEnableImpactSound && HitEvent.Feedback.ImpactSound)
	{
		if (USoundBase* Sound = HitEvent.Feedback.ImpactSound.LoadSynchronous())
		{
			if (UAudioComponent* AudioComponent = UGameplayStatics::SpawnSoundAtLocation(
				GetWorld(),
				Sound,
				HitEvent.HitLocation,
				HitEvent.HitNormal.Rotation()))
			{
				AudioComponent->SetIsReplicated(false);
			}
		}
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
