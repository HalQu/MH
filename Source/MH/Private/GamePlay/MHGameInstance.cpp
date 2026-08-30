// Fill out your copyright notice in the Description page of Project Settings.

#include "GamePlay/MHGameInstance.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GamePlay/MHPlayerController.h"
#include "GamePlay/Character/MHCharacter.h"

UMHGameInstance::UMHGameInstance()
{
}

// ============================================================================
// 关卡切换回调：到达 LobbyMap 后创建会话，切换地图后刷新 UI
// ============================================================================
void UMHGameInstance::OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld)
{
	Super::OnWorldChanged(OldWorld, NewWorld);

	if (NewWorld==nullptr) return;

	FString MapName = UGameplayStatics::GetCurrentLevelName(NewWorld);

#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("=== OnWorldChanged ==="));
	UE_LOG(LogTemp, Log, TEXT("Map=%s, PendingServerName=%s"), *MapName, *PendingServerName);
#endif

	// 主机：刚刚 ServerTravel 到 LobbyMap，现在创建会话（此时 NetDriver 已有监听端口）
	if (MapName.Contains("LobbyMap") && !PendingServerName.IsEmpty())
	{
		IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
		if (Subsystem)
		{
			SessionInterface = Subsystem->GetSessionInterface();
			if (SessionInterface.IsValid())
			{
				// 先销毁引擎自动创建的临时会话
				FNamedOnlineSession* Existing = SessionInterface->GetNamedSession(NAME_GameSession);
				if (Existing)
				{
					SessionInterface->DestroySession(NAME_GameSession);
				}

				FOnlineSessionSettings SessionSettings;
				SessionSettings.bIsLANMatch = true;
				SessionSettings.bShouldAdvertise = true;
				SessionSettings.bUsesPresence = true;
				SessionSettings.bUseLobbiesIfAvailable = false;
				SessionSettings.NumPublicConnections = PendingMaxPlayers;
				SessionSettings.bAllowJoinInProgress = true;
				SessionSettings.bAllowInvites = true;
				SessionSettings.bAllowJoinViaPresence = true;
				SessionSettings.Set(FName("SERVER_NAME"), PendingServerName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

				SessionInterface->OnCreateSessionCompleteDelegates.Remove(OnCreateSessionCompleteHandle);
				OnCreateSessionCompleteHandle = SessionInterface->OnCreateSessionCompleteDelegates.AddUObject(this, &UMHGameInstance::OnCreateSessionComplete);

				if (!SessionInterface->CreateSession(0, NAME_GameSession, SessionSettings))
				{
					UE_LOG(LogTemp, Error, TEXT("OnWorldChanged: CreateSession failed!"));
				}

#if MH_DEBUG
				UE_LOG(LogTemp, Log, TEXT("OnWorldChanged: CreateSession called (port should now be valid)"));
#endif
			}
		}
		PendingServerName.Empty();
	}

}


// ============================================================================
// 主机：创建集会
// ============================================================================
void UMHGameInstance::HostSession(const FString& ServerName, bool bIsLAN, int32 MaxPlayers)
{
#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("=== HostSession CALLED ==="));
	UE_LOG(LogTemp, Log, TEXT("ServerName=%s, bIsLAN=%d, MaxPlayers=%d"), *ServerName, bIsLAN, MaxPlayers);
#endif

	// 保存待创建信息，ServerTravel 到 LobbyMap 后由 OnWorldChanged 创建会话（此时 NetDriver 已有端口）
	PendingServerName = ServerName;
	PendingMaxPlayers = MaxPlayers;

	GetWorld()->ServerTravel("/Game/Levels/LobbyMap?listen");
}

void UMHGameInstance::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("=== OnCreateSessionComplete CALLED ==="));
	UE_LOG(LogTemp, Log, TEXT("SessionName=%s, bWasSuccessful=%d"), *SessionName.ToString(), bWasSuccessful);
