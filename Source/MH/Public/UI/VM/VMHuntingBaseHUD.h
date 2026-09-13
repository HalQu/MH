// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UI/Core/BaseViewModel.h"
#include "UI/Core/Observable.h"
#include "VMHuntingBaseHUD.generated.h"

class UHealthComponent;
class UCombatComponent;
class UHitReactionComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHHealthPercentChanged, float, NewPercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHCombatDebugTextChanged, const FString&, NewText);

/**
 * 
 */
UCLASS()
class MH_API UVMHuntingBaseHUD : public UBaseViewModel
{
	GENERATED_BODY()
public:
    TBindedValue<float> HealthPercent;

    TBindedValue<FString> CombatDebugText;

    UPROPERTY(BlueprintAssignable, Category = "UI|Health")
    FMHHealthPercentChanged OnHealthPercentChanged;

    UPROPERTY(BlueprintAssignable, Category = "UI|Combat")
    FMHCombatDebugTextChanged OnCombatDebugTextChanged;

    UFUNCTION(BlueprintPure, Category = "UI|Health")
    float GetHealthPercentValue() const { return HealthPercent.Get(); }

    UFUNCTION(BlueprintPure, Category = "UI|Combat")
    FString GetCombatDebugTextValue() const { return CombatDebugText.Get(); }

    UFUNCTION(BlueprintCallable, Category = "UI|Combat")
    void RefreshCombatDebug();

    virtual void OnActivated() override;
    virtual void OnDeactivated() override;
    virtual void RefreshAll() override;

private:
    UHealthComponent* ResolveHealthComponent() const;
    void BindHealthComponent(UHealthComponent* HealthComponent);
    void UnbindHealthComponent();
    void SetHealthPercent(float NewPercent);

    UCombatComponent* ResolveCombatComponent() const;
    UHitReactionComponent* ResolveHitReactionComponent() const;
    void BindCombatComponent(UCombatComponent* CombatComponent);
    void UnbindCombatComponent();
    void SetCombatDebugText(const FString& NewText);
    FString BuildCombatDebugText() const;

    UFUNCTION()
    void HandleHitConfirmed(const FMHCombatHitEvent& HitEvent);

    UFUNCTION()
    void HandleHealthChanged(float NewHealth, float MaxHealth);

    TWeakObjectPtr<UHealthComponent> BoundHealthComponent;
    TWeakObjectPtr<UCombatComponent> BoundCombatComponent;
    FMHCombatHitEvent LastConfirmedHit;
    float LastConfirmedHitWorldTime = -1000.f;
};
