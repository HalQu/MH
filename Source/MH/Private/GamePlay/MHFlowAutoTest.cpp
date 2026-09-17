#include "GamePlay/MHFlowAutoTest.h"

#if !UE_BUILD_SHIPPING

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GamePlay/GameMode/MHGameMode_Lobby.h"
#include "GamePlay/MHGameInstance.h"
#include "GamePlay/MHGameState_Hunting.h"
#include "GamePlay/MHPlayerController.h"
#include "GamePlay/MHPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogMHFlowAutoTest, Log, All);

namespace
{
	const TCHAR* const HostLevelToken = TEXT("BeginMap");
	const TCHAR* const LobbyLevelToken = TEXT("LobbyMap");
	const TCHAR* const HuntingLevelToken = TEXT("HuntingMap");

	const TCHAR* const AutoTestRoomName = TEXT("自动测试房间");
}

TSharedPtr<FMHFlowAutoTest> FMHFlowAutoTest::CreateIfRequested(UMHGameInstance& InGameInstance)
{
	FString Mode;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MHAutoTest="), Mode) || Mode.IsEmpty())
	{
		return nullptr;
	}

	const bool bHost = Mode.Equals(TEXT("Host"), ESearchCase::IgnoreCase);
	const bool bClient = Mode.Equals(TEXT("Client"), ESearchCase::IgnoreCase);
	if (!bHost && !bClient)
	{
		UE_LOG(LogMHFlowAutoTest, Warning, TEXT("未知的 -MHAutoTest=%s（可用值：Host / Client）"), *Mode);
		return nullptr;
	}

	const bool bQuitWhenDone = FParse::Param(FCommandLine::Get(), TEXT("MHAutoTestQuit"));

	// 无渲染自动测试进程通常没有前台焦点；保留引擎默认 Idle 会让 GameThread
	// 长时间停止 tick，最终把 5 秒流程拖成数分钟。自检进程必须持续运行。
	if (IConsoleVariable* IdleWhenNotForeground = IConsoleManager::Get().FindConsoleVariable(TEXT("t.IdleWhenNotForeground")))
	{
		IdleWhenNotForeground->Set(0, ECVF_SetByGameOverride);
	}

	const TCHAR* const AsyncCompilationCVars[] =
	{
		TEXT("Editor.AsyncSkinnedAssetCompilation"),
		TEXT("Editor.AsyncStaticMeshCompilation"),
		TEXT("Editor.AsyncAssetCompilation")
	};
	for (const TCHAR* CVarName : AsyncCompilationCVars)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName))
		{
			CVar->Set(0, ECVF_SetByGameOverride);
		}
	}



	TSharedPtr<FMHFlowAutoTest> Tester = MakeShareable(new FMHFlowAutoTest(InGameInstance, bHost, bQuitWhenDone));
	UE_LOG(LogMHFlowAutoTest, Log, TEXT("[MHAutoTest] 启动自检：角色=%s，跑完自动退出=%d"),
		bHost ? TEXT("Host") : TEXT("Client"), bQuitWhenDone ? 1 : 0);
	return Tester;
}

FMHFlowAutoTest::FMHFlowAutoTest(UMHGameInstance& InGameInstance, bool bInHost, bool bInQuitWhenDone)
	: FTSTickerObjectBase(0.5f)
	, GameInstance(&InGameInstance)
	, bHost(bInHost)
	, bQuitWhenDone(bInQuitWhenDone)
{
	StartWallTime = PhaseStartWallTime = LastTickWallTime = LastHeartbeatWallTime = FPlatformTime::Seconds();
}

