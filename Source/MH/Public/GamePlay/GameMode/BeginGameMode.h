// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "BeginGameMode.generated.h"

/**
 * 
 */
UCLASS()
class MH_API ABeginGameMode : public AGameMode
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen")
	TSubclassOf<class UBaseScreen> MainMenuScreenClass;
};
