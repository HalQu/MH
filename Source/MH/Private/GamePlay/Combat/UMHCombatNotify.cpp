#include "GamePlay/Combat/UMHCombatNotify.h"
#include "GamePlay/Combat/UCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"

void UMHCombatNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

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
		CombatComponent->HandleCombatNotify(NotifyType);
	}
}
