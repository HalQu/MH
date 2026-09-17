#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "MHGameState_Hunting.generated.h"

/**
 * 狩猎阶段的复制状态。GameMode 是唯一写方，所有客户端只读。
 */
UCLASS()
class MH_API AMHGameState_Hunting : public AGameState
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "MH|Flow")
	bool IsMatchActive() const { return bMatchActive; }

	UFUNCTION(BlueprintPure, Category = "MH|Flow")
	float GetRemainingTime() const { return RemainingTime; }

	UFUNCTION(BlueprintPure, Category = "MH|Flow")
	FText GetResultText() const;

	void SetRemainingTime(float NewRemainingTime);
	void BeginMatch(float Duration);
	void EndMatch(const FString& NewResult);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MH|Flow")
	bool bMatchActive = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MH|Flow")
	float RemainingTime = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MH|Flow")
	FString ResultText;
};
