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
	AMHGameMode_Hunting();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;

	/** 房主请求提前结束本局，结果展示后统一返回集会所。 */
	void RequestEndHunt(APlayerController* RequestingController, const FText& ResultText);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen")
	TSubclassOf<class UBaseScreen> HuntingHUDClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunt", meta = (ClampMin = "10.0"))
	float HuntDuration = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunt", meta = (ClampMin = "0.0"))
	float ResultDisplayDuration = 3.f;

private:
	void FinishHunt(const FText& ResultText);
	bool AreAllPlayersDead() const;

	float TimeUntilLobbyTravel = -1.f;
};
