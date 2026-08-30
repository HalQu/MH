// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * 
 */
namespace Utils
{
	float GetSignedAngleBetweenVectors(const FVector& A, const FVector& B, const FVector& UpAxis);
	bool IsForwordBetweenVectors(const FVector& VecA, const FVector& VecB, const FVector& Axis);
	bool IsGamepadConnected();
}


