// Fill out your copyright notice in the Description page of Project Settings.

#include "GamePlay/MHPlayerController.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "GamePlay/MHGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Core/BaseScreen.h"
#include "UI/Core/UIManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GamePlay/GameMode/MHGameMode_Hunting.h"
#include "GamePlay/GameMode/MHGameMode_Lobby.h"
#include "GamePlay/MHGameState_Hunting.h"
#include "UI/Screen/HuntMenuScreen.h"

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

void AMHPlayerController::RequestLeaveToMainMenu()
{
	if (HasAuthority())
	{
		for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			AMHPlayerController* OtherController = Cast<AMHPlayerController>(Iterator->Get());
			if (OtherController && OtherController != this)
			{
				OtherController->Client_ReturnToMainMenu();
			}
		}
	}

	if (UMHGameInstance* GameInstance = GetGameInstance<UMHGameInstance>())
	{
		GameInstance->ReturnToMainMenu();
	}
}

void AMHPlayerController::Client_ReturnToMainMenu_Implementation()
{
	if (UMHGameInstance* GameInstance = GetGameInstance<UMHGameInstance>())
	{
		GameInstance->ReturnToMainMenu();
	}
}

void AMHPlayerController::Server_SetReady_Implementation(bool bNewReady)
{
	if (AMHGameMode_Lobby* LobbyMode = GetWorld()->GetAuthGameMode<AMHGameMode_Lobby>())
	{
		LobbyMode->SetPlayerReady(this, bNewReady);
	}
}

void AMHPlayerController::Server_RequestStartHunt_Implementation()
{
	if (AMHGameMode_Lobby* LobbyMode = GetWorld()->GetAuthGameMode<AMHGameMode_Lobby>())
{
		LobbyMode->RequestStartHunt(this);
	}
}

void AMHPlayerController::Server_RequestEndHunt_Implementation()
{
	if (AMHGameMode_Hunting* HuntingMode = GetWorld()->GetAuthGameMode<AMHGameMode_Hunting>())
	{
		HuntingMode->RequestEndHunt(this, NSLOCTEXT("MHFlow", "HostEndedHunt", "房主结束了狩猎"));
	}
}

void AMHPlayerController::Server_RequestReturnToLobby_Implementation()
{
	if (AMHGameMode_Hunting* HuntingMode = GetWorld()->GetAuthGameMode<AMHGameMode_Hunting>())
	{
		HuntingMode->RequestEndHunt(this, NSLOCTEXT("MHFlow", "ReturningToLobby", "正在返回集会所"));
	}
}

void AMHPlayerController::ToggleHuntMenu()
{
	if (!IsLocalController() || !GetWorld() || !GetWorld()->GetGameState<AMHGameState_Hunting>())
	{
		return;
	}

	UUIManager* UIManager = UUIManager::GetUIManager(this);
	if (!UIManager)
{
		return;
	}

	if (UIManager->GetTopScreen(EUILayer::Overlay))
{
		UIManager->PopOverlay();
}
	else
{
		UIManager->PushOverlay(UMHHuntMenuScreen::StaticClass());
}
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

	if (InputComponent)
	{
		InputComponent->BindAction(TEXT("PauseMenu"), IE_Pressed, this, &AMHPlayerController::ToggleHuntMenu);
	}
}

