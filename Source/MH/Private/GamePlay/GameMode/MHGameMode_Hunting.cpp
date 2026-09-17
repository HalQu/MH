#include "GamePlay/GameMode/MHGameMode_Hunting.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GamePlay/MHPlayerController.h"
#include "GamePlay/Character/MHCharacter.h"
#include "GamePlay/Combat/UHealthComponent.h"
#include "GamePlay/MHGameState_Hunting.h"
#include "GamePlay/MHPlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/DefaultPawn.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/PlayerState.h"
#include "UI/Core/BaseScreen.h"
#include "UI/Core/UIScreenTypes.h"
#include "UI/Core/UIManager.h"
#include "UI/Screen/HuntingStatusHUD.h"

AMHGameMode_Hunting::AMHGameMode_Hunting()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f;
	DefaultPawnClass = AMHCharacter::StaticClass();
	PlayerControllerClass = AMHPlayerController::StaticClass();
	PlayerStateClass = AMHPlayerState::StaticClass();
	GameStateClass = AMHGameState_Hunting::StaticClass();
	HuntingHUDClass = UMHHuntingStatusHUD::StaticClass();
	bUseSeamlessTravel = true;
}

void AMHGameMode_Hunting::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	// GameState/Controller 必须在 InitGame 前确定，避免地图或蓝图 CDO 覆盖流程配置。
	DefaultPawnClass = AMHCharacter::StaticClass();
	PlayerControllerClass = AMHPlayerController::StaticClass();
	PlayerStateClass = AMHPlayerState::StaticClass();
	GameStateClass = AMHGameState_Hunting::StaticClass();
	HuntingHUDClass = UMHHuntingStatusHUD::StaticClass();
	bUseSeamlessTravel = true;

	Super::InitGame(MapName, Options, ErrorMessage);

	FString AutoTestMode;
	if (FParse::Value(FCommandLine::Get(), TEXT("MHAutoTest="), AutoTestMode) && !AutoTestMode.IsEmpty())
	{
		DefaultPawnClass = ADefaultPawn::StaticClass();
	}
}

namespace
{
    void OpenHuntingHUDForPlayer(APlayerController* PlayerController, TSubclassOf<UBaseScreen> HUDClass)
    {
        if (!HUDClass)
        {
            UE_LOG(LogTemp, Error, TEXT("[MHGameMode_Hunting] HuntingHUDClass is not configured"));
            return;
        }

        if (!IsValid(PlayerController))
        {
            return;
        }

        if (PlayerController->IsLocalController())
        {
            if (!UUIManager::OpenPersistentScreenForLocalPlayer(
                PlayerController,
                FName("HUD"),
                HUDClass,
                EUIScreenInputMode::GameOnly))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[MHGameMode_Hunting] Failed to open HUD for local controller %s"),
                    *PlayerController->GetName());
            }
            return;
        }

        if (AMHPlayerController* MHPlayerController = Cast<AMHPlayerController>(PlayerController))
        {
            MHPlayerController->Client_OpenPersistentScreen(
                FName("HUD"),
                HUDClass,
                EUIScreenInputMode::GameOnly);
            return;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[MHGameMode_Hunting] Remote PlayerController %s is not AMHPlayerController; cannot open HUD"),
            *PlayerController->GetName());
    }
}

void AMHGameMode_Hunting::BeginPlay()
{
    Super::BeginPlay();

    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
    PrimaryActorTick.TickInterval = 0.2f;
    SetActorTickEnabled(true);

    TimeUntilLobbyTravel = -1.f;
    if (AMHGameState_Hunting* HuntingState = GetGameState<AMHGameState_Hunting>())
    {
        HuntingState->BeginMatch(HuntDuration);
    }

    for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
    {
        OpenHuntingHUDForPlayer(Iterator->Get(), HuntingHUDClass);
        if (APlayerController* PlayerController = Iterator->Get())
        {
            if (AMHPlayerState* PlayerState = PlayerController->GetPlayerState<AMHPlayerState>())
            {
                PlayerState->SetIsHost(PlayerController->IsLocalController());
            }
        }
    }
}

void AMHGameMode_Hunting::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    if (NewPlayer)
    {
        if (AMHPlayerState* PlayerState = NewPlayer->GetPlayerState<AMHPlayerState>())
        {
            if (NewPlayer->IsLocalController())
            {
                PlayerState->SetIsHost(true);
            }
            PlayerState->SetReady(false);
        }
    }

    OpenHuntingHUDForPlayer(NewPlayer, HuntingHUDClass);
}

void AMHGameMode_Hunting::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!HasAuthority())
{
        return;
}

    if (TimeUntilLobbyTravel >= 0.f)
{
        TimeUntilLobbyTravel -= DeltaSeconds;
        if (TimeUntilLobbyTravel <= 0.f)
        {
            TimeUntilLobbyTravel = -1.f;
			UE_LOG(LogTemp, Log, TEXT("Hunting: returning to lobby"));
			GetWorld()->ServerTravel(TEXT("/Game/Levels/LobbyMap?listen?game=/Script/MH.MHGameMode_Lobby"));
        }
        return;
    }

    AMHGameState_Hunting* HuntingState = GetGameState<AMHGameState_Hunting>();
    if (!HuntingState || !HuntingState->IsMatchActive())
{
        return;
    }

    const float RemainingTime = FMath::Max(0.f, HuntingState->GetRemainingTime() - DeltaSeconds);
    HuntingState->SetRemainingTime(RemainingTime);

    if (RemainingTime <= 0.f)
{
        FinishHunt(NSLOCTEXT("MHFlow", "HuntTimeUp", "狩猎时间结束"));
    }
    else if (AreAllPlayersDead())
{
        FinishHunt(NSLOCTEXT("MHFlow", "HuntFailed", "全员倒下，狩猎结束"));
    }
}

void AMHGameMode_Hunting::RequestEndHunt(APlayerController* RequestingController, const FText& ResultText)
{
	if (!HasAuthority() || !RequestingController)
{
		return;
}

	// 监听服务器上只有房主的 PlayerController 是本地控制器；不要把服务器权威
	// 当成房主判断，否则远程客户端的服务器侧控制器也会通过。
	if (!RequestingController->IsLocalController())
	{
		return;
	}

	FinishHunt(ResultText);
}

void AMHGameMode_Hunting::FinishHunt(const FText& ResultText)
{
	AMHGameState_Hunting* HuntingState = GetGameState<AMHGameState_Hunting>();
	if (!HuntingState || !HuntingState->IsMatchActive())
{
		return;
}

	HuntingState->EndMatch(ResultText.ToString());
	UE_LOG(LogTemp, Log, TEXT("Hunting: match ended: %s"), *ResultText.ToString());
	TimeUntilLobbyTravel = FMath::Max(0.f, ResultDisplayDuration);
}

bool AMHGameMode_Hunting::AreAllPlayersDead() const
{
	const AGameStateBase* GameStateBase = GetGameState<AGameStateBase>();
	if (!GameStateBase || GameStateBase->PlayerArray.Num() == 0)
{
		return false;
}

	for (APlayerState* PlayerState : GameStateBase->PlayerArray)
{
		APlayerController* PlayerController = PlayerState ? PlayerState->GetPlayerController() : nullptr;
		const AMHCharacter* Character = PlayerController ? Cast<AMHCharacter>(PlayerController->GetPawn()) : nullptr;
		const UHealthComponent* Health = Character ? Character->GetHealthComponent() : nullptr;
		if (!Health || !Health->IsDead())
{
			return false;
}
	}

	return true;
}