#endif

	if (bWasSuccessful)
	{
		int32 ListenPort = GetWorld() ? GetWorld()->URL.Port : 0;
#if WITH_EDITOR
		if (ListenPort > 0 && GetWorld() && GetWorld()->WorldType == EWorldType::PIE)
		{
			ListenPort += 10000;
#if MH_DEBUG
			UE_LOG(LogTemp, Log, TEXT("OnCreateSessionComplete: PIE adjusted port %d -> %d"), GetWorld()->URL.Port, ListenPort);
#endif
		}
#endif
		if (ListenPort > 0)
		{
			FString ConnectStr = FString::Printf(TEXT("127.0.0.1:%d"), ListenPort);
#if MH_DEBUG
			UE_LOG(LogTemp, Log, TEXT("OnCreateSessionComplete: Saving CONNECT_STR='%s'"), *ConnectStr);
#endif
			if (SessionInterface.IsValid())
			{
				FNamedOnlineSession* Session = SessionInterface->GetNamedSession(SessionName);
				if (Session)
				{
					Session->SessionSettings.Set(FName("CONNECT_STR"), ConnectStr,
						EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
					SessionInterface->UpdateSession(SessionName, Session->SessionSettings, true);
				}
			}
		}
		OnHostSessionComplete.Broadcast();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("OnCreateSessionComplete: FAILED!"));
	}

}

// ============================================================================
// 搜索 LAN 上的集会
// ============================================================================
void UMHGameInstance::FindSessions(bool bIsLAN)
{
#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("=== FindSessions CALLED ==="));
	UE_LOG(LogTemp, Log, TEXT("bIsLAN=%d"), bIsLAN);
#endif

	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("FindSessions: No OnlineSubsystem found!"));
		return;
	}

#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("FindSessions: OnlineSubsystem=%s (ptr=%p)"), *Subsystem->GetSubsystemName().ToString(), Subsystem);
#endif

	SessionInterface = Subsystem->GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("FindSessions: No SessionInterface!"));
		return;
	}

	SessionSearch = MakeShareable(new FOnlineSessionSearch());
	SessionSearch->bIsLanQuery = bIsLAN;
	SessionSearch->MaxSearchResults = 20;

	// FOnlineSessionSearch 构造函数默认设 SEARCH_PRESENCE=true，与主机 bUsesPresence=true 一致
	// 不做覆盖，避免过滤条件不匹配

#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("FindSessions: bIsLanQuery=%d, MaxSearchResults=%d"), SessionSearch->bIsLanQuery, SessionSearch->MaxSearchResults);
	UE_LOG(LogTemp, Log, TEXT("FindSessions: QuerySettings count=%d"), SessionSearch->QuerySettings.SearchParams.Num());
	for (const auto& Param : SessionSearch->QuerySettings.SearchParams)
	{
		UE_LOG(LogTemp, Log, TEXT("  QueryParam: %s = %s"), *Param.Key.ToString(), *Param.Value.Data.ToString());
	}
#endif

	SessionInterface->OnFindSessionsCompleteDelegates.Remove(OnFindSessionsCompleteHandle);
	OnFindSessionsCompleteHandle = SessionInterface->OnFindSessionsCompleteDelegates.AddUObject(this, &UMHGameInstance::OnFindSessionsComplete);

#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("FindSessions: Starting LAN search..."));
#endif

	if (!SessionInterface->FindSessions(0, SessionSearch.ToSharedRef()))
	{
		UE_LOG(LogTemp, Error, TEXT("FindSessions: FindSessions returned FALSE (immediate failure)!"));
	}
}