bool FMHFlowAutoTest::Tick(float DeltaTime)
{
	(void)DeltaTime;
	// Core ticker 的 DeltaTime 在无渲染/后台运行时可能为 0；流程超时必须使用墙钟。
	const double Now = FPlatformTime::Seconds();
	const float WallDelta = FMath::Max(0.f, static_cast<float>(Now - LastTickWallTime));
	LastTickWallTime = Now;
	PhaseElapsed = static_cast<float>(Now - PhaseStartWallTime);
	TotalElapsed = static_cast<float>(Now - StartWallTime);

	if (Phase == EPhase::Finished)
	{
		return false;
	}

	if (TotalElapsed > 300.f)
	{
		Fail(TEXT("整体超时"));
		return false;
	}

	if (Now - LastHeartbeatWallTime >= 5.0)
	{
		LastHeartbeatWallTime = Now;
		Log(FString::Printf(TEXT("阶段心跳：%s，阶段 %.1f 秒，总计 %.1f 秒"),
			*GetPhaseName(), PhaseElapsed, TotalElapsed));
	}

	const float PhaseTimeout = GetPhaseTimeout();
	if (PhaseTimeout > 0.f && PhaseElapsed > PhaseTimeout)
	{
		Fail(FString::Printf(TEXT("阶段 %s 超时（%.0f 秒）"), *GetPhaseName(), PhaseTimeout));
		return false;
	}

	switch (Phase)
	{
	case EPhase::Launch:
	{
		if (!GetLocalController() || !IsLevel(HostLevelToken))
		{
			break;
		}

		UMHGameInstance* Instance = GameInstance.Get();
		if (!Instance)
		{
			Fail(TEXT("GameInstance 已失效"));
			break;
		}

		if (bHost)
		{
			Log(TEXT("主菜单就绪，创建局域网房间"));
			Instance->HostSession(AutoTestRoomName, true, 4);
			EnterPhase(EPhase::Hosting);
		}
		else
		{
			Log(TEXT("主菜单就绪，开始搜索局域网房间"));
			SearchAttempts = 1;
			Instance->FindSessions(true);
			EnterPhase(EPhase::Searching);
		}
		break;
	}

	case EPhase::Hosting:
	{
		AMHPlayerState* PlayerState = GetLocalPlayerState();
		if (IsLevel(LobbyLevelToken) && PlayerState)
		{
			Log(FString::Printf(TEXT("已进入集会所，房主标记=%s"),
				PlayerState->bIsHost ? TEXT("是") : TEXT("否")));
			HostRequestReady();
			EnterPhase(EPhase::WaitingForReady);
		}
		break;
	}

	case EPhase::WaitingForReady:
	{
		// 等待客户端加入并双方准备完毕，房主再开战。
		AMHGameMode_Lobby* LobbyMode = GetWorld() ? GetWorld()->GetAuthGameMode<AMHGameMode_Lobby>() : nullptr;
		if (!LobbyMode)
		{
			break;
		}

		if (!bReadySent)
		{
			HostRequestReady();
		}

		const int32 PlayerCount = LobbyMode->GetPlayerCount();
		if (PlayerCount >= 2 && LobbyMode->CanStartHunt())
		{
			Log(FString::Printf(TEXT("%d 名猎人全部准备，房主开始狩猎"), PlayerCount));
			if (AMHPlayerController* Controller = Cast<AMHPlayerController>(GetLocalController()))
			{
				Controller->Server_RequestStartHunt();
			}
			EnterPhase(EPhase::WaitingForHunt);
		}
		break;
	}

	case EPhase::Searching:
	{
		UMHGameInstance* Instance = GameInstance.Get();
		if (!Instance)
		{
			Fail(TEXT("GameInstance 已失效"));
			break;
		}

		const int32 ResultCount = Instance->GetSessionResultCount();
		if (ResultCount > 0)
		{
			Log(FString::Printf(TEXT("发现 %d 个房间，加入第一个"), ResultCount));
			Instance->JoinSelectedSession(0);
			EnterPhase(EPhase::Joining);
			break;
		}

		if (PhaseElapsed > 5.f)
		{
			if (SearchAttempts >= 4)
			{
				Fail(TEXT("4 次搜索都没有发现房间"));
				break;
			}
			++SearchAttempts;
			Log(FString::Printf(TEXT("第 %d 次搜索局域网房间"), SearchAttempts));
			Instance->FindSessions(true);
			EnterPhase(EPhase::Searching);
		}
		break;
	}

	case EPhase::Joining:
	{
		AMHPlayerState* PlayerState = GetLocalPlayerState();
		if (IsLevel(LobbyLevelToken) && PlayerState)
		{
			const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState<AGameStateBase>() : nullptr;
			const int32 PlayerCount = GameState ? GameState->PlayerArray.Num() : 0;
			Log(FString::Printf(TEXT("已加入集会所，房主标记=%s，当前 %d 名猎人"),
				PlayerState->bIsHost ? TEXT("是") : TEXT("否"), PlayerCount));
			HostRequestReady();
			EnterPhase(EPhase::WaitingForHunt);
		}
		break;
	}

	case EPhase::WaitingForHunt:
	{
		AMHGameState_Hunting* HuntingState = GetHuntingState();
		if (IsLevel(HuntingLevelToken) && HuntingState && HuntingState->IsMatchActive())
		{
			Log(FString::Printf(TEXT("狩猎开始，复制到本机的剩余时间 %.1f 秒"), HuntingState->GetRemainingTime()));
			HuntActiveElapsed = 0.f;
			EnterPhase(EPhase::WaitingForResult);
		}
		break;
	}

	case EPhase::WaitingForResult:
	{
		AMHGameState_Hunting* HuntingState = GetHuntingState();
		if (!HuntingState)
		{
			break;
		}

		if (HuntingState->IsMatchActive())
		{
			HuntActiveElapsed += WallDelta;

			// 房主在开战 5 秒后提前结束，验证结算与回程；客户端只观察。
			if (bHost && !bEndHuntSent && HuntActiveElapsed > 5.f)
			{
				bEndHuntSent = true;
				if (AMHPlayerController* Controller = Cast<AMHPlayerController>(GetLocalController()))
				{
					Log(TEXT("房主提前结束狩猎"));
					Controller->Server_RequestEndHunt();
				}
			}
			break;
		}

		if (!HuntingState->GetResultText().IsEmpty())
		{
			Log(FString::Printf(TEXT("收到结算：%s"), *HuntingState->GetResultText().ToString()));
			EnterPhase(EPhase::WaitingForLobbyReturn);
		}
		break;
	}

	case EPhase::WaitingForLobbyReturn:
	{
		if (IsLevel(LobbyLevelToken))
		{
			Log(TEXT("结算后已自动回到集会所"));
			EnterPhase(EPhase::WaitingForMainMenuReturn);
		}
		break;
	}

	case EPhase::WaitingForMainMenuReturn:
	{
		UMHGameInstance* Instance = GameInstance.Get();
		if (!Instance)
		{
			Fail(TEXT("GameInstance 已失效"));
			break;
		}

		if (!bLeaveSent)
		{
			bLeaveSent = true;
			if (AMHPlayerController* Controller = Cast<AMHPlayerController>(GetLocalController()))
			{
				Log(TEXT("请求离开房间并回到主菜单"));
				Controller->RequestLeaveToMainMenu();
			}
		}

		if (IsLevel(HostLevelToken))
		{
			Succeed();
		}
		break;
	}

	case EPhase::Finished:
	default:
		return false;
	}

	return Phase != EPhase::Finished;
}

