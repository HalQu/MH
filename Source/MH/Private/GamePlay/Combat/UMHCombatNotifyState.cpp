#include "GamePlay/Combat/UMHCombatNotifyState.h"
#include "GamePlay/Combat/UCombatComponent.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	void DispatchCombatNotifyState(USkeletalMeshComponent* MeshComp, EMHCombatNotifyStateType StateType, EMHCombatNotifyStateEvent StateEvent, UAnimSequenceBase* Animation)
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
			CombatComponent->HandleCombatNotifyState(StateType, StateEvent, Cast<UAnimMontage>(Animation));
		}
	}
}

void UMHCombatNotifyState::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	DispatchCombatNotifyState(MeshComp, StateType, EMHCombatNotifyStateEvent::Begin, Animation);
}

void UMHCombatNotifyState::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	DispatchCombatNotifyState(MeshComp, StateType, EMHCombatNotifyStateEvent::End, Animation);
}
