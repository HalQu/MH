// Fill out your copyright notice in the Description page of Project Settings.

#include "GamePlay/MHPlayerController.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "GamePlay/MHGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Core/BaseScreen.h"
#include "UI/Core/UIManager.h"

AMHPlayerController::AMHPlayerController()
{
	bReplicates = true;
}

UBaseScreen* AMHPlayerController::OpenPersistentScreenLocal(
	FName ScreenID,
	TSubclassOf<UBaseScreen> ScreenClass,
	EUIScreenInputMode InputMode)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UUIManager* UIManager = LocalPlayer ? LocalPlayer->GetSubsystem<UUIManager>() : nullptr;
	return UIManager ? UIManager->OpenPersistentScreen(ScreenID, ScreenClass, InputMode) : nullptr;
}

void AMHPlayerController::Client_OpenPersistentScreen_Implementation(
	FName ScreenID,
	TSubclassOf<UBaseScreen> ScreenClass,
	EUIScreenInputMode InputMode)
{
	OpenPersistentScreenLocal(ScreenID, ScreenClass, InputMode);
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

