#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "IMHCombatTargetInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UMHCombatTargetInterface : public UInterface
{
	GENERATED_BODY()
};

class MH_API IMHCombatTargetInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	FMHDamageResult ReceiveDamage(const FMHDamageEvent& DamageEvent);
};
