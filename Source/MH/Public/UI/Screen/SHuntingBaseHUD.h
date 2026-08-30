// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Core/BaseScreen.h"
#include "Components/ProgressBar.h"
#include "UI/Core/BaseViewModel.h"
#include "UI/VM/VMHuntingBaseHUD.h"
#include "SHuntingBaseHUD.generated.h"

/**
 * 
 */
UCLASS()
class MH_API USHuntingBaseHUD : public UBaseScreen
{
	GENERATED_BODY()
public:
    USHuntingBaseHUD() {}

    UPROPERTY(meta = (BindWidget))
    UProgressBar* HealthBar;

    virtual void OnOpen(UObject* Param) override
    {
        Super::OnOpen(Param);
		UVMHuntingBaseHUD* VM = Cast<UVMHuntingBaseHUD>(GetViewModel());
        BIND_VM_PROPERTY(VM, HealthPercent, &USHuntingBaseHUD::SetHealthBar);
    }

    void SetHealthBar(float Percent) { HealthBar->SetPercent(Percent); }
};
