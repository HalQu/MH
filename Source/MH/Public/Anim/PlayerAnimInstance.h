// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "PlayerAnimInstance.generated.h"

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
};