void FMHFlowAutoTest::HostRequestReady()
{
	if (bReadySent)
	{
		return;
	}

	AMHPlayerState* PlayerState = GetLocalPlayerState();
	if (!PlayerState || PlayerState->bReady)
	{
		return;
	}

	if (AMHPlayerController* Controller = Cast<AMHPlayerController>(GetLocalController()))
	{
		bReadySent = true;
		Log(TEXT("本地玩家已准备"));
		Controller->Server_SetReady(true);
	}
}

void FMHFlowAutoTest::EnterPhase(EPhase NewPhase)
{
	Phase = NewPhase;
	PhaseStartWallTime = FPlatformTime::Seconds();
	PhaseElapsed = 0.f;
}

void FMHFlowAutoTest::Succeed()
{
	Phase = EPhase::Finished;
	UE_LOG(LogMHFlowAutoTest, Log, TEXT("[MHAutoTest] RESULT=%s_PASS，总耗时 %.1f 秒"),
		bHost ? TEXT("HOST") : TEXT("CLIENT"), TotalElapsed);

	if (bQuitWhenDone)
	{
		FPlatformMisc::RequestExit(false, TEXT("FMHFlowAutoTest"));
	}
}

void FMHFlowAutoTest::Fail(const FString& Reason)
{
	Phase = EPhase::Finished;
	UE_LOG(LogMHFlowAutoTest, Error, TEXT("[MHAutoTest] RESULT=%s_FAIL，阶段=%s，原因=%s"),
		bHost ? TEXT("HOST") : TEXT("CLIENT"), *GetPhaseName(), *Reason);

	if (bQuitWhenDone)
	{
		FPlatformMisc::RequestExit(false, TEXT("FMHFlowAutoTest"));
	}
}

