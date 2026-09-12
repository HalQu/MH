#include "GamePlay/GameMode/MHGameMode_Hunting.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GamePlay/MHPlayerController.h"
#include "UI/Core/BaseScreen.h"
#include "UI/Core/UIScreenTypes.h"
#include "UI/Core/UIManager.h"

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

    for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
    {
        OpenHuntingHUDForPlayer(Iterator->Get(), HuntingHUDClass);
    }
}

void AMHGameMode_Hunting::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    OpenHuntingHUDForPlayer(NewPlayer, HuntingHUDClass);
}
