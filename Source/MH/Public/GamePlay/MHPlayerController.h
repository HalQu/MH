// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MHPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;


UCLASS()
class MH_API AMHPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AMHPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputMappingContext> BaseContext;


};
