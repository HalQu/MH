// Fill out your copyright notice in the Description page of Project Settings.


#include "GamePlay/Character/MHCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "GamePlay/Combat/UCombatComponent.h"
#include "GamePlay/Combat/UCombatFeedbackComponent.h"
#include "GamePlay/Combat/UHitReactionComponent.h"
#include "GamePlay/Combat/UHealthComponent.h"
// Sets default values
AMHCharacter::AMHCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;

	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetCapsuleComponent()->SetGenerateOverlapEvents(false);

	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetGenerateOverlapEvents(true);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Spring Arm"));
	SpringArm->SetupAttachment(GetRootComponent());
	SpringArm->TargetArmLength = 600.f;
	SpringArm->bUsePawnControlRotation = true; // move spring arm with mouse
	SpringArm->SetupAttachment(GetMesh()); // attach to mesh so it rotates with the character

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->bUsePawnControlRotation = false; // prevents camera from moving independently

	CombatComponent = CreateDefaultSubobject<UCombatComponent>(TEXT("CombatComponent"));
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	HitReactionComponent = CreateDefaultSubobject<UHitReactionComponent>(TEXT("HitReactionComponent"));
	CombatFeedbackComponent = CreateDefaultSubobject<UCombatFeedbackComponent>(TEXT("CombatFeedbackComponent"));

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);

}


void AMHCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AMHCharacter::HandleDeath);
	}
}

FMHDamageResult AMHCharacter::ReceiveDamage_Implementation(const FMHDamageEvent& DamageEvent)
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

void AMHCharacter::HandleDeath()
{
	// 死亡后不再接受输入、也不再开新动作；进行中的动作由组件自己收尾。
	if (CombatComponent)
	{
		CombatComponent->SetCombatEnabled(false);
	}
}

void AMHCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInput->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AMHCharacter::Move);
		EnhancedInput->BindAction(IA_Move, ETriggerEvent::Completed, this, &AMHCharacter::StopMove);
		EnhancedInput->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AMHCharacter::Look);
		if (IA_Attack_Y)
		{
			// Triggered fires every frame while held; attacks are edge-driven.
			EnhancedInput->BindAction(IA_Attack_Y, ETriggerEvent::Started, this, &AMHCharacter::HandleAttackInput, IA_Attack_Y, ETriggerEvent::Started);
			EnhancedInput->BindAction(IA_Attack_Y, ETriggerEvent::Completed, this, &AMHCharacter::HandleAttackInput, IA_Attack_Y, ETriggerEvent::Completed);
		}
		if (IA_Attack_B)
		{
			EnhancedInput->BindAction(IA_Attack_B, ETriggerEvent::Started, this, &AMHCharacter::HandleAttackInput, IA_Attack_B, ETriggerEvent::Started);
			EnhancedInput->BindAction(IA_Attack_B, ETriggerEvent::Completed, this, &AMHCharacter::HandleAttackInput, IA_Attack_B, ETriggerEvent::Completed);
		}

		if (IA_Jump)
		{
			EnhancedInput->BindAction(IA_Jump, ETriggerEvent::Started, this, &AMHCharacter::HandleJumpPressed);
			EnhancedInput->BindAction(IA_Jump, ETriggerEvent::Completed, this, &AMHCharacter::HandleJumpReleased);
		}

		if (IA_EquipWeapon)
		{
			EnhancedInput->BindAction(IA_EquipWeapon, ETriggerEvent::Started, this, &AMHCharacter::EquipWeapon);
		}

	}
}

void AMHCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AMHCharacter::Move(const FInputActionValue& Value)
{
	if (HitReactionComponent && HitReactionComponent->IsMovementLocked())
	{
		return;
	}

	// Retrieve 2D input vector (X: right/left, Y: forward/backward)
	InputVector = Value.Get<FVector2D>();

	// Get Yaw rotation from controller from movement direction
	const FRotator YawRotation(0.0f, GetControlRotation().Yaw, 0.0f);

	// Calculate forward and right directions based on Yaw rotation
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// Add movement input to pawn if valid

	AddMovementInput(Forward, InputVector.Y);
	AddMovementInput(Right, InputVector.X);
	if (CombatComponent)
	{
		CombatComponent->OnMove(InputVector);
	}
}

void AMHCharacter::StopMove()
{
	InputVector = FVector2D::ZeroVector;
	if (CombatComponent)
	{
		CombatComponent->OnMove(InputVector);
	}
}

void AMHCharacter::HandleJumpPressed()
{
	if (HitReactionComponent && HitReactionComponent->IsMovementLocked())
	{
		return;
	}

	Jump();
}

void AMHCharacter::HandleJumpReleased()
{
	StopJumping();
}



void AMHCharacter::Look(const FInputActionValue& Value)
{
	// Get look input as 2D vector
	const FVector2D LookVector = Value.Get<FVector2D>();

	// Apply yaw (horizontal) and pitch (vertical) rotation input
	AddControllerYawInput(LookVector.X);
	AddControllerPitchInput(LookVector.Y);

}


void AMHCharacter::EquipWeapon(const FInputActionValue& Value)
{
	if (HitReactionComponent && HitReactionComponent->IsReacting())
	{
		return;
	}

	if (CombatComponent)
	{
		CombatComponent->EquipWeapon_Default();
	}
}
void AMHCharacter::HandleAttackInput(const FInputActionValue&, TObjectPtr<UInputAction> InputAction, ETriggerEvent TriggerEvent)
{
	if (CombatComponent && InputAction)
	{
		CombatComponent->HandleComboInput(InputAction, TriggerEvent);
	}
}
