// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UI/Core/UIScreenTypes.h"
#include "MHPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class UBaseScreen;


UCLASS()
class MH_API AMHPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AMHPlayerController();

	/** 只在本机创建常驻界面。蓝图和本地 UI 代码应调用这个入口。 */
	UFUNCTION(BlueprintCallable, Category = "UI")
	UBaseScreen* OpenPersistentScreenLocal(
		FName ScreenID,
		TSubclassOf<UBaseScreen> ScreenClass,
		EUIScreenInputMode InputMode = EUIScreenInputMode::GameOnly);

	/** 服务器通知对应客户端在本机打开常驻界面。 */
	UFUNCTION(Client, Reliable)
	void Client_OpenPersistentScreen(
		FName ScreenID,
		TSubclassOf<UBaseScreen> ScreenClass,
		EUIScreenInputMode InputMode);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputMappingContext> BaseContext;


};
