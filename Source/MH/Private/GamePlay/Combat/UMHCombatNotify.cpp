#include "GamePlay/Combat/UMHCombatNotify.h"
#include "GamePlay/Combat/UCombatComponent.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	void DispatchCombatNotify(USkeletalMeshComponent* MeshComp, EMHCombatNotifyType NotifyType, UAnimSequenceBase* Animation)
	{
		if (!MeshComp)
		{
			return;
		}

		AActor* Owner = MeshComp->GetOwner();
		if (!Owner)
		{
			return;
		}

		if (UCombatComponent* CombatComponent = Owner->FindComponentByClass<UCombatComponent>())
		{
			CombatComponent->HandleCombatNotify(NotifyType, Cast<UAnimMontage>(Animation));
		}
	}
}

void UMHCombatNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	DispatchCombatNotify(MeshComp, NotifyType, Animation);
}
