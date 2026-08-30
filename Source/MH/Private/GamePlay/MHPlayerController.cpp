// Fill out your copyright notice in the Description page of Project Settings.

#include "GamePlay/MHPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "GamePlay/MHGameInstance.h"
#include "Kismet/GameplayStatics.h"

AMHPlayerController::AMHPlayerController()
{
	bReplicates = false;
}

void AMHPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (BaseContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(BaseContext, 0);
		}
	}
}

void AMHPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
}

