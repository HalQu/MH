// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "PlayerAnimInstance.generated.h"

class UCombatComponent;

/**
 * 
 */
UCLASS()
class MH_API UPlayerAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// Cached References
	UPROPERTY()
	class AMHCharacter* MHCharacter = nullptr;

	UPROPERTY()
	class UCharacterMovementComponent* CharacterMovementComponent;

	UPROPERTY()
	TObjectPtr<class UCombatComponent> CombatComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	bool bIsInAir = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	bool bIsAccelerating = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	bool bIsMoving = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float Speed_2D = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float Speed_Z = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float Rota = 0.f;

	// Combat state exposed to the animation graph. The AnimInstance never mutates gameplay state.
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Combat")
	EMHCombatState CombatState = EMHCombatState::Locomotion;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Combat")
	EMHCombatMovePhase MovePhase = EMHCombatMovePhase::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bIsAttacking = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Combat")
	FName CurrentWeaponId = NAME_None;
};
