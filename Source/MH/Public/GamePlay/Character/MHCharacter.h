// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GamePlay/Combat/IMHCombatTargetInterface.h"
#include "MHCharacter.generated.h"

struct FInputActionValue;
enum class ETriggerEvent : uint8;
class UInputMappingContext;
class UInputAction;
class UCombatComponent;
class UHealthComponent;
class UHitReactionComponent;
class UCombatFeedbackComponent;

UCLASS()
class MH_API AMHCharacter : public ACharacter, public IMHCombatTargetInterface
{
	GENERATED_BODY()

public:

	AMHCharacter();


	virtual void Tick(float DeltaTime) override;


	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/** IMHCombatTargetInterface：收到伤害请求，实际扣血由 UHealthComponent 处理。 */
	virtual FMHDamageResult ReceiveDamage_Implementation(const FMHDamageEvent& DamageEvent) override;

	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Combat|HitReaction")
	UHitReactionComponent* GetHitReactionComponent() const { return HitReactionComponent; }

	UFUNCTION(BlueprintPure, Category = "Combat|Feedback")
	UCombatFeedbackComponent* GetCombatFeedbackComponent() const { return CombatFeedbackComponent; }

protected:

	//Camera
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	class USpringArmComponent* SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	class UCameraComponent* Camera;

	//Combat
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<class UCombatComponent> CombatComponent;

	//Combat|Health
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Health")
	TObjectPtr<class UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|HitReaction")
	TObjectPtr<class UHitReactionComponent> HitReactionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Feedback")
	TObjectPtr<class UCombatFeedbackComponent> CombatFeedbackComponent;

	//input
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<class UInputMappingContext> InputMappingContext;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<class UInputAction> IA_Move;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<class UInputAction> IA_Look;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<class UInputAction> IA_Attack_Y;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<class UInputAction> IA_Attack_B;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<class UInputAction> IA_Jump;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<class UInputAction> IA_EquipWeapon;



	virtual void BeginPlay() override;
	UFUNCTION()
	void HandleDeath();
	//Test
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Test")
	UAnimMontage* AttackMontage;
private:
	FVector2D InputVector;
	void Move(const FInputActionValue& Value);
	void StopMove();
	void Look(const FInputActionValue& Value);

	void EquipWeapon(const FInputActionValue& Value);
	void HandleAttackInput(const FInputActionValue& Value, TObjectPtr<UInputAction> InputAction, ETriggerEvent TriggerEvent);
	void HandleJumpPressed();
	void HandleJumpReleased();
};
