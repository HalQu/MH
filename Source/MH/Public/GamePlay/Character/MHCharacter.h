// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GamePlay/Character/MHCombatCharacterBase.h"
#include "MHCharacter.generated.h"

struct FInputActionValue;
enum class ETriggerEvent : uint8;
class UInputMappingContext;
class UInputAction;

/**
 * 玩家角色：只保留玩家专属的相机与输入绑定。
 * 战斗组件的装配、受伤与死亡收尾都在 AMHCombatCharacterBase，怪物走同一条路径。
 */

UCLASS()
class MH_API AMHCharacter : public AMHCombatCharacterBase
{
	GENERATED_BODY()

public:

	AMHCharacter();


	virtual void Tick(float DeltaTime) override;


	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	// 相机：只属于玩家，怪物不需要也不该有弹簧臂。
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	class USpringArmComponent* SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	class UCameraComponent* Camera;

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



	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Test")
	UAnimMontage* AttackMontage;
private:
	FVector2D InputVector;

	/** 硬直或本机顿帧期间，移动与跳跃输入一律拒绝。 */
	bool IsMovementInputBlocked() const;
	void Move(const FInputActionValue& Value);
	void StopMove();
	void Look(const FInputActionValue& Value);

	void EquipWeapon(const FInputActionValue& Value);
	void HandleAttackInput(const FInputActionValue& Value, TObjectPtr<UInputAction> InputAction, ETriggerEvent TriggerEvent);
	void HandleJumpPressed();
	void HandleJumpReleased();
};
