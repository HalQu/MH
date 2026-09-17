// Fill out your copyright notice in the Description page of Project Settings.

#include "GamePlay/MHGameInstance.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GamePlay/MHPlayerController.h"
#include "GamePlay/Character/MHCharacter.h"
#if !UE_BUILD_SHIPPING
#include "GamePlay/MHFlowAutoTest.h"
#endif
#include "Misc/CoreDelegates.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Engine/NetDriver.h"

namespace
{
	/** 是否为回环或无效地址。 */
	bool IsUnusableHostAddress(const TSharedPtr<FInternetAddr>& Address)
	{
		if (!Address.IsValid() || !Address->IsValid())
		{
			return true;
		}

		uint32 Ip = 0;
		Address->GetIp(Ip);
		return (Ip & 0xff000000) == 0x7f000000;
	}

	/**
	 * 取本机对外可用的局域网 IPv4，用于写入 CONNECT_STR。
	 * 单机调试（只有回环地址）时退回 127.0.0.1，保证本机多开仍能联调。
	 */
	FString ResolveLANAddress()
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!SocketSubsystem)
		{
			return TEXT("127.0.0.1");
		}

		bool bCanBindAll = false;
		TSharedPtr<FInternetAddr> HostAddress = SocketSubsystem->GetLocalHostAddr(*GLog, bCanBindAll);
		if (HostAddress.IsValid() && !IsUnusableHostAddress(HostAddress))
		{
			return HostAddress->ToString(false);
		}

		// 主机名解析到回环时，枚举网卡换一个非回环地址。
		TArray<TSharedPtr<FInternetAddr>> LocalAddresses;
		if (SocketSubsystem->GetLocalAdapterAddresses(LocalAddresses))
		{
			for (const TSharedPtr<FInternetAddr>& Address : LocalAddresses)
			{
				if (Address.IsValid() && !IsUnusableHostAddress(Address))
				{
					return Address->ToString(false);
				}
			}
		}

		return TEXT("127.0.0.1");
	}

	/** 实际监听端口；PIE 下引擎会给每个实例叠加 10000 的端口偏移。 */
	int32 ResolveListenPort(const UWorld* World)
	{
		if (!World)
		{
			return 0;
		}

		int32 Port = World->URL.Port;
#if WITH_EDITOR
		if (Port > 0 && World->WorldType == EWorldType::PIE)
		{
			Port += 10000;
		}
#endif
		return Port;
	}
}

void UMHGameInstance::Init()
{
	Super::Init();

#if !UE_BUILD_SHIPPING
	FlowAutoTest = FMHFlowAutoTest::CreateIfRequested(*this);
#endif
}

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
				SessionSettings.bIsLANMatch = PendingIsLAN;
				SessionSettings.bShouldAdvertise = true;
				SessionSettings.bUsesPresence = PendingIsLAN;
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
	PendingIsLAN = bIsLAN;
	PendingMaxPlayers = FMath::Clamp(MaxPlayers, 1, 16);

	const FString TravelURL = TEXT("/Game/Levels/LobbyMap?listen?game=/Script/MH.MHGameMode_Lobby");

	GetWorld()->ServerTravel(TravelURL);
}

void UMHGameInstance::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("=== OnCreateSessionComplete CALLED ==="));
	UE_LOG(LogTemp, Log, TEXT("SessionName=%s, bWasSuccessful=%d"), *SessionName.ToString(), bWasSuccessful);
