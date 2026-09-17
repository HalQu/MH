// Fill out your copyright notice in the Description page of Project Settings.

#include "GamePlay/MHPlayerState.h"

#include "Net/UnrealNetwork.h"

AMHPlayerState::AMHPlayerState()
{
}

void AMHPlayerState::SetReady(bool bNewReady)
{
	if (HasAuthority() && bReady != bNewReady)
	{
		bReady = bNewReady;
	}
}

void AMHPlayerState::SetIsHost(bool bNewIsHost)
{
	if (HasAuthority() && bIsHost != bNewIsHost)
	{
		bIsHost = bNewIsHost;
	}
}

void AMHPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMHPlayerState, bReady);
	DOREPLIFETIME(AMHPlayerState, bIsHost);
}
