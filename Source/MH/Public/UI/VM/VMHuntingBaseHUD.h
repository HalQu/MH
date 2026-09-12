// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Core/BaseViewModel.h"
#include "UI/Core/Observable.h"
#include "VMHuntingBaseHUD.generated.h"

class UHealthComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHHealthPercentChanged, float, NewPercent);

/**
 * 
 */
UCLASS()
class MH_API UVMHuntingBaseHUD : public UBaseViewModel
{
	GENERATED_BODY()
public:
    TBindedValue<float> HealthPercent;

    UPROPERTY(BlueprintAssignable, Category = "UI|Health")
    FMHHealthPercentChanged OnHealthPercentChanged;

    UFUNCTION(BlueprintPure, Category = "UI|Health")
    float GetHealthPercentValue() const { return HealthPercent.Get(); }

    virtual void OnActivated() override;
    virtual void OnDeactivated() override;
    virtual void RefreshAll() override;

private:
    UHealthComponent* ResolveHealthComponent() const;
    void BindHealthComponent(UHealthComponent* HealthComponent);
    void UnbindHealthComponent();
    void SetHealthPercent(float NewPercent);

    UFUNCTION()
    void HandleHealthChanged(float NewHealth, float MaxHealth);

    TWeakObjectPtr<UHealthComponent> BoundHealthComponent;
};