#endif

	if (bWasSuccessful)
	{
		// 房主把本机可达地址写进会话设置：客户端优先用它，避免 LAN 广播地址
		// 在单机多开或多网卡环境下被解析成回环地址、端口 0。
		const int32 ListenPort = ResolveListenPort(GetWorld());
		if (ListenPort > 0)
		{
			const FString ConnectStr = FString::Printf(TEXT("%s:%d"), *ResolveLANAddress(), ListenPort);
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
		OnSessionOperationFailed.Broadcast(TEXT("创建房间失败，请返回主菜单后重试。"));
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
		OnSessionOperationFailed.Broadcast(TEXT("在线子系统不可用。"));
		return;
	}

#if MH_DEBUG
	UE_LOG(LogTemp, Log, TEXT("FindSessions: OnlineSubsystem=%s (ptr=%p)"), *Subsystem->GetSubsystemName().ToString(), Subsystem);
#endif

	SessionInterface = Subsystem->GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("FindSessions: No SessionInterface!"));
		OnSessionOperationFailed.Broadcast(TEXT("会话接口不可用。"));
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
		OnSessionOperationFailed.Broadcast(TEXT("搜索房间失败。"));
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
	if (!bWasSuccessful)
	{
		OnSessionOperationFailed.Broadcast(TEXT("房间搜索未完成。"));
	}
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
			PendingJoinSessionIndex = SessionIndex;
			DestroySession();
			return;
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

void UMHGameInstance::ContinueJoinSelectedSession(int32 SessionIndex)
{
	JoinSelectedSession(SessionIndex);
}

void UMHGameInstance::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	UE_LOG(LogTemp, Log, TEXT("OnJoinSessionComplete: Session=%s, Result=%d"), *SessionName.ToString(), (int32)Result);

	if (Result == EOnJoinSessionCompleteResult::Success)
	{
		FString ResolvedURL;
		const bool bResolved = SessionInterface->GetResolvedConnectString(SessionName, ResolvedURL);

		// 房主写入的 CONNECT_STR 最可靠：LAN 广播解析出的地址在单机多开、多网卡
		// 或 PIE 端口偏移场景下可能是回环地址或端口 0。
		FString TravelURL = !PendingConnectString.IsEmpty() ? PendingConnectString : ResolvedURL;

		if (TravelURL.IsEmpty() || TravelURL.EndsWith(TEXT(":0")) || TravelURL.EndsWith(TEXT(":")))
		{
			UE_LOG(LogTemp, Error,
				TEXT("OnJoinSessionComplete: 无可用的连接地址 (Resolved='%s' bResolved=%d, CONNECT_STR='%s')"),
				*ResolvedURL, bResolved ? 1 : 0, *PendingConnectString);
			PendingConnectString.Empty();
			OnSessionOperationFailed.Broadcast(TEXT("无法解析房间地址，请重新搜索后再试。"));
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("OnJoinSessionComplete: ClientTravel -> %s"), *TravelURL);

		if (APlayerController* PC = GetFirstLocalPlayerController())
		{
			PC->ClientTravel(TravelURL, TRAVEL_Absolute);
			OnSessionJoined.Broadcast();
		}
		else
		{
			OnSessionOperationFailed.Broadcast(TEXT("本机没有可用的玩家控制器，无法加入房间。"));
		}
		PendingConnectString.Empty();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("OnJoinSessionComplete: Failed with result %d"), (int32)Result);
		OnSessionOperationFailed.Broadcast(TEXT("加入房间失败，房间可能已经关闭。"));
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

	if (PendingJoinSessionIndex != INDEX_NONE)
	{
		const int32 JoinIndex = PendingJoinSessionIndex;
		PendingJoinSessionIndex = INDEX_NONE;
		ContinueJoinSelectedSession(JoinIndex);
		return;
	}

	if (bReturnToMainMenuAfterDestroy)
	{
		bReturnToMainMenuAfterDestroy = false;
		TravelToMainMenu();
	}
}

void UMHGameInstance::ReturnToMainMenu()
{
	bReturnToMainMenuAfterDestroy = true;
	PendingJoinSessionIndex = INDEX_NONE;

	if (HasActiveSession())
	{
		DestroySession();
		return;
	}

	bReturnToMainMenuAfterDestroy = false;
	TravelToMainMenu();
}

bool UMHGameInstance::HasActiveSession() const
{
	return SessionInterface.IsValid() && SessionInterface->GetNamedSession(NAME_GameSession) != nullptr;
}

void UMHGameInstance::TravelToMainMenu()
{
	if (APlayerController* PlayerController = GetFirstLocalPlayerController())
{
		const FString TravelURL = TEXT("/Game/Levels/BeginMap?game=/Script/MH.BeginGameMode");

		PlayerController->ClientTravel(TravelURL, TRAVEL_Absolute);
}
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








