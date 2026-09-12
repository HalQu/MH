// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "BeginGameMode.generated.h"

class APlayerController;

/**
 * 
 */
UCLASS()
class MH_API ABeginGameMode : public AGameMode
{
	GENERATED_BODY()
public:
	ABeginGameMode();

	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen")
	TSubclassOf<class UBaseScreen> MainMenuScreenClass;
};
