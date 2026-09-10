#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UMHCombatNotify.generated.h"

/*
 * Single-frame event bridge from a montage timeline back into CombatComponent.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "MH Combat Notify"))
class MH_API UMHCombatNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	EMHCombatNotifyType NotifyType = EMHCombatNotifyType::AttackStart;
};
