// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/VM/VMHuntingBaseHUD.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GamePlay/Combat/UHealthComponent.h"
#include "GamePlay/MHPlayerState.h"

void UVMHuntingBaseHUD::OnActivated()
{
	Super::OnActivated();

	BindHealthComponent(ResolveHealthComponent());
}

void UVMHuntingBaseHUD::OnDeactivated()
{
	UnbindHealthComponent();
	Super::OnDeactivated();
}

void UVMHuntingBaseHUD::RefreshAll()
{
	UHealthComponent* HealthComponent = ResolveHealthComponent();
	BindHealthComponent(HealthComponent);

	SetHealthPercent(HealthComponent ? HealthComponent->GetHealthPercent() : 0.f);
}

UHealthComponent* UVMHuntingBaseHUD::ResolveHealthComponent() const
{
	AMHPlayerState* PlayerState = GetDataSource<AMHPlayerState>();
	APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;

	// 在网络复制或重生切换的瞬间，PlayerState 的 Pawn 可能尚未更新；
	// 本地控制器已经指向新 Pawn，因此把它作为兜底数据源。
	if (!Pawn)
	{
		if (const UUserWidget* Widget = Cast<UUserWidget>(OuterWidget.Get()))
		{
			if (APlayerController* PlayerController = Widget->GetOwningPlayer())
			{
				Pawn = PlayerController->GetPawn();
			}
		}
	}

	return Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
}

void UVMHuntingBaseHUD::SetHealthPercent(float NewPercent)
{
	const float ClampedPercent = FMath::Clamp(NewPercent, 0.f, 1.f);
	HealthPercent.Set(ClampedPercent);
	OnHealthPercentChanged.Broadcast(ClampedPercent);
}

void UVMHuntingBaseHUD::BindHealthComponent(UHealthComponent* HealthComponent)
{
	if (BoundHealthComponent.Get() != HealthComponent)
	{
		UnbindHealthComponent();
		BoundHealthComponent = HealthComponent;
	}

	if (HealthComponent && IsActive())
	{
		// 页面可能先被覆盖时刷新过数据源，恢复激活后需要重新建立委托。
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &UVMHuntingBaseHUD::HandleHealthChanged);
		HealthComponent->OnHealthChanged.AddDynamic(this, &UVMHuntingBaseHUD::HandleHealthChanged);
	}
}

void UVMHuntingBaseHUD::UnbindHealthComponent()
{
	if (UHealthComponent* HealthComponent = BoundHealthComponent.Get())
	{
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &UVMHuntingBaseHUD::HandleHealthChanged);
	}

	BoundHealthComponent.Reset();
}

void UVMHuntingBaseHUD::HandleHealthChanged(float NewHealth, float MaxHealth)
{
	SetHealthPercent(MaxHealth > 0.f ? NewHealth / MaxHealth : 0.f);
}

