#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UMHCombatNotify.generated.h"

/*
 * Event bridge from a montage timeline back into CombatComponent.
 * Keeps gameplay timing in the component and animation data in the montage.
 */
UCLASS(BlueprintType, meta = (DisplayName = "MH Combat Notify"))
class MH_API UMHCombatNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	EMHCombatNotifyType NotifyType = EMHCombatNotifyType::AttackHit;
};