void FMHFlowAutoTest::Log(const FString& Message) const
{
	UE_LOG(LogMHFlowAutoTest, Log, TEXT("[MHAutoTest:%s] %s"),
		bHost ? TEXT("Host") : TEXT("Client"), *Message);
}

FString FMHFlowAutoTest::GetPhaseName() const
{
	switch (Phase)
	{
	case EPhase::Launch: return TEXT("Launch");
	case EPhase::Hosting: return TEXT("Hosting");
	case EPhase::WaitingForReady: return TEXT("WaitingForReady");
	case EPhase::WaitingForHunt: return TEXT("WaitingForHunt");
	case EPhase::WaitingForResult: return TEXT("WaitingForResult");
	case EPhase::WaitingForLobbyReturn: return TEXT("WaitingForLobbyReturn");
	case EPhase::WaitingForMainMenuReturn: return TEXT("WaitingForMainMenuReturn");
	case EPhase::Searching: return TEXT("Searching");
	case EPhase::Joining: return TEXT("Joining");
	case EPhase::Finished: return TEXT("Finished");
	default: return TEXT("Unknown");
	}
}

float FMHFlowAutoTest::GetPhaseTimeout() const
{
	switch (Phase)
	{
	case EPhase::Launch: return 60.f;
	case EPhase::Hosting: return 60.f;
	case EPhase::Searching: return 40.f;
	case EPhase::Joining: return 60.f;
	case EPhase::WaitingForReady: return 120.f;
	case EPhase::WaitingForHunt: return 60.f;
	case EPhase::WaitingForResult: return 60.f;
	case EPhase::WaitingForLobbyReturn: return 30.f;
	case EPhase::WaitingForMainMenuReturn: return 30.f;
	default: return 30.f;
	}
}

FString FMHFlowAutoTest::GetLevelName() const
{
	const UWorld* World = GetWorld();
	return World ? UGameplayStatics::GetCurrentLevelName(World, true) : FString();
}

UWorld* FMHFlowAutoTest::GetWorld() const
{
	const UMHGameInstance* Instance = GameInstance.Get();
	return Instance ? Instance->GetWorld() : nullptr;
}

bool FMHFlowAutoTest::IsLevel(const TCHAR* LevelToken) const
{
	return GetLevelName().Contains(LevelToken);
}

APlayerController* FMHFlowAutoTest::GetLocalController() const
{
	const UMHGameInstance* Instance = GameInstance.Get();
	return Instance ? Instance->GetFirstLocalPlayerController() : nullptr;
}

AMHPlayerState* FMHFlowAutoTest::GetLocalPlayerState() const
{
	const APlayerController* Controller = GetLocalController();
	return Controller ? Controller->GetPlayerState<AMHPlayerState>() : nullptr;
}

AMHGameState_Hunting* FMHFlowAutoTest::GetHuntingState() const
{
	UWorld* World = GetWorld();
	return World ? World->GetGameState<AMHGameState_Hunting>() : nullptr;
}

#endif // !UE_BUILD_SHIPPING
