// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MHPlayerState.generated.h"

/*
 * 
 */
UCLASS()
class MH_API AMHPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	//Test ViewModel 
	UPROPERTY(BlueprintReadOnly, Category = "MH|PlayerState")
	float CurrentHealth=50.0f;
	UPROPERTY(BlueprintReadOnly, Category = "MH|PlayerState")
	float MaxHealth=100.0f;
	AMHPlayerState();
};
