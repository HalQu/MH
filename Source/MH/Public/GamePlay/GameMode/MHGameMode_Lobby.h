// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "MHGameMode_Lobby.generated.h"

class AQuestBoard;
class UDataTable;

/**
 * 集会所游戏模式
 */
UCLASS()
class MH_API AMHGameMode_Lobby : public AGameMode
{
	GENERATED_BODY()

public:
	AMHGameMode_Lobby();

	/** 获取当前集会中的玩家数量 */
	UFUNCTION(BlueprintCallable, Category = "Game")
	int32 GetPlayerCount();

	/** 任务数据表（在蓝图中指定） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	TObjectPtr<UDataTable> QuestDataTable;

	// 玩家加入/离开事件
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

protected:
	virtual void BeginPlay() override;

private:

};
