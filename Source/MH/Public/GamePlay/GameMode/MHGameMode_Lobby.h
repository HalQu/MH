// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "MHGameMode_Lobby.generated.h"

class AQuestBoard;
class UDataTable;
class APlayerController;
class UBaseScreen;

/**
 * 集会所游戏模式
 */
UCLASS()
class MH_API AMHGameMode_Lobby : public AGameMode
{
	GENERATED_BODY()

public:
	AMHGameMode_Lobby();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	/** 获取当前集会中的玩家数量 */
	UFUNCTION(BlueprintCallable, Category = "Game")
	int32 GetPlayerCount() const;

	/** 集会所界面；蓝图未覆盖时使用 C++ 流程页面。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen")
	TSubclassOf<class UBaseScreen> LobbyScreenClass;

	/** 服务器更新一名玩家的准备状态。 */
	void SetPlayerReady(APlayerController* PlayerController, bool bNewReady);

	/** 只有房主能开始狩猎；要求所有已连接玩家准备完成。 */
	void RequestStartHunt(APlayerController* RequestingController);

	UFUNCTION(BlueprintPure, Category = "Game")
	bool CanStartHunt() const;

	UFUNCTION(BlueprintPure, Category = "Game")
	bool AreAllPlayersReady() const;

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
