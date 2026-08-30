// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "MHGameInstance.generated.h"

/**
 * 可被发现集会的信息
 */
USTRUCT(BlueprintType)
struct FSessionData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	FString ServerName;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 MaxPlayers = 4;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 Ping = 0;

	// 内部索引，供 JoinSession 使用
	int32 SessionIndex = -1;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnHostSessionComplete);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionsFound, const TArray<FSessionData>&, SessionResults);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSessionJoined);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionDestroyed, bool, bWasSuccessful);

/**
 * 游戏实例：管理网络会话（基于 LAN / OnlineSubsystemNull）
 */
UCLASS()
class MH_API UMHGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UMHGameInstance();

	// ===== 会话操作 =====

	/** 创建集会（本地作为监听服务器） */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void HostSession(const FString& ServerName, bool bIsLAN = true, int32 MaxPlayers = 4);

	/** 搜索 LAN 上可加入的集会 */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void FindSessions(bool bIsLAN = true);

	/** 加入指定索引的集会 */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void JoinSelectedSession(int32 SessionIndex);

	/** 销毁当前集会 */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void DestroySession();

	/** 获取当前查找结果中的集会数量 */
	UFUNCTION(BlueprintCallable, Category = "Session")
	int32 GetSessionResultCount() const;

	/** 获取指定索引的集会信息 */
	UFUNCTION(BlueprintCallable, Category = "Session")
	FSessionData GetSessionResult(int32 Index) const;

	// ===== 回调 / 委托 =====

	UPROPERTY(BlueprintAssignable, Category = "Session")
	FOnHostSessionComplete OnHostSessionComplete;
	UPROPERTY(BlueprintAssignable, Category = "Session")
	FOnSessionsFound OnSessionsFound;
	UPROPERTY(BlueprintAssignable, Category = "Session")
	FOnSessionJoined OnSessionJoined;
	UPROPERTY(BlueprintAssignable, Category = "Session")
	FOnSessionDestroyed OnSessionDestroyed;

private:

public:
	// ===== 存档/读档（预留接口） =====

	/** 保存玩家数据到磁盘 */
	UFUNCTION(BlueprintCallable, Category = "Save")
	void SavePlayerData();

	/** 从磁盘读取玩家数据 */
	UFUNCTION(BlueprintCallable, Category = "Save")
	void LoadPlayerData();
protected:
	// 在线子系统会话接口
	IOnlineSessionPtr SessionInterface;
	TSharedPtr<FOnlineSessionSearch> SessionSearch;


	/** 创建完成回调 */
	FDelegateHandle OnCreateSessionCompleteHandle;
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	/** 搜索完成回调 */
	FDelegateHandle OnFindSessionsCompleteHandle;
	void OnFindSessionsComplete(bool bWasSuccessful);

	/** 加入完成回调 */
	FDelegateHandle OnJoinSessionCompleteHandle;
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	/** 销毁完成回调 */
	FDelegateHandle OnDestroySessionCompleteHandle;
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);


	/** 待创建的集会信息（ServerTravel 到达目标关卡后使用） */
	FString PendingServerName;
	int32 PendingMaxPlayers = 4;

	/** 客户端加入时从搜索结果中提取的连接地址（后备 GetResolvedConnectString） */
	FString PendingConnectString;

	/** 关卡加载完成后回调：用于到达 LobbyMap 后正式创建会话 */
	virtual void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld) override;
// ============================================================================
};
