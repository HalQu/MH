// Fill out your copyright notice in the Description page of Project Settings.

#include "GamePlay/GameMode/MHGameMode_Lobby.h"
#include "GamePlay/MHPlayerController.h"
#include "GamePlay/Character/MHCharacter.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

AMHGameMode_Lobby::AMHGameMode_Lobby()
{
	bUseSeamlessTravel = true;
	DefaultPawnClass = AMHCharacter::StaticClass();
}

void AMHGameMode_Lobby::BeginPlay()
{
	Super::BeginPlay();

}

void AMHGameMode_Lobby::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

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

int32 AMHGameMode_Lobby::GetPlayerCount()
{
	return GetNumPlayers();
}