void UMHGameInstance::OnFindSessionsComplete(bool bWasSuccessful)
{
#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("=== OnFindSessionsComplete CALLED ==="));
	UE_LOG(LogTemp, Log, TEXT("bWasSuccessful=%d"), bWasSuccessful);
#endif

	TArray<FSessionData> SessionResults;

	// 结果 A: OnlineSubsystem 发现的集会（LAN 发现）
	if (bWasSuccessful && SessionSearch.IsValid())
	{
#if MH_DEBUG
		int32 NumResults = SessionSearch->SearchResults.Num();
		UE_LOG(LogTemp, Log, TEXT("OnFindSessionsComplete: OnlineSubsystem found %d results"), NumResults);
#endif

		for (int32 i = 0; i < SessionSearch->SearchResults.Num(); ++i)
		{
			const FOnlineSessionSearchResult& Result = SessionSearch->SearchResults[i];

#if MH_DEBUG
			UE_LOG(LogTemp, Log, TEXT("  Result[%d]: IsValid=%d, IsSessionInfoValid=%d, Ping=%d"),
				i, Result.IsValid() ? 1 : 0, Result.IsSessionInfoValid() ? 1 : 0, Result.PingInMs);
			UE_LOG(LogTemp, Log, TEXT("    NumOpenPublicConn=%d, NumPublicConn=%d"),
				Result.Session.NumOpenPublicConnections, Result.Session.SessionSettings.NumPublicConnections);
#endif

			if (!Result.IsValid() || !Result.IsSessionInfoValid()) continue;

			FSessionData Data;
			Data.SessionIndex = i;
			Data.CurrentPlayers = Result.Session.SessionSettings.NumPublicConnections - Result.Session.NumOpenPublicConnections;
			Data.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
			Data.Ping = Result.PingInMs;

			FString ServerName;
			if (Result.Session.SessionSettings.Get(FName("SERVER_NAME"), ServerName))
			{
				Data.ServerName = ServerName;
			}
			else
			{
				Data.ServerName = FString::Printf(TEXT("集会 #%d"), i + 1);
			}

#if MH_DEBUG
			UE_LOG(LogTemp, Log, TEXT("    => ServerName=%s, Players=%d/%d"),
				*Data.ServerName, Data.CurrentPlayers, Data.MaxPlayers);
#endif

			SessionResults.Add(Data);
		}
	}

	OnSessionsFound.Broadcast(SessionResults);
}

// ============================================================================
// 加入集会
// ============================================================================
void UMHGameInstance::JoinSelectedSession(int32 SessionIndex)
{
	if (!SessionInterface.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("JoinSession: Invalid session interface!"));
		return;
	}

#if MH_DEBUG
	int32 NumSearchResults = SessionSearch.IsValid() ? SessionSearch->SearchResults.Num() : 0;
	UE_LOG(LogTemp, Log, TEXT("=== JoinSelectedSession ==="));
	UE_LOG(LogTemp, Log, TEXT("SessionIndex=%d, NumSearchResults=%d"), SessionIndex, NumSearchResults);
#endif

	// 优先尝试 OnlineSubsystem 的标准加入流程
	if (SessionSearch.IsValid() && SessionSearch->SearchResults.IsValidIndex(SessionIndex))
	{
#if MH_DEBUG
		UE_LOG(LogTemp, Log, TEXT("JoinSession: Using OnlineSubsystem JoinSession for index %d"), SessionIndex);
#endif
		// 从搜索结果提取自定义连接地址（后备 GetResolvedConnectString）
		const FOnlineSessionSearchResult& SearchRes = SessionSearch->SearchResults[SessionIndex];
		FString ConnStr;
		if (SearchRes.Session.SessionSettings.Get(FName("CONNECT_STR"), ConnStr))
		{
			PendingConnectString = ConnStr;
#if MH_DEBUG
			UE_LOG(LogTemp, Log, TEXT("JoinSession: Got CONNECT_STR='%s'"), *PendingConnectString);
#endif
		}
		else
		{
			PendingConnectString.Empty();
		}
		// 加入前先销毁本地已有会话，避免 standalone 模式下 NAME_GameSession 冲突
		FNamedOnlineSession* LocalSession = SessionInterface->GetNamedSession(NAME_GameSession);
		if (LocalSession)
		{
			SessionInterface->DestroySession(NAME_GameSession);
		}

		SessionInterface->OnJoinSessionCompleteDelegates.Remove(OnJoinSessionCompleteHandle);
		OnJoinSessionCompleteHandle = SessionInterface->OnJoinSessionCompleteDelegates.AddUObject(this, &UMHGameInstance::OnJoinSessionComplete);

		if (!SessionInterface->JoinSession(0, NAME_GameSession, SessionSearch->SearchResults[SessionIndex]))
		{
			UE_LOG(LogTemp, Error, TEXT("JoinSession: OnlineSubsystem JoinSession failed!"));
		}
		return;
	}

	UE_LOG(LogTemp, Error, TEXT("JoinSession: Session index %d not found in SearchResults (have %d results)!"), SessionIndex, SessionSearch.IsValid() ? SessionSearch->SearchResults.Num() : 0);
}

