#include "UI/VM/VMHuntingBaseHUD.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GamePlay/Combat/UCombatComponent.h"
#include "GamePlay/Combat/UHealthComponent.h"
#include "GamePlay/Combat/UHitReactionComponent.h"
#include "GamePlay/MHPlayerState.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMHCombatDebugHUD(
		TEXT("mh.Combat.DebugHUD"),
		0,
		TEXT("Shows combat prediction, state, hit window and recent hit data on the hunting HUD. 0 off, 1 on."),
		ECVF_Cheat);

	const TCHAR* CombatStateToString(EMHCombatState State)
	{
		switch (State)
		{
		case EMHCombatState::Locomotion:
			return TEXT("Locomotion");
		case EMHCombatState::Attack:
			return TEXT("Attack");
		case EMHCombatState::WeaponSwitch:
			return TEXT("WeaponSwitch");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* MovePhaseToString(EMHCombatMovePhase Phase)
	{
		switch (Phase)
		{
		case EMHCombatMovePhase::None:
			return TEXT("None");
		case EMHCombatMovePhase::Startup:
			return TEXT("Startup");
		case EMHCombatMovePhase::Charge:
			return TEXT("Charge");
		case EMHCombatMovePhase::Active:
			return TEXT("Active");
		case EMHCombatMovePhase::Recovery:
			return TEXT("Recovery");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* ComboWindowToString(EMHCombatComboWindowState State)
	{
		switch (State)
		{
		case EMHCombatComboWindowState::Closed:
			return TEXT("Closed");
		case EMHCombatComboWindowState::Pending:
			return TEXT("Pending");
		case EMHCombatComboWindowState::Open:
			return TEXT("Open");
		case EMHCombatComboWindowState::Buffered:
			return TEXT("Buffered");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* ReactionDirectionToString(EMHHitReactionDirection Direction)
	{
		switch (Direction)
		{
		case EMHHitReactionDirection::Front:
			return TEXT("Front");
		case EMHHitReactionDirection::Back:
			return TEXT("Back");
		case EMHHitReactionDirection::Left:
			return TEXT("Left");
		case EMHHitReactionDirection::Right:
			return TEXT("Right");
		default:
			return TEXT("Unknown");
		}
	}
}

void UVMHuntingBaseHUD::OnActivated()
{
	Super::OnActivated();

	BindHealthComponent(ResolveHealthComponent());
	BindCombatComponent(ResolveCombatComponent());
	RefreshCombatDebug();
}

void UVMHuntingBaseHUD::OnDeactivated()
{
	UnbindCombatComponent();
	UnbindHealthComponent();
	Super::OnDeactivated();
}

void UVMHuntingBaseHUD::RefreshAll()
{
	UHealthComponent* HealthComponent = ResolveHealthComponent();
	BindHealthComponent(HealthComponent);
	SetHealthPercent(HealthComponent ? HealthComponent->GetHealthPercent() : 0.f);

	UCombatComponent* CombatComponent = ResolveCombatComponent();
	BindCombatComponent(CombatComponent);
	RefreshCombatDebug();
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

UCombatComponent* UVMHuntingBaseHUD::ResolveCombatComponent() const
{
	AMHPlayerState* PlayerState = GetDataSource<AMHPlayerState>();
	APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;

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

	return Pawn ? Pawn->FindComponentByClass<UCombatComponent>() : nullptr;
}

UHitReactionComponent* UVMHuntingBaseHUD::ResolveHitReactionComponent() const
{
	AMHPlayerState* PlayerState = GetDataSource<AMHPlayerState>();
	APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;

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

	return Pawn ? Pawn->FindComponentByClass<UHitReactionComponent>() : nullptr;
}

void UVMHuntingBaseHUD::SetHealthPercent(float NewPercent)
{
	const float ClampedPercent = FMath::Clamp(NewPercent, 0.f, 1.f);
	HealthPercent.Set(ClampedPercent);
	OnHealthPercentChanged.Broadcast(ClampedPercent);
}

void UVMHuntingBaseHUD::SetCombatDebugText(const FString& NewText)
{
	CombatDebugText.Set(NewText);
	OnCombatDebugTextChanged.Broadcast(NewText);
}

void UVMHuntingBaseHUD::RefreshCombatDebug()
{
	if (CVarMHCombatDebugHUD.GetValueOnAnyThread() == 0)
	{
		SetCombatDebugText(FString());
		return;
	}

	SetCombatDebugText(BuildCombatDebugText());
}

FString UVMHuntingBaseHUD::BuildCombatDebugText() const
{
	UCombatComponent* CombatComponent = BoundCombatComponent.Get();
	UHealthComponent* HealthComponent = BoundHealthComponent.Get();
	if (!CombatComponent)
	{
		return TEXT("Combat Debug\nWaiting for local combat component...");
	}

	float PingMilliseconds = 0.f;
	if (const AMHPlayerState* PlayerState = GetDataSource<AMHPlayerState>())
	{
		PingMilliseconds = PlayerState->GetPingInMilliseconds();
	}

	const float MoveTime = CombatComponent->GetCurrentMoveTime();
	const float MoveLength = CombatComponent->GetCurrentMoveLength();
	const FString MoveTimeText = MoveLength > 0.f
		? FString::Printf(TEXT("%.2f/%.2f"), MoveTime, MoveLength)
		: FString::Printf(TEXT("%.2f/--"), MoveTime);

	FString HealthText = TEXT("--/--");
	if (HealthComponent)
	{
		HealthText = FString::Printf(TEXT("%.1f/%.1f"), HealthComponent->GetHealth(), HealthComponent->GetMaxHealth());
	}

	FString ReactionText = TEXT("None");
	if (const UHitReactionComponent* HitReaction = ResolveHitReactionComponent())
	{
		const FMHHitReactionState& State = HitReaction->GetReactionState();
		if (State.bActive)
		{
			const float RemainingTime = HitReaction->GetReactionRemainingTime();
			ReactionText = FString::Printf(
				TEXT("%s/%s %.2fs"),
				*State.ReactionId.ToString(),
				ReactionDirectionToString(State.Direction),
				FMath::Max(RemainingTime, 0.f));
		}
		else if (HitReaction->IsInvulnerable())
		{
			ReactionText = TEXT("Invulnerable");
		}
		else if (HitReaction->IsSuperArmorActive())
		{
			ReactionText = FString::Printf(TEXT("SuperArmor %.1f"), HitReaction->GetCurrentPoise());
		}
	}

	FString LastHitText = TEXT("None");
	if (LastConfirmedHit.HitId != INDEX_NONE)
	{
		const float HitAge = GetWorld() ? FMath::Max(GetWorld()->GetTimeSeconds() - LastConfirmedHitWorldTime, 0.f) : 0.f;
		LastHitText = FString::Printf(
			TEXT("#%d %s Dmg %.1f HP %.1f Age %.2fs%s%s"),
			LastConfirmedHit.HitId,
			LastConfirmedHit.Target ? *LastConfirmedHit.Target->GetName() : TEXT("null"),
			LastConfirmedHit.AppliedDamage,
			LastConfirmedHit.RemainingHealth,
			HitAge,
			LastConfirmedHit.bKilled ? TEXT(" KILL") : TEXT(""),
			LastConfirmedHit.bSuperArmorBlocked ? TEXT(" ARMOR") : (LastConfirmedHit.bInvulnerable ? TEXT(" I-FRAME") : TEXT("")));
	}

	return FString::Printf(
		TEXT("Combat Debug\n")
		TEXT("Ping %.0fms | Pending %d | Ack %d | Replay %d | ActionSeq %d\n")
		TEXT("State %s/%s | Move %s[%d] | %s | Combo %s | HitWindow %s\n")
		TEXT("HP %s | Reaction %s\n")
		TEXT("LastHit %s"),
		PingMilliseconds,
		CombatComponent->GetPendingInputCount(),
		CombatComponent->GetLastProcessedInputSequence(),
		CombatComponent->GetLastReplayedInputCount(),
		CombatComponent->GetActionStateSequence(),
		CombatStateToString(CombatComponent->GetCombatState()),
		MovePhaseToString(CombatComponent->GetMovePhase()),
		*CombatComponent->GetCurrentMoveId().ToString(),
		CombatComponent->GetCurrentMoveIndex(),
		*MoveTimeText,
		ComboWindowToString(CombatComponent->GetComboWindowState()),
		CombatComponent->IsHitWindowActive() ? TEXT("ON") : TEXT("OFF"),
		*HealthText,
		*ReactionText,
		*LastHitText);
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

void UVMHuntingBaseHUD::BindCombatComponent(UCombatComponent* CombatComponent)
{
	if (BoundCombatComponent.Get() != CombatComponent)
	{
		UnbindCombatComponent();
		BoundCombatComponent = CombatComponent;
	}

	if (CombatComponent && IsActive())
	{
		CombatComponent->OnHitConfirmed.RemoveDynamic(this, &UVMHuntingBaseHUD::HandleHitConfirmed);
		CombatComponent->OnHitConfirmed.AddDynamic(this, &UVMHuntingBaseHUD::HandleHitConfirmed);
	}
}

void UVMHuntingBaseHUD::UnbindCombatComponent()
{
	if (UCombatComponent* CombatComponent = BoundCombatComponent.Get())
	{
		CombatComponent->OnHitConfirmed.RemoveDynamic(this, &UVMHuntingBaseHUD::HandleHitConfirmed);
	}

	BoundCombatComponent.Reset();
}

void UVMHuntingBaseHUD::HandleHealthChanged(float NewHealth, float MaxHealth)
{
	SetHealthPercent(MaxHealth > 0.f ? NewHealth / MaxHealth : 0.f);
}

void UVMHuntingBaseHUD::HandleHitConfirmed(const FMHCombatHitEvent& HitEvent)
{
	LastConfirmedHit = HitEvent;
	LastConfirmedHitWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	RefreshCombatDebug();
}
