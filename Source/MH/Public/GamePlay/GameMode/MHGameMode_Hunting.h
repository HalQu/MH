// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "MHGameMode_Hunting.generated.h"

class APlayerController;

/**
 * 
 */
UCLASS()
class MH_API AMHGameMode_Hunting : public AGameMode
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen")
	TSubclassOf<class UBaseScreen> HuntingHUDClass;
};
