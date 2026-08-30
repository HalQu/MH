// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "MHGameMode_MainMenu.generated.h"

/**
 * 主菜单游戏模式
 * 管理创建集会 / 查找集会等入口逻辑
 */
UCLASS()
class MH_API AMHGameMode_MainMenu : public AGameMode
{
	GENERATED_BODY()

public:
	AMHGameMode_MainMenu();

	/** 主持集会（创建 LAN Session 并跳转 LobbyMap） */
	UFUNCTION(BlueprintCallable, Category = "Game")
	void HostGame(const FString& ServerName);

	/** 搜索可加入的集会（蓝图触发，通过 GameInstance 委托返回结果） */
	UFUNCTION(BlueprintCallable, Category = "Game")
	void FindGames();

	/** 加入指定集会 */
	UFUNCTION(BlueprintCallable, Category = "Game")
	void JoinGame(int32 SessionIndex);
};
