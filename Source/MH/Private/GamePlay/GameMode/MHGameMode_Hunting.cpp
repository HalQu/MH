// Fill out your copyright notice in the Description page of Project Settings.


#include "GamePlay/GameMode/MHGameMode_Hunting.h"
#include "UI/Core/UIManager.h"

void AMHGameMode_Hunting::BeginPlay()
{
	Super::BeginPlay();
	if (UUIManager* UI = GetWorld()->GetSubsystem<UUIManager>())
	{
		UI->OpenPersistentScreen(FName("HUD"), HuntingHUDClass);
	}
}