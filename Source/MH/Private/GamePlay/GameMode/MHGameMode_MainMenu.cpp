// Fill out your copyright notice in the Description page of Project Settings.


#include "GamePlay/GameMode/MHGameMode_MainMenu.h"
#include "GamePlay/MHGameInstance.h"

AMHGameMode_MainMenu::AMHGameMode_MainMenu()
{
}

void AMHGameMode_MainMenu::HostGame(const FString& ServerName)
{
	if (UMHGameInstance* GI = GetGameInstance<UMHGameInstance>())
	{
		GI->HostSession(ServerName, true, 4);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("HostGame: UMHGameInstance not found!"));
	}
}

void AMHGameMode_MainMenu::FindGames()
{
	if (UMHGameInstance* GI = GetGameInstance<UMHGameInstance>())
	{
		GI->FindSessions(true);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("FindGames: UMHGameInstance not found!"));
	}
}

void AMHGameMode_MainMenu::JoinGame(int32 SessionIndex)
{
	if (UMHGameInstance* GI = GetGameInstance<UMHGameInstance>())
	{
		GI->JoinSelectedSession(SessionIndex);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("JoinGame: UMHGameInstance not found!"));
	}
}
