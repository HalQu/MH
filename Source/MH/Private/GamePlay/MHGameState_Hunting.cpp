#include "GamePlay/MHGameState_Hunting.h"

#include "Net/UnrealNetwork.h"

FText AMHGameState_Hunting::GetResultText() const
{
	return FText::FromString(ResultText);
}

void AMHGameState_Hunting::SetRemainingTime(float NewRemainingTime)
{
	if (HasAuthority())
	{
		RemainingTime = FMath::Max(0.f, NewRemainingTime);
	}
}

void AMHGameState_Hunting::BeginMatch(float Duration)
{
	if (!HasAuthority())
	{
		return;
	}

	ResultText.Empty();
	RemainingTime = FMath::Max(0.f, Duration);
	bMatchActive = true;
}

void AMHGameState_Hunting::EndMatch(const FString& NewResult)
{
	if (!HasAuthority())
	{
		return;
	}

	bMatchActive = false;
	RemainingTime = 0.f;
	ResultText = NewResult;
}

void AMHGameState_Hunting::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMHGameState_Hunting, bMatchActive);
	DOREPLIFETIME(AMHGameState_Hunting, RemainingTime);
	DOREPLIFETIME(AMHGameState_Hunting, ResultText);
}
