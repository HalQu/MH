// Fill out your copyright notice in the Description page of Project Settings.


#include "Anim/PlayerAnimInstance.h"
#include "GamePlay/Character/MHCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MHUtils.h"
#include "GamePlay/Combat/UCombatComponent.h"
void UPlayerAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	APawn* PawnOwner = TryGetPawnOwner();
	if (!PawnOwner) return;

	MHCharacter = Cast<AMHCharacter>(PawnOwner);
	if (!MHCharacter) return;

	CharacterMovementComponent = MHCharacter->GetCharacterMovement();
	CombatComponent = MHCharacter->FindComponentByClass<UCombatComponent>();
}

void UPlayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!MHCharacter || !CharacterMovementComponent) return;
	const FVector Velocity = MHCharacter->GetVelocity();

	Speed_2D = Velocity.Size2D();

	Speed_Z = Velocity.Z;
	Rota = Utils::GetSignedAngleBetweenVectors(MHCharacter->GetActorForwardVector(), Velocity, FVector::UpVector);

	bIsInAir = CharacterMovementComponent->IsFalling();
	bIsAccelerating = CharacterMovementComponent->GetCurrentAcceleration().SizeSquared() > KINDA_SMALL_NUMBER;

	if (CombatComponent)
	{
		CombatState = CombatComponent->GetCombatState();
		MovePhase = CombatComponent->GetMovePhase();
		bIsAttacking = CombatState == EMHCombatState::Attack;
		CurrentWeaponId = CombatComponent->GetCurrentWeaponId();
	}
}