void UMHGameInstance::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	UE_LOG(LogTemp, Log, TEXT("OnJoinSessionComplete: Session=%s, Result=%d"), *SessionName.ToString(), (int32)Result);

	if (Result == EOnJoinSessionCompleteResult::Success)
	{
		FString TravelURL;
		if (SessionInterface->GetResolvedConnectString(SessionName, TravelURL))
		{
			// NULL 子系统端口解析可能失败（返回 :0），改用自定义连接串
			if (TravelURL.EndsWith(":0") || TravelURL.EndsWith(":") || TravelURL.IsEmpty())
			{
				if (!PendingConnectString.IsEmpty())
				{
#if MH_DEBUG
					UE_LOG(LogTemp, Log, TEXT("OnJoinSessionComplete: GetResolvedConnectString returned '%s' — using CONNECT_STR='%s'"),
						*TravelURL, *PendingConnectString);
#endif
					TravelURL = PendingConnectString;
				}
			}

			APlayerController* PC = GetFirstLocalPlayerController();
			if (PC)
			{
				PC->ClientTravel(TravelURL, TRAVEL_Absolute);
				OnSessionJoined.Broadcast();
			}
		}
		PendingConnectString.Empty();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("OnJoinSessionComplete: Failed with result %d"), (int32)Result);
	}
}

// ============================================================================
// 销毁集会
// ============================================================================
void UMHGameInstance::DestroySession()
{
	if (!SessionInterface.IsValid()) return;

	SessionInterface->OnDestroySessionCompleteDelegates.Remove(OnDestroySessionCompleteHandle);
	OnDestroySessionCompleteHandle = SessionInterface->OnDestroySessionCompleteDelegates.AddUObject(this, &UMHGameInstance::OnDestroySessionComplete);

	if (!SessionInterface->DestroySession(NAME_GameSession))
	{
		OnSessionDestroyed.Broadcast(true);
	}
}

void UMHGameInstance::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	UE_LOG(LogTemp, Log, TEXT("OnDestroySessionComplete: Session=%s, Success=%d"), *SessionName.ToString(), bWasSuccessful);
	OnSessionDestroyed.Broadcast(bWasSuccessful);
}




// ============================================================================
// 存档/读档（预留接口）
// ============================================================================
void UMHGameInstance::SavePlayerData()
{
#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("SavePlayerData: Not implemented yet."));
#endif
}

void UMHGameInstance::LoadPlayerData()
{
#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("LoadPlayerData: Not implemented yet."));
#endif
}


// ============================================================================
// 辅助
// ============================================================================
int32 UMHGameInstance::GetSessionResultCount() const
{
	return SessionSearch.IsValid() ? SessionSearch->SearchResults.Num() : 0;
}

FSessionData UMHGameInstance::GetSessionResult(int32 Index) const
{
	FSessionData Result;
	if (SessionSearch.IsValid() && SessionSearch->SearchResults.IsValidIndex(Index))
	{
		const FOnlineSessionSearchResult& SearchResult = SessionSearch->SearchResults[Index];
		Result.SessionIndex = Index;
		Result.CurrentPlayers = SearchResult.Session.SessionSettings.NumPublicConnections - SearchResult.Session.NumOpenPublicConnections;
		Result.MaxPlayers = SearchResult.Session.SessionSettings.NumPublicConnections;
		Result.Ping = SearchResult.PingInMs;

		FString ServerName;
		if (SearchResult.Session.SessionSettings.Get(FName("SERVER_NAME"), ServerName))
		{
			Result.ServerName = ServerName;
		}
		else
		{
			Result.ServerName = FString::Printf(TEXT("集会 #%d"), Index + 1);
		}
	}
	return Result;
}








