// Fill out your copyright notice in the Description page of Project Settings.

#include "GamePlay/GameMode/MHGameMode_Lobby.h"
#include "GamePlay/MHPlayerController.h"
#include "GamePlay/Character/MHCharacter.h"
#include "GameFramework/DefaultPawn.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GamePlay/MHPlayerState.h"
#include "UI/Core/BaseScreen.h"
#include "UI/Core/UIManager.h"
#include "UI/Screen/LobbyScreen.h"

namespace
{
	bool IsFlowAutoTestEnabled()
	{
		FString Mode;
		return FParse::Value(FCommandLine::Get(), TEXT("MHAutoTest="), Mode) && !Mode.IsEmpty();
	}

	void OpenLobbyScreenForPlayer(APlayerController* PlayerController, TSubclassOf<UBaseScreen> ScreenClass)
	{
		if (!PlayerController || !ScreenClass)
		{
			return;
		}

		if (PlayerController->IsLocalController())
		{
			UUIManager::OpenPersistentScreenForLocalPlayer(
				PlayerController,
				FName(TEXT("Lobby")),
				ScreenClass,
				EUIScreenInputMode::UIOnly);
			return;
		}

		if (AMHPlayerController* MHController = Cast<AMHPlayerController>(PlayerController))
		{
			MHController->Client_OpenPersistentScreen(
				FName(TEXT("Lobby")),
				ScreenClass,
				EUIScreenInputMode::UIOnly);
		}
	}
}
AMHGameMode_Lobby::AMHGameMode_Lobby()
{
	bUseSeamlessTravel = true;
	DefaultPawnClass = AMHCharacter::StaticClass();
	PlayerControllerClass = AMHPlayerController::StaticClass();
	PlayerStateClass = AMHPlayerState::StaticClass();
	LobbyScreenClass = ULobbyScreen::StaticClass();
}

void AMHGameMode_Lobby::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// 自动测试不依赖角色表现，避开首次运行时的巨型骨骼网格派生数据构建。
	if (IsFlowAutoTestEnabled())
	{
		DefaultPawnClass = ADefaultPawn::StaticClass();
	}
}

void AMHGameMode_Lobby::BeginPlay()
{
	Super::BeginPlay();

	// Lobby -> Hunting -> Lobby 的往返会保留 PlayerState，重新回到房间时清空准备状态。
	if (AGameStateBase* GameStateBase = GetGameState<AGameStateBase>())
	{
		for (APlayerState* PlayerState : GameStateBase->PlayerArray)
		{
			if (AMHPlayerState* MHPlayerState = Cast<AMHPlayerState>(PlayerState))
			{
				MHPlayerState->SetReady(false);
			}
		}
	}

	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		OpenLobbyScreenForPlayer(Iterator->Get(), LobbyScreenClass);
		if (APlayerController* PlayerController = Iterator->Get())
		{
			if (AMHPlayerState* PlayerState = PlayerController->GetPlayerState<AMHPlayerState>())
			{
				PlayerState->SetIsHost(PlayerController->IsLocalController());
			}
		}
	}
}

void AMHGameMode_Lobby::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (AMHPlayerState* PlayerState = NewPlayer ? NewPlayer->GetPlayerState<AMHPlayerState>() : nullptr)
{
		PlayerState->SetIsHost(NewPlayer->IsLocalController());
		PlayerState->SetReady(false);
}

	OpenLobbyScreenForPlayer(NewPlayer, LobbyScreenClass);

	int32 PlayerCount = GetPlayerCount();
#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("Player joined lobby. Total players: %d"), PlayerCount);
#endif

	// UI 刷新由客户端自行处理（监听 PostLogin / Logout）
}

void AMHGameMode_Lobby::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	int32 PlayerCount = GetPlayerCount();
#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("Player left lobby. Total players: %d"), PlayerCount);
#endif

	// UI 刷新由客户端自行处理（监听 PostLogin / Logout）
}

int32 AMHGameMode_Lobby::GetPlayerCount() const
{
	return GameState ? GameState->PlayerArray.Num() : 0;
}

void AMHGameMode_Lobby::SetPlayerReady(APlayerController* PlayerController, bool bNewReady)
{
	if (!HasAuthority() || !PlayerController)
{
		return;
}

	if (AMHPlayerState* PlayerState = PlayerController->GetPlayerState<AMHPlayerState>())
{
		PlayerState->SetReady(bNewReady);
	}
}

bool AMHGameMode_Lobby::AreAllPlayersReady() const
{
	const AGameStateBase* GameStateBase = GetGameState<AGameStateBase>();
	if (!GameStateBase || GameStateBase->PlayerArray.Num() == 0)
{
		return false;
}

	for (APlayerState* PlayerState : GameStateBase->PlayerArray)
{
		const AMHPlayerState* MHPlayerState = Cast<AMHPlayerState>(PlayerState);
		if (!MHPlayerState || !MHPlayerState->bReady)
		{
			return false;
		}
	}

	return true;
}

bool AMHGameMode_Lobby::CanStartHunt() const
{
	return GetPlayerCount() > 0 && AreAllPlayersReady();
}

void AMHGameMode_Lobby::RequestStartHunt(APlayerController* RequestingController)
{
	if (!HasAuthority() || !RequestingController)
{
		return;
}

	// 监听服务器上只有房主是本地控制器；服务器权威本身不能区分房主和远程客户端。
	if (!RequestingController->IsLocalController())
	{
		return;
	}

	if (!CanStartHunt())
{
		return;
}

	UE_LOG(LogTemp, Log, TEXT("Lobby: host started hunt with %d player(s)."), GetPlayerCount());
	const FString TravelURL = TEXT("/Game/Levels/HuntingMap?listen?game=/Script/MH.MHGameMode_Hunting");

	GetWorld()->ServerTravel(TravelURL);
}
