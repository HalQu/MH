// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MHPlayerState.generated.h"

/*
 * 
 */
UCLASS()
class MH_API AMHPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/** 集会所准备状态。服务器写入，客户端只读复制结果。 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "MH|Flow")
	bool bReady = false;

	/** 当前会话的房主标记。 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "MH|Flow")
	bool bIsHost = false;

	void SetReady(bool bNewReady);
	void SetIsHost(bool bNewIsHost);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//Test ViewModel 
	UPROPERTY(BlueprintReadOnly, Category = "MH|PlayerState")
	float CurrentHealth=50.0f;
	UPROPERTY(BlueprintReadOnly, Category = "MH|PlayerState")
	float MaxHealth=100.0f;
	AMHPlayerState();
};
