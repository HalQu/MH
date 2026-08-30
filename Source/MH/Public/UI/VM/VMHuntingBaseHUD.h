// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Core/BaseViewModel.h"
#include "UI/Core/Observable.h"
#include "GamePlay/MHPlayerState.h"
#include "VMHuntingBaseHUD.generated.h"

/**
 * 
 */
UCLASS()
class MH_API UVMHuntingBaseHUD : public UBaseViewModel
{
	GENERATED_BODY()
public:
    TBindedValue<float> HealthPercent;   

    virtual void RefreshAll() override   
    {
        if (AMHPlayerState* PS = GetDataSource<AMHPlayerState>())
        {
            //HealthPercent.Set(PS->MaxHealth > 0 ? PS->CurrentHealth / PS->MaxHealth : 0.f);
            HealthPercent.Set(0.5f);
        }
    }
};
