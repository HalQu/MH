// Fill out your copyright notice in the Description page of Project Settings.


#include "GamePlay/Character/MHCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
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

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);

}


void AMHCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

void AMHCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMHCharacter::Move);
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMHCharacter::Look);
	}
}

void AMHCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AMHCharacter::Move(const FInputActionValue& Value)
{
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

}

void AMHCharacter::Look(const FInputActionValue& Value)
{
	// Get look input as 2D vector
	const FVector2D LookVector = Value.Get<FVector2D>();

	// Apply yaw (horizontal) and pitch (vertical) rotation input
	AddControllerYawInput(LookVector.X);
	AddControllerPitchInput(LookVector.Y);

}


