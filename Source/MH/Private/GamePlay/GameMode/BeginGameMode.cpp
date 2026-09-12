#include "GamePlay/GameMode/BeginGameMode.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GamePlay/MHPlayerController.h"
#include "UI/Core/BaseScreen.h"
#include "UI/Core/UIScreenTypes.h"
#include "UI/Core/UIManager.h"

namespace
{
    void OpenMainMenuForPlayer(APlayerController* PlayerController, TSubclassOf<UBaseScreen> MainMenuClass)
    {
        if (!MainMenuClass)
        {
            UE_LOG(LogTemp, Error, TEXT("[BeginGameMode] MainMenuScreenClass is not configured"));
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
                FName("MainMenu"),
                MainMenuClass,
                EUIScreenInputMode::UIOnly))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[BeginGameMode] Failed to open MainMenu for local controller %s"),
                    *PlayerController->GetName());
            }
            return;
        }

        if (AMHPlayerController* MHPlayerController = Cast<AMHPlayerController>(PlayerController))
        {
            MHPlayerController->Client_OpenPersistentScreen(
                FName("MainMenu"),
                MainMenuClass,
                EUIScreenInputMode::UIOnly);
            return;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[BeginGameMode] Remote PlayerController %s is not AMHPlayerController; cannot open MainMenu"),
            *PlayerController->GetName());
    }
}

ABeginGameMode::ABeginGameMode()
{
    PlayerControllerClass = AMHPlayerController::StaticClass();
}

void ABeginGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);

    // 蓝图若没有单独配置控制器类型，统一补成项目控制器，保证客户端 UI RPC 可用。
    if (!PlayerControllerClass || PlayerControllerClass == APlayerController::StaticClass())
    {
        PlayerControllerClass = AMHPlayerController::StaticClass();
    }
}

void ABeginGameMode::BeginPlay()
{
    Super::BeginPlay();

    for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
    {
        OpenMainMenuForPlayer(Iterator->Get(), MainMenuScreenClass);
    }
}

void ABeginGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    OpenMainMenuForPlayer(NewPlayer, MainMenuScreenClass);
}
