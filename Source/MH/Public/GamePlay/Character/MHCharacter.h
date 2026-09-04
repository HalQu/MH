// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MHCharacter.generated.h"

struct FInputActionValue;
class UInputMappingContext;
class UInputAction;
class UCombatComponent;

UCLASS()
class MH_API AMHCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	AMHCharacter();


	virtual void Tick(float DeltaTime) override;


	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	//Camera
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	class USpringArmComponent* SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	class UCameraComponent* Camera;

	//Combat
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<class UCombatComponent> CombatComponent;

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
	//Test
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Test")
	UAnimMontage* AttackMontage;
private:
	FVector2D InputVector;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	void EquipWeapon(const FInputActionValue& Value);
	void OnYPressed(const FInputActionValue& Value);
	void OnYReleased(const FInputActionValue& Value);
	void OnBPressed(const FInputActionValue& Value);
	void OnBReleased(const FInputActionValue& Value);
};
