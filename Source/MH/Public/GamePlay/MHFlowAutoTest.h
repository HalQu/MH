#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class APlayerController;
class AMHGameState_Hunting;
class AMHPlayerState;
class UMHGameInstance;
class UWorld;

/**
 * 开发期完整联机流程自检。
 *
 * 用两个进程分别启动同一个构建即可跑完整闭环：
 *   房主进程：-MHAutoTest=Host
 *   客户端进程：-MHAutoTest=Client
 * 追加 -MHAutoTestQuit 可在流程跑完后自动退出进程。
 *
 * 覆盖链路：主菜单 -> 创建/搜索加入房间 -> 集会所准备 -> 开始狩猎 ->
 * 倒计时与结算复制 -> 回到集会所 -> 回到主菜单。
 * Shipping 构建下整个类不编译，运行时零开销。
 */
#if !UE_BUILD_SHIPPING
class FMHFlowAutoTest : public FTSTickerObjectBase
{
public:
	/** 命令行中没有 -MHAutoTest=Host|Client 时返回空指针。 */
	static TSharedPtr<FMHFlowAutoTest> CreateIfRequested(UMHGameInstance& InGameInstance);

	virtual bool Tick(float DeltaTime) override;

	FMHFlowAutoTest(UMHGameInstance& InGameInstance, bool bInHost, bool bInQuitWhenDone);

private:

	enum class EPhase : uint8
	{
		Launch,
		Hosting,
		WaitingForReady,
		WaitingForHunt,
		WaitingForResult,
		WaitingForLobbyReturn,
		WaitingForMainMenuReturn,
		Searching,
		Joining,
		Finished
	};

	void EnterPhase(EPhase NewPhase);
	void Succeed();
	void Fail(const FString& Reason);
	void Log(const FString& Message) const;

	FString GetPhaseName() const;
	float GetPhaseTimeout() const;
	UWorld* GetWorld() const;
	FString GetLevelName() const;
	APlayerController* GetLocalController() const;
	AMHPlayerState* GetLocalPlayerState() const;
	AMHGameState_Hunting* GetHuntingState() const;
	bool IsLevel(const TCHAR* LevelToken) const;

	/** 房主阶段：所有本地玩家准备完毕后请求开始狩猎。 */
	void HostRequestReady();

	TWeakObjectPtr<UMHGameInstance> GameInstance;
	EPhase Phase = EPhase::Launch;
	bool bHost = false;
	bool bQuitWhenDone = false;
	float PhaseElapsed = 0.f;
	float TotalElapsed = 0.f;
	float HuntActiveElapsed = 0.f;
	double StartWallTime = 0.0;
	double PhaseStartWallTime = 0.0;
	double LastTickWallTime = 0.0;
	double LastHeartbeatWallTime = 0.0;
	int32 SearchAttempts = 0;
	bool bReadySent = false;
	bool bEndHuntSent = false;
	bool bLeaveSent = false;
};
#endif
