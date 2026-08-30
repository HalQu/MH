// Fill out your copyright notice in the Description page of Project Settings.


#include "GamePlay/GameMode/BeginGameMode.h"
#include "UI/Core/UIManager.h"
void ABeginGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (UUIManager* UI = GetWorld()->GetSubsystem<UUIManager>())
	{
		UI->OpenPersistentScreen(FName("MainMenu"), MainMenuScreenClass);
	}
}
