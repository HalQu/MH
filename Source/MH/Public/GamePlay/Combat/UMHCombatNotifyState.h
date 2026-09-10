#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UMHCombatNotifyState.generated.h"

/*
 * Interval bridge from a montage timeline back into CombatComponent.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "MH Combat Notify State"))
class MH_API UMHCombatNotifyState : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	EMHCombatNotifyStateType StateType = EMHCombatNotifyStateType::ComboWindow;
};
