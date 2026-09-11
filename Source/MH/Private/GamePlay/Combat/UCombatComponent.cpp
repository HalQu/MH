#include "GamePlay/Combat/UCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "InputAction.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GamePlay/Combat/IMHCombatTargetInterface.h"
#include "GamePlay/Combat/UWeaponDataAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogMHCombatNet, Log, All);

UCombatComponent::UCombatComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UCombatComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UCombatComponent::CacheOwnerReferences()
{
	if (!CachedCharacter)
	{
		CachedCharacter = Cast<ACharacter>(GetOwner());
	}

	if (!CachedCharacter)
	{
		return;
	}

	if (!CachedMesh)
	{
		CachedMesh = CachedCharacter->GetMesh();
	}

	if (!CachedMovement)
	{
		CachedMovement = CachedCharacter->GetCharacterMovement();
	}

	if (!CachedAnimInstance && CachedMesh)
	{
		CachedAnimInstance = CachedMesh->GetAnimInstance();
	}
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCombatComponent, CurrentWeaponPath);
	DOREPLIFETIME(UCombatComponent, CombatState);
	DOREPLIFETIME(UCombatComponent, MovePhase);
	DOREPLIFETIME(UCombatComponent, CurrentMoveId);
	DOREPLIFETIME(UCombatComponent, CurrentMoveIndex);
	DOREPLIFETIME(UCombatComponent, bComboWindowOpen);
}

void UCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	// Existing Blueprint component templates may still contain the old replication default.
	const bool bWasReplicated = GetIsReplicated();
	if (!GetIsReplicated())
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] Component replication flag was false at BeginPlay; enabling it now."));
		SetIsReplicated(true);
	}

	CacheOwnerReferences();
	if (!CachedCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Owner is not a character."));
		return;
	}

	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] BeginPlay Owner=%s NetMode=%d LocalRole=%d RemoteRole=%d Authority=%d LocallyControlled=%d ReplicatedBefore=%d Replicated=%d"),
		*CachedCharacter->GetName(),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
		static_cast<int32>(CachedCharacter->GetLocalRole()),
		static_cast<int32>(CachedCharacter->GetRemoteRole()),
		CachedCharacter->HasAuthority() ? 1 : 0,
		CachedCharacter->IsLocallyControlled() ? 1 : 0,
		bWasReplicated ? 1 : 0,
		GetIsReplicated() ? 1 : 0);
	BindMontageDelegates();

	LoadoutWeapons.RemoveAll([](const TObjectPtr<UWeaponDataAsset>& Weapon)
	{
		return !Weapon;
	});

	if (!CurrentWeapon && !CurrentWeaponPath.IsNull())
	{
		CurrentWeapon = Cast<UWeaponDataAsset>(CurrentWeaponPath.TryLoad());
	}

	if (CurrentWeapon)
	{
		if (GetOwner() && GetOwner()->HasAuthority() && CurrentWeaponPath.IsNull())
		{
			CurrentWeaponPath = FSoftObjectPath(CurrentWeapon);
		}

		UpdateWeaponMesh(CurrentWeapon);
	}
	else if (GetOwner() && GetOwner()->HasAuthority())
	{
		EquipWeapon_Default();
	}
}

void UCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindMontageDelegates();
	UpdateMovementLock(false);
	AttackInputPressTimes.Reset();
	ClearBufferedComboInput();
	CancelPredictedMove(false);
	CurrentChargeInputAction = nullptr;
	bIsCharging = false;
	bChargeInputHeld = false;
	Super::EndPlay(EndPlayReason);
}

void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bHasPendingPredictedMove && GetWorld() && GetWorld()->GetTimeSeconds() - PendingPredictedMove.StartTime > MovePredictionTimeout)
	{
		CancelPredictedMove(true);
	}

	if ((CombatState == EMHCombatState::Attack || bHasPendingPredictedMove) && CachedAnimInstance && CurrentMoveData.Montage)
	{
		CurrentMoveTime = CachedAnimInstance->Montage_GetPosition(CurrentMoveData.Montage);
	}
}

bool UCombatComponent::HandleComboInput(UInputAction* InputAction, ETriggerEvent TriggerEvent)
{
	if (!InputAction || !CachedCharacter || !GetOwner() || !GetWorld())
	{
		return false;
	}

	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] HandleComboInput Owner=%s NetMode=%d LocalRole=%d Authority=%d LocallyControlled=%d Action=%s Trigger=%d State=%d"),
		*CachedCharacter->GetName(),
		static_cast<int32>(GetWorld()->GetNetMode()),
		static_cast<int32>(GetOwner()->GetLocalRole()),
		GetOwner()->HasAuthority() ? 1 : 0,
		CachedCharacter->IsLocallyControlled() ? 1 : 0,
		*InputAction->GetName(),
		static_cast<int32>(TriggerEvent),
		static_cast<int32>(CombatState));

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	float HoldDuration = 0.f;
	if (TriggerEvent == ETriggerEvent::Started)
	{
		AttackInputPressTimes.Add(InputAction, CurrentTime);
	}
	else if (TriggerEvent == ETriggerEvent::Triggered)
	{
		if (const float* PressTime = AttackInputPressTimes.Find(InputAction))
		{
			HoldDuration = CurrentTime - *PressTime;
		}
	}
	else if (TriggerEvent == ETriggerEvent::Completed)
	{
		if (const float* PressTime = AttackInputPressTimes.Find(InputAction))
		{
			HoldDuration = CurrentTime - *PressTime;
		}
		AttackInputPressTimes.Remove(InputAction);
	}

	if (PendingServerHoldDuration >= 0.f)
	{
		HoldDuration = PendingServerHoldDuration;
		PendingServerHoldDuration = -1.f;
	}

	const ENetRole OwnerRole = GetOwner()->GetLocalRole();

	FMHCombatInputSnapshot InputSnapshot{InputAction, TriggerEvent, CurrentMoveInput, HoldDuration};
	if (OwnerRole == ROLE_Authority)
	{
		InputSnapshot.ClientInputSequence = PendingServerInputSequence;
	}

	if (OwnerRole == ROLE_AutonomousProxy && CachedCharacter->IsLocallyControlled())
	{
		InputSnapshot.ClientInputSequence = ++LocalInputSequence;
		bool bChargeRelease = false;

		const bool bHasPredictedCharge = bHasPendingPredictedMove && PendingPredictedMove.Montage == CurrentMoveData.Montage;
		if ((CombatState == EMHCombatState::Attack || bHasPredictedCharge)
			&& bIsChargeMove
			&& TriggerEvent == ETriggerEvent::Completed
			&& bChargeInputHeld)
		{
			bChargeRelease = true;
			if (bIsCharging && CachedAnimInstance && CurrentMoveData.Montage)
			{
				CachedAnimInstance->Montage_SetPlayRate(CurrentMoveData.Montage, CurrentMoveData.MontagePlayRate);
			}
			bIsCharging = false;
			bChargeInputHeld = false;
		}

		TryPredictMove(InputSnapshot, bChargeRelease);

		UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] Sending Server_HandleComboInput Sequence=%d Action=%s Trigger=%d Predicted=%d"), InputSnapshot.ClientInputSequence, *InputAction->GetName(), static_cast<int32>(TriggerEvent), bHasPendingPredictedMove ? 1 : 0);
		Server_HandleComboInput(FSoftObjectPath(InputAction), TriggerEvent, CurrentMoveInput, HoldDuration, InputSnapshot.ClientInputSequence);
		return true;
	}

	if (OwnerRole != ROLE_Authority)
	{
		UE_LOG(LogMHCombatNet, Verbose, TEXT("[CombatNet] HandleComboInput ignored on simulated proxy. Owner=%s"), *CachedCharacter->GetName());
		return false;
	}

	if (CombatState == EMHCombatState::Attack
		&& bIsChargeMove
		&& TriggerEvent == ETriggerEvent::Completed
		&& CurrentChargeInputAction.Get() == InputAction
		&& bChargeInputHeld)
	{
		UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] Charge input released for move: %s"), *CurrentMoveData.MoveId.ToString());
		if (bIsCharging && CachedAnimInstance && CurrentMoveData.Montage)
		{
			//ReleaseCharge();
			bIsCharging = false;
			CachedAnimInstance->Montage_SetPlayRate(CurrentMoveData.Montage, CurrentMoveData.MontagePlayRate);
		}
		bChargeInputHeld = false;
		return true;
	}
	
	if (CombatState == EMHCombatState::Locomotion || CombatState == EMHCombatState::WeaponSwitch)
	{
		return TryStartAttack(InputSnapshot);
	}

	if (CombatState == EMHCombatState::Attack)
	{
		return BufferNextCombo(InputSnapshot);
	}

	return false;
}

void UCombatComponent::Server_HandleComboInput_Implementation(const FSoftObjectPath& InputActionPath, ETriggerEvent TriggerEvent, FVector2D MoveInput, float HoldDuration, int32 ClientSequence)
{
	AActor* Owner = GetOwner();
	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] Server_HandleComboInput received Owner=%s NetMode=%d LocalRole=%d Authority=%d Sequence=%d LastSequence=%d Action=%s Trigger=%d Hold=%.3f"),
		Owner ? *Owner->GetName() : TEXT("null"),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
		Owner ? static_cast<int32>(Owner->GetLocalRole()) : -1,
		Owner && Owner->HasAuthority() ? 1 : 0,
		ClientSequence,
		LastReceivedInputSequence,
		*InputActionPath.ToString(),
		static_cast<int32>(TriggerEvent),
		HoldDuration);
	if (!Owner || !Owner->HasAuthority())
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] Server_HandleComboInput rejected: no authority."));
		return;
	}

	if (ClientSequence <= LastReceivedInputSequence)
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] Server_HandleComboInput rejected: stale sequence %d <= %d."), ClientSequence, LastReceivedInputSequence);
		return;
	}

	LastReceivedInputSequence = ClientSequence;
	UInputAction* InputAction = Cast<UInputAction>(InputActionPath.TryLoad());
	if (!InputAction)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Server received an unknown input action path: %s"), *InputActionPath.ToString());
		return;
	}

	PendingServerHoldDuration = HoldDuration;
	CurrentMoveInput = MoveInput;
	PendingServerInputSequence = ClientSequence;
	HandleComboInput(InputAction, TriggerEvent);
	PendingServerInputSequence = INDEX_NONE;
	PendingServerHoldDuration = -1.f;
}

bool UCombatComponent::TryPredictMove(const FMHCombatInputSnapshot& Input, bool bChargeRelease)
{
	if (bChargeRelease
		|| bHasPendingPredictedMove
		|| !CachedCharacter
		|| !CachedCharacter->IsLocallyControlled()
		|| !GetOwner()
		|| GetOwner()->GetLocalRole() != ROLE_AutonomousProxy
		|| !CurrentWeapon)
	{
		return false;
	}

	// First pass only predicts new attacks. Chained moves still wait for the authoritative combo window.
	if (CombatState != EMHCombatState::Locomotion && CombatState != EMHCombatState::WeaponSwitch)
	{
		return false;
	}

	const TMap<FComboCondition, int32>& StartMoves = IsAirborne() ? CurrentWeapon->AirStartMoves : CurrentWeapon->GroundStartMoves;
	const int32 MoveIndex = FindBestComboIndex(StartMoves, Input);
	const FMHCombatMoveData* Move = GetMove(MoveIndex);
	if (!Move || !Move->Montage)
	{
		return false;
	}

	PendingPredictedMove.InputSequence = Input.ClientInputSequence;
	PendingPredictedMove.WeaponPath = CurrentWeaponPath.IsNull() ? FSoftObjectPath(CurrentWeapon) : CurrentWeaponPath;
	PendingPredictedMove.MoveIndex = MoveIndex;
	PendingPredictedMove.SectionName = Move->SectionName;
	PendingPredictedMove.PlayRate = Move->MontagePlayRate;
	PendingPredictedMove.StartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	PendingPredictedMove.Montage = Move->Montage;
	bHasPendingPredictedMove = true;

	PlayMovePresentation(CurrentWeapon, MoveIndex, Move->SectionName, Move->MontagePlayRate);
	if (!CachedAnimInstance || !CachedAnimInstance->Montage_IsPlaying(Move->Montage))
	{
		CancelPredictedMove(false);
		return false;
	}

	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] Predicting Move Owner=%s Sequence=%d Weapon=%s MoveIndex=%d Montage=%s"),
		*CachedCharacter->GetName(),
		Input.ClientInputSequence,
		*PendingPredictedMove.WeaponPath.ToString(),
		MoveIndex,
		*Move->Montage->GetName());
	return true;
}

void UCombatComponent::ConfirmPredictedMove(int32 ClientInputSequence, const FSoftObjectPath& WeaponPath, int32 MoveIndex)
{
	if (!bHasPendingPredictedMove || PendingPredictedMove.InputSequence != ClientInputSequence)
	{
		return;
	}

	if (PendingPredictedMove.WeaponPath != WeaponPath || PendingPredictedMove.MoveIndex != MoveIndex)
	{
		CancelPredictedMove(false);
		return;
	}

	const int32 ConfirmedSequence = PendingPredictedMove.InputSequence;
	bHasPendingPredictedMove = false;
	if (const FMHCombatMoveData* ConfirmedMove = GetMove(MoveIndex))
	{
		CurrentMoveIndex = MoveIndex;
		CurrentMoveId = ConfirmedMove->MoveId;
		CurrentMoveTime = 0.f;
		if (CombatState != EMHCombatState::Attack)
		{
			SetCombatState(
				EMHCombatState::Attack,
				ConfirmedMove->bIsChargeMove ? EMHCombatMovePhase::Charge : EMHCombatMovePhase::Startup);
		}
	}
	PendingPredictedMove = FMHCombatPredictedMove();

	UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] Prediction confirmed Owner=%s Sequence=%d MoveIndex=%d"),
		CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"), ConfirmedSequence, MoveIndex);
}

void UCombatComponent::CancelPredictedMove(bool bTimedOut)
{
	if (!bHasPendingPredictedMove)
	{
		return;
	}

	const FMHCombatPredictedMove Prediction = PendingPredictedMove;
	bHasPendingPredictedMove = false;
	PendingPredictedMove = FMHCombatPredictedMove();

	if (CachedCharacter && Prediction.Montage)
	{
		CachedCharacter->StopAnimMontage(Prediction.Montage);
	}

	if (CurrentMoveData.Montage == Prediction.Montage)
	{
		CurrentMoveData = FMHCombatMoveData();
		CurrentMoveTime = 0.f;
		bIsChargeMove = false;
		bIsCharging = false;
		bChargeInputHeld = false;
		CurrentChargeInputAction = nullptr;
	}

	if (bTimedOut)
	{
		UE_LOG(LogMHCombatNet, Warning,
			TEXT("[CombatNet] Prediction cancelled Owner=%s Sequence=%d TimedOut=1"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"), Prediction.InputSequence);
	}
	else
	{
		UE_LOG(LogMHCombatNet, Log,
			TEXT("[CombatNet] Prediction cancelled Owner=%s Sequence=%d TimedOut=0"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"), Prediction.InputSequence);
	}
}

bool UCombatComponent::TryStartAttack(const FMHCombatInputSnapshot& Input)
{
	UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] TryStartAttack() called with InputAction: %s, TriggerEvent: %d"), Input.InputAction ? *Input.InputAction->GetName() : TEXT("null"), static_cast<int32>(Input.TriggerEvent));
	if (!CurrentWeapon)
	{
		return false;
	}

	const TMap<FComboCondition, int32>& StartMoves = IsAirborne() ? CurrentWeapon->AirStartMoves : CurrentWeapon->GroundStartMoves;
	const int32 MoveIndex = FindBestComboIndex(StartMoves, Input);
	if (MoveIndex == INDEX_NONE)
	{
		return false;
	}

	const FMHCombatMoveData* Move = GetMove(MoveIndex);
	return Move && StartMove(*Move, MoveIndex, Input.TriggerEvent == ETriggerEvent::Started ? Input.InputAction : nullptr, Input.ClientInputSequence);
}

bool UCombatComponent::BufferNextCombo(const FMHCombatInputSnapshot& Input)
{
	if (!CurrentMoveData.bCanChain)
	{
		return false;
	}

	const int32 NextMoveIndex = FindBestComboIndex(CurrentMoveData.ComboChain, Input);
	if (NextMoveIndex == INDEX_NONE)
	{
		return false;
	}

	if (!IsComboInputAllowed())
	{
		return false;
	}

	BufferedComboInput = Input;
	bHasBufferedComboInput = true;

	TryStartNextCombo();
	return true;
}

int32 UCombatComponent::FindBestComboIndex(const TMap<FComboCondition, int32>& ComboMoves, const FMHCombatInputSnapshot& Input) const
{
	int32 BestIndex = INDEX_NONE;
	float BestScore = -1.f;

	for (const TPair<FComboCondition, int32>& Pair : ComboMoves)
	{
		const FComboCondition& Condition = Pair.Key;
		if (!MatchesComboCondition(Condition, Input))
		{
			continue;
		}

		float ConditionScore = 0.f;
		if (Condition.bCheckHoldDuration)
		{
			ConditionScore += 1000.f + FMath::Max(0.f, Condition.MinHoldDuration) * 100.f;
		}
		if (Condition.bCheckMoveDirection)
		{
			ConditionScore += 100.f + Condition.MoveDirectionThreshold.Size() * 10.f;
		}

		if (ConditionScore > BestScore + KINDA_SMALL_NUMBER)
		{
			BestScore = ConditionScore;
			BestIndex = Pair.Value;
		}
	}

	return BestIndex;
}

bool UCombatComponent::MatchesComboCondition(const FComboCondition& Condition, const FMHCombatInputSnapshot& Input) const
{
	if (!Condition.InputAction || Condition.TriggerEvent == ETriggerEvent::None)
	{
		return false;
	}

	if (Condition.InputAction.Get() != Input.InputAction || Condition.TriggerEvent != Input.TriggerEvent)
	{
		return false;
	}

	if (Condition.bCheckHoldDuration)
	{
		if (Input.TriggerEvent != ETriggerEvent::Completed)
		{
			return false;
		}
		if (Input.HoldDuration + KINDA_SMALL_NUMBER < Condition.MinHoldDuration)
		{
			return false;
		}
	}

	if (Condition.bCheckMoveDirection)
	{
		const FVector2D Threshold = Condition.MoveDirectionThreshold;
		if (!FMath::IsNearlyZero(Threshold.X, 0.001f))
		{
			if (Threshold.X > 0.f && Input.MoveInput.X < Threshold.X - 0.001f)
			{
				return false;
			}
			if (Threshold.X < 0.f && Input.MoveInput.X > Threshold.X + 0.001f)
			{
				return false;
			}
		}
		if (!FMath::IsNearlyZero(Threshold.Y, 0.001f))
		{
			if (Threshold.Y > 0.f && Input.MoveInput.Y < Threshold.Y - 0.001f)
			{
				return false;
			}
			if (Threshold.Y < 0.f && Input.MoveInput.Y > Threshold.Y + 0.001f)
			{
				return false;
			}
		}
	}

	return true;
}

void UCombatComponent::ClearBufferedComboInput()
{
	bHasBufferedComboInput = false;
	BufferedComboInput = FMHCombatInputSnapshot();
}

bool UCombatComponent::CycleWeapon(int32 Delta)
{
	if (LoadoutWeapons.IsEmpty())
	{
		return DefaultWeapon ? EquipWeapon(DefaultWeapon) : false;
	}

	int32 CurrentIndex = CurrentWeaponIndex;
	if (CurrentIndex == INDEX_NONE && CurrentWeapon)
	{
		CurrentIndex = LoadoutWeapons.IndexOfByPredicate([this](const TObjectPtr<UWeaponDataAsset>& Weapon)
		{
			return Weapon == CurrentWeapon;
		});
	}

	if (CurrentIndex == INDEX_NONE)
	{
		CurrentIndex = 0;
	}

	const int32 WeaponCount = LoadoutWeapons.Num();
	const int32 Direction = Delta >= 0 ? 1 : -1;
	const int32 NextIndex = ((CurrentIndex + FMath::Abs(Delta) * Direction) % WeaponCount + WeaponCount) % WeaponCount;
	return EquipWeapon(LoadoutWeapons[NextIndex]);
}

bool UCombatComponent::EquipWeapon(UWeaponDataAsset* NewWeapon)
{
	UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] EquipWeapon() called with NewWeapon: %s"), NewWeapon ? *NewWeapon->GetName() : TEXT("null"));
	if (!NewWeapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] EquipWeapon() called with null NewWeapon."));
		return false;
	}

	if (CurrentWeapon == NewWeapon)
	{
		UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] Weapon is already equipped."));
		return true;
	}

	if (!CanSwitchWeaponNow())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Cannot switch weapon now."));
		return false;
	}

	if (CombatState == EMHCombatState::Attack)
	{
		FinishCurrentMove(false);
	}

	CurrentWeapon = NewWeapon;
	CurrentWeaponPath = FSoftObjectPath(NewWeapon);

	UpdateWeaponMesh(NewWeapon);
	UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] Equipped weapon: %s"), *NewWeapon->GetName());
	CurrentMoveIndex = INDEX_NONE;
	CurrentMoveId = NAME_None;
	OnWeaponChanged.Broadcast(CurrentWeapon);
	return true;
}

bool UCombatComponent::EquipWeapon_Default()
{
	UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] EquipWeapon_Default() called. Equipping default weapon."));
	return EquipWeapon(DefaultWeapon);
}

void UCombatComponent::CancelCurrentAttack()
{
	FinishCurrentMove(true);
}

bool UCombatComponent::AddWeaponToLoadout(UWeaponDataAsset* Weapon)
{
	if (!Weapon)
	{
		return false;
	}

	if (LoadoutWeapons.ContainsByPredicate([Weapon](const TObjectPtr<UWeaponDataAsset>& LoadoutWeapon)
	{
		return LoadoutWeapon == Weapon;
	}))
	{
		return true;
	}

	LoadoutWeapons.Add(Weapon);

	if (!CurrentWeapon)
	{
		return EquipWeapon(Weapon);
	}

	return true;
}

bool UCombatComponent::RemoveWeaponFromLoadout(UWeaponDataAsset* Weapon)
{
	if (!Weapon)
	{
		return false;
	}

	if (CurrentWeapon == Weapon)
	{
		return false;
	}

	const int32 WeaponIndex = LoadoutWeapons.IndexOfByPredicate([Weapon](const TObjectPtr<UWeaponDataAsset>& LoadoutWeapon)
	{
		return LoadoutWeapon == Weapon;
	});

	if (WeaponIndex == INDEX_NONE)
	{
		return false;
	}

	LoadoutWeapons.RemoveAt(WeaponIndex);
	return true;
}

void UCombatComponent::HandleCombatNotify(EMHCombatNotifyType NotifyType, UAnimMontage* SourceMontage)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (CombatState != EMHCombatState::Attack || !IsCurrentMontage(SourceMontage))
	{
		return;
	}

	switch (NotifyType)
	{
	case EMHCombatNotifyType::AttackStart:
		HandleAttackStart();
		break;
	case EMHCombatNotifyType::AttackHit:
		if (MovePhase == EMHCombatMovePhase::Active)
		{
			PerformHitCheck();
		}
		break;
	case EMHCombatNotifyType::RecoveryStart:
		SetCombatState(CombatState, EMHCombatMovePhase::Recovery);
		break;
	case EMHCombatNotifyType::MoveEnd:
		if (!TryStartNextCombo())
		{
			FinishCurrentMove(false);
		}
		break;
	}
}

void UCombatComponent::HandleCombatNotifyState(EMHCombatNotifyStateType StateType, EMHCombatNotifyStateEvent StateEvent, UAnimMontage* SourceMontage)
{
	if (!IsCurrentMontage(SourceMontage))
	{
		return;
	}

	const bool bPredictedPresentation = bHasPendingPredictedMove
		&& GetOwner()
		&& !GetOwner()->HasAuthority();
	if (CombatState != EMHCombatState::Attack && !bPredictedPresentation)
	{
		return;
	}

	switch (StateType)
	{
	case EMHCombatNotifyStateType::ChargeWindow:
		if (StateEvent == EMHCombatNotifyStateEvent::Begin)
		{
			if (CachedAnimInstance && CachedAnimInstance->Montage_IsPlaying(CurrentMoveData.Montage)&&bChargeInputHeld)
			{
				bIsCharging = true;
				CachedAnimInstance->Montage_SetPlayRate(CurrentMoveData.Montage, CurrentMoveData.MontagePlayRate * 0.3f);
			}
		}
		else if (StateEvent == EMHCombatNotifyStateEvent::End)
		{
			if (CachedAnimInstance && CachedAnimInstance->Montage_IsPlaying(CurrentMoveData.Montage)&& bIsCharging)
			{
				bIsCharging = false;
				CachedAnimInstance->Montage_SetPlayRate(CurrentMoveData.Montage, CurrentMoveData.MontagePlayRate);
			}
		}
		break;
	case EMHCombatNotifyStateType::ComboWindow:
		if (!GetOwner() || !GetOwner()->HasAuthority())
		{
			break;
		}
		if (StateEvent == EMHCombatNotifyStateEvent::Begin)
		{
			bComboWindowPending = false;
			bComboWindowOpen = true;
			bComboWindowClosed = false;
			TryStartNextCombo();
		}
		else if (StateEvent == EMHCombatNotifyStateEvent::End)
		{
			bComboWindowOpen = false;
			bComboWindowClosed = true;
			if (UWorld* World = GetWorld())
			{
				ComboWindowCloseTime = World->GetTimeSeconds();
			}
			TryStartNextCombo();
		}
		break;
	case EMHCombatNotifyStateType::WeaponSwitchAllowed:
		if (!GetOwner() || !GetOwner()->HasAuthority())
		{
			break;
		}
		if (StateEvent == EMHCombatNotifyStateEvent::Begin)
		{
			bWeaponSwitchAllowed = true;
		}
		else if (StateEvent == EMHCombatNotifyStateEvent::End)
		{
			bWeaponSwitchAllowed = false;
		}
		break;
	}
}

UWeaponDataAsset* UCombatComponent::GetCurrentWeapon() const
{
	return CurrentWeapon;
}

FName UCombatComponent::GetCurrentWeaponId() const
{
	return CurrentWeapon ? CurrentWeapon->WeaponId : NAME_None;
}

FMHCombatMoveData UCombatComponent::GetCurrentMoveData() const
{
	return CurrentMoveData;
}

EMHCombatState UCombatComponent::GetCombatState() const
{
	return CombatState;
}

EMHCombatMovePhase UCombatComponent::GetMovePhase() const
{
	return MovePhase;
}

int32 UCombatComponent::GetCurrentMoveIndex() const
{
	return CurrentMoveIndex;
}

float UCombatComponent::GetCurrentMoveTime() const
{
	return CurrentMoveTime;
}

bool UCombatComponent::IsAirborne() const
{
	return CachedMovement && CachedMovement->IsFalling();
}

bool UCombatComponent::CanSwitchWeaponNow() const
{
	if (CombatState != EMHCombatState::Attack)
	{
		return true;
	}

	return bWeaponSwitchAllowed;
}

void UCombatComponent::OnRep_CurrentWeaponPath()
{
	CacheOwnerReferences();
	if (CurrentWeaponPath.IsNull())
	{
		CurrentWeapon = nullptr;
		UpdateWeaponMesh(nullptr);
		return;
	}

	UWeaponDataAsset* NewWeapon = Cast<UWeaponDataAsset>(CurrentWeaponPath.TryLoad());
	if (!NewWeapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Could not resolve replicated weapon path: %s"), *CurrentWeaponPath.ToString());
		return;
	}

	if (CurrentWeapon != NewWeapon)
	{
		CurrentWeapon = NewWeapon;
		OnWeaponChanged.Broadcast(CurrentWeapon);
	}

	UpdateWeaponMesh(NewWeapon);

	OnRep_CurrentMoveIndex();
}

void UCombatComponent::OnRep_CombatState() const
{
	OnCombatStateChanged.Broadcast(CombatState, MovePhase);
}

void UCombatComponent::OnRep_CurrentMoveIndex()
{
	if (CurrentMoveIndex == INDEX_NONE || !CurrentWeapon)
	{
		if (!GetOwner() || !GetOwner()->HasAuthority())
		{
			CurrentMoveData = FMHCombatMoveData();
		}
		return;
	}

	if (const FMHCombatMoveData* Move = GetMove(CurrentMoveIndex))
	{
		CurrentMoveData = *Move;
	}
}

void UCombatComponent::UpdateWeaponMesh(UWeaponDataAsset* NewWeapon)
{
	if (CurrentWeaponMesh)
	{
		CurrentWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		CurrentWeaponMesh->UnregisterComponent();
		CurrentWeaponMesh->DestroyComponent();
		CurrentWeaponMesh = nullptr;
	}

	if (!NewWeapon || !NewWeapon->MeshAsset || !CachedCharacter)
	{
		return;
	}

	CurrentWeaponMesh = NewObject<UStaticMeshComponent>(CachedCharacter);
	if (!CurrentWeaponMesh)
	{
		return;
	}

	CurrentWeaponMesh->SetStaticMesh(NewWeapon->MeshAsset);
	CurrentWeaponMesh->RegisterComponent();

	if (USkeletalMeshComponent* SkeletalMesh = CachedCharacter->GetMesh())
	{
		CurrentWeaponMesh->AttachToComponent(
			SkeletalMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			DefaultHitOriginSocketName);
	}

	CurrentWeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CurrentWeaponMesh->SetVisibility(true);
}

void UCombatComponent::PlayMovePresentation(UWeaponDataAsset* MoveWeapon, int32 MoveIndex, FName SectionName, float PlayRate)
{
	CacheOwnerReferences();
	if (!CachedCharacter || !MoveWeapon || !MoveWeapon->Moves.IsValidIndex(MoveIndex))
	{
		UE_LOG(LogMHCombatNet, Warning,
			TEXT("[CombatNet] PlayMovePresentation aborted. Owner=%s Character=%d Weapon=%d MoveIndex=%d"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("null"),
			CachedCharacter ? 1 : 0,
			MoveWeapon ? 1 : 0,
			MoveIndex);
		return;
	}

	const FMHCombatMoveData& Move = MoveWeapon->Moves[MoveIndex];
	if (!Move.Montage)
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] PlayMovePresentation aborted: montage is null. Owner=%s MoveIndex=%d"), *CachedCharacter->GetName(), MoveIndex);
		return;
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		if (CurrentMoveData.Montage && CurrentMoveData.Montage != Move.Montage)
		{
			CachedCharacter->StopAnimMontage(CurrentMoveData.Montage);
		}
		CurrentMoveData = Move;
		bIsChargeMove = Move.bIsChargeMove;
		CurrentChargeInputAction = nullptr;
		bChargeInputHeld = Move.bIsChargeMove;
		bIsCharging = false;
	}

	BindMontageDelegates();
	const FName PlaySection = SectionName.IsNone() ? Move.SectionName : SectionName;
	const float PlayLength = CachedCharacter->PlayAnimMontage(Move.Montage, PlayRate, PlaySection);
	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] PlayMovePresentation Owner=%s NetMode=%d LocalRole=%d Authority=%d MoveIndex=%d Section=%s Montage=%s PlayRate=%.3f PlayLength=%.3f"),
		*CachedCharacter->GetName(),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
		static_cast<int32>(GetOwner() ? GetOwner()->GetLocalRole() : ROLE_None),
		GetOwner() && GetOwner()->HasAuthority() ? 1 : 0,
		MoveIndex,
		*PlaySection.ToString(),
		*Move.Montage->GetName(),
		PlayRate,
		PlayLength);
	if (PlayLength <= 0.f)
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] PlayAnimMontage failed. Owner=%s Montage=%s"), *CachedCharacter->GetName(), *Move.Montage->GetName());
	}
	OnAttackStarted.Broadcast(Move.MoveId);
}

void UCombatComponent::StopMovePresentation(UWeaponDataAsset* MoveWeapon, int32 MoveIndex)
{
	UAnimMontage* MontageToStop = nullptr;
	if (MoveWeapon && MoveWeapon->Moves.IsValidIndex(MoveIndex))
	{
		MontageToStop = MoveWeapon->Moves[MoveIndex].Montage;
	}

	if (!MontageToStop)
	{
		MontageToStop = CurrentMoveData.Montage;
	}

	if (CachedCharacter && MontageToStop)
	{
		CachedCharacter->StopAnimMontage(MontageToStop);
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		CurrentMoveData = FMHCombatMoveData();
		bIsChargeMove = false;
		CurrentChargeInputAction = nullptr;
		bIsCharging = false;
		bChargeInputHeld = false;
	}
}

void UCombatComponent::Multicast_PlayMove_Implementation(const FSoftObjectPath& WeaponPath, int32 MoveIndex, FName SectionName, float PlayRate, int32 ClientInputSequence)
{
	CacheOwnerReferences();
	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] Multicast_PlayMove received Owner=%s NetMode=%d LocalRole=%d Authority=%d LocallyControlled=%d Replicated=%d WeaponPath=%s MoveIndex=%d Section=%s PlayRate=%.3f InputSequence=%d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("null"),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
		GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : -1,
		GetOwner() && GetOwner()->HasAuthority() ? 1 : 0,
		CachedCharacter && CachedCharacter->IsLocallyControlled() ? 1 : 0,
		GetIsReplicated() ? 1 : 0,
		*WeaponPath.ToString(),
		MoveIndex,
		*SectionName.ToString(),
		PlayRate,
		ClientInputSequence);

	UWeaponDataAsset* MoveWeapon = Cast<UWeaponDataAsset>(WeaponPath.TryLoad());
	if (!MoveWeapon)
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] Multicast_PlayMove could not resolve weapon path: %s"), *WeaponPath.ToString());
		return;
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		if (CurrentWeaponPath.IsNull() || CurrentWeaponPath == WeaponPath)
		{
			CurrentWeapon = MoveWeapon;
		}

		if (bHasPendingPredictedMove && PendingPredictedMove.InputSequence == ClientInputSequence)
		{
			const bool bMatchesPrediction = PendingPredictedMove.WeaponPath == WeaponPath
				&& PendingPredictedMove.MoveIndex == MoveIndex;
			ConfirmPredictedMove(ClientInputSequence, WeaponPath, MoveIndex);
			if (bMatchesPrediction)
			{
				return;
			}
		}

		CancelPredictedMove(false);
	}

	PlayMovePresentation(MoveWeapon, MoveIndex, SectionName, PlayRate);
}

void UCombatComponent::Multicast_StopMove_Implementation(const FSoftObjectPath& WeaponPath, int32 MoveIndex)
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		CancelPredictedMove(false);
	}

	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] Multicast_StopMove received Owner=%s NetMode=%d LocalRole=%d Authority=%d MoveIndex=%d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("null"),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
		GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : -1,
		GetOwner() && GetOwner()->HasAuthority() ? 1 : 0,
		MoveIndex);
	UWeaponDataAsset* MoveWeapon = Cast<UWeaponDataAsset>(WeaponPath.TryLoad());
	StopMovePresentation(MoveWeapon, MoveIndex);
}

void UCombatComponent::OnMove(const FVector2D& MoveInput)
{
	CurrentMoveInput = MoveInput;
}

bool UCombatComponent::StartMove(const FMHCombatMoveData& Move, int32 MoveIndex, UInputAction* SourceInputAction, int32 ClientInputSequence)
{
	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] StartMove Owner=%s NetMode=%d LocalRole=%d Authority=%d Replicated=%d MoveId=%s MoveIndex=%d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("null"),
		static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
		GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : -1,
		GetOwner() && GetOwner()->HasAuthority() ? 1 : 0,
		GetIsReplicated() ? 1 : 0,
		*Move.MoveId.ToString(),
		MoveIndex);
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	CacheOwnerReferences();
	if (!CachedCharacter)
	{
		return false;
	}

	if (!Move.Montage)
	{
		return false;
	}

	UAnimMontage* PreviousMontage = (CombatState == EMHCombatState::Attack)
		? CurrentMoveData.Montage
		: nullptr;

	CurrentMoveData = Move;
	CurrentMoveIndex = MoveIndex;
	CurrentMoveId = Move.MoveId;
	CurrentMoveTime = 0.f;
	ClearBufferedComboInput();
	bHitExecuted = false;
	bWeaponSwitchAllowed = false;
	bComboWindowOpen = false;
	bComboWindowPending = true;
	bComboWindowClosed = false;
	ComboWindowCloseTime = 0.f;
	HitActorsThisMove.Reset();

	//bIsCharging = Move.bIsChargeMove;
	bIsChargeMove = Move.bIsChargeMove;
	CurrentChargeInputAction = Move.bIsChargeMove ? SourceInputAction : nullptr;
	bChargeInputHeld = Move.bIsChargeMove && SourceInputAction != nullptr;
	
	if (bLockGroundMovementDuringAttack && !IsAirborne())
	{
		UpdateMovementLock(true);
	}

	BindMontageDelegates();

	if (PreviousMontage && PreviousMontage != CurrentMoveData.Montage)
	{
		CachedCharacter->StopAnimMontage(PreviousMontage);
	}

	const EMHCombatMovePhase InitialPhase = Move.bIsChargeMove
		? EMHCombatMovePhase::Charge
		: EMHCombatMovePhase::Startup;
	SetCombatState(EMHCombatState::Attack, InitialPhase);

	Multicast_PlayMove(CurrentWeaponPath, CurrentMoveIndex, CurrentMoveData.SectionName, CurrentMoveData.MontagePlayRate, ClientInputSequence);
	return true;
}

bool UCombatComponent::TryStartNextCombo()
{
	if (!bHasBufferedComboInput || !CurrentMoveData.bCanChain)
	{
		return false;
	}

	const bool bCanStartChain = CanStartBufferedCombo();

	if (!bCanStartChain)
	{
		return false;
	}

	const int32 NextMoveIndex = FindBestComboIndex(CurrentMoveData.ComboChain, BufferedComboInput);
	if (NextMoveIndex == INDEX_NONE)
	{
		ClearBufferedComboInput();
		return false;
	}

	const FMHCombatMoveData* NextMove = GetMove(NextMoveIndex);
	if (!NextMove)
	{
		ClearBufferedComboInput();
		return false;
	}

	UInputAction* SourceInputAction = BufferedComboInput.TriggerEvent == ETriggerEvent::Started
		? BufferedComboInput.InputAction
		: nullptr;
	const int32 ClientInputSequence = BufferedComboInput.ClientInputSequence;
	ClearBufferedComboInput();
	return StartMove(*NextMove, NextMoveIndex, SourceInputAction, ClientInputSequence);
}

const FMHCombatMoveData* UCombatComponent::GetMove(int32 MoveIndex) const
{
	if (!CurrentWeapon || !CurrentWeapon->Moves.IsValidIndex(MoveIndex))
	{
		return nullptr;
	}

	return &CurrentWeapon->Moves[MoveIndex];
}

void UCombatComponent::FinishCurrentMove(bool bInterrupted)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (CombatState != EMHCombatState::Attack)
	{
		return;
	}

	UAnimMontage* PlayingMontage = CurrentMoveData.Montage;

	const FSoftObjectPath MoveWeaponPath = CurrentWeaponPath;
	const int32 MoveIndex = CurrentMoveIndex;
	Multicast_StopMove(MoveWeaponPath, MoveIndex);

	UpdateMovementLock(false);
	SetCombatState(EMHCombatState::Locomotion, EMHCombatMovePhase::None);

	CurrentMoveData = FMHCombatMoveData();
	CurrentMoveId = NAME_None;
	CurrentMoveTime = 0.f;
	CurrentMoveIndex = INDEX_NONE;
	ClearBufferedComboInput();
	bHitExecuted = false;
	bWeaponSwitchAllowed = false;
	bComboWindowOpen = false;
	bComboWindowPending = false;
	bComboWindowClosed = false;
	ComboWindowCloseTime = 0.f;
	HitActorsThisMove.Reset();
	CurrentChargeInputAction = nullptr;
	bIsChargeMove = false;
	bIsCharging = false;
	bChargeInputHeld = false;

	if (PlayingMontage && CachedCharacter)
	{
		CachedCharacter->StopAnimMontage(PlayingMontage);
	}

	OnAttackEnded.Broadcast(bInterrupted);
}

bool UCombatComponent::IsComboInputAllowed() const
{
	if (!GetWorld())
	{
		return false;
	}

	if (bComboWindowPending || bComboWindowOpen)
	{
		return true;
	}

	return bComboWindowClosed
		&& GetWorld()->GetTimeSeconds() - ComboWindowCloseTime <= AttackInputBufferDuration;
}

bool UCombatComponent::CanStartBufferedCombo() const
{
	if (bComboWindowOpen)
	{
		return true;
	}

	if (!bComboWindowClosed || !GetWorld())
	{
		return false;
	}

	return GetWorld()->GetTimeSeconds() - ComboWindowCloseTime <= AttackInputBufferDuration;
}

void UCombatComponent::ReleaseCharge()
{
	UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Releasing charge."));
	if (!CachedAnimInstance || !CurrentMoveData.Montage)
	{
		if (!CachedAnimInstance)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Cannot release charge: Missing AnimInstance."));
		}
		if (!CurrentMoveData.Montage)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Cannot release charge: Missing Montage."));
		}
		return;
	}

	if (CurrentMoveData.AttackSectionName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Charge move is missing AttackSectionName."));
		return;
	}

	CachedAnimInstance->Montage_JumpToSection(CurrentMoveData.AttackSectionName, CurrentMoveData.Montage);
	if (bIsCharging)
	{
		HandleAttackStart();
	}
	UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Charge released, jumped to section: %s"), *CurrentMoveData.AttackSectionName.ToString());
	return;
}

void UCombatComponent::HandleAttackStart()
{
	bIsCharging = false;
	if (MovePhase == EMHCombatMovePhase::Startup || MovePhase == EMHCombatMovePhase::Charge)
	{
		SetCombatState(CombatState, EMHCombatMovePhase::Active);
	}
}

void UCombatComponent::PerformHitCheck()
{
	if (bHitExecuted || !CachedCharacter || !CachedMesh || !CurrentWeapon || !GetWorld())
	{
		return;
	}

	bHitExecuted = true;

	const FName SocketName = CurrentMoveData.HitOriginSocketName.IsNone()
		? DefaultHitOriginSocketName
		: CurrentMoveData.HitOriginSocketName;

	FVector Origin = CachedCharacter->GetActorLocation();
	if (CachedMesh->DoesSocketExist(SocketName))
	{
		Origin = CachedMesh->GetSocketLocation(SocketName);
	}
	else
	{
		const float ForwardOffset = FMath::Max(CurrentMoveData.HitRange, 0.f) * 0.5f;
		Origin += CachedCharacter->GetActorForwardVector() * ForwardOffset;
	}

	const float Radius = FMath::Max(CurrentMoveData.HitRadius, 20.f);
	const FCollisionShape HitShape = FCollisionShape::MakeSphere(Radius);
	const FCollisionQueryParams QueryParams(FName(TEXT("MHCombatHit")), false, CachedCharacter);

	TArray<FOverlapResult> Overlaps;
	if (!GetWorld()->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, HitShape, QueryParams))
	{
		return;
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		if (!Target
			|| Target == CachedCharacter
			|| !Target->GetClass()->ImplementsInterface(UMHCombatTargetInterface::StaticClass())
			|| HitActorsThisMove.ContainsByPredicate([Target](const TWeakObjectPtr<AActor>& ExistingHit)
			{
				return ExistingHit.Get() == Target;
			}))
		{
			continue;
		}

		HitActorsThisMove.Add(Target);
		const FVector HitLocation = Overlap.Component.IsValid() ? Overlap.Component->GetComponentLocation() : Target->GetActorLocation();
		const FVector HitNormal = -CachedCharacter->GetActorForwardVector();
		ApplyDamageToTarget(Target, HitLocation, HitNormal);
	}
}

void UCombatComponent::ApplyDamageToTarget(AActor* Target, const FVector& HitLocation, const FVector& HitNormal)
{
	FMHDamageEvent DamageEvent;
	DamageEvent.Source = CachedCharacter;
	DamageEvent.Weapon = CurrentWeapon;
	DamageEvent.Damage = ResolveDamage(CurrentMoveData);
	DamageEvent.HitLocation = HitLocation;
	DamageEvent.HitNormal = HitNormal;
	DamageEvent.LaunchImpulse = CurrentMoveData.LaunchImpulse;

	IMHCombatTargetInterface::Execute_ReceiveDamage(Target, DamageEvent);
}

float UCombatComponent::ResolveDamage(const FMHCombatMoveData& MoveData) const
{
	return MoveData.Damage * DamageMultiplier;
}

void UCombatComponent::UpdateMovementLock(bool bLock)
{
	if (!CachedMovement)
	{
		return;
	}

	if (bLock)
	{
		if (bMovementLocked)
		{
			return;
		}
		if (!CachedMovement->IsFalling())
		{
			SavedMovementMode = CachedMovement->MovementMode;
			CachedMovement->DisableMovement();
			bMovementLocked = true;
		}

		return;
	}

	if (bMovementLocked)
	{
		CachedMovement->SetMovementMode(SavedMovementMode);
		bMovementLocked = false;
	}
}

void UCombatComponent::SetCombatState(EMHCombatState NewState, EMHCombatMovePhase NewPhase)
{
	if (CombatState == NewState && MovePhase == NewPhase)
	{
		return;
	}

	CombatState = NewState;
	MovePhase = NewPhase;
	OnCombatStateChanged.Broadcast(CombatState, MovePhase);
}

void UCombatComponent::BindMontageDelegates()
{
	if (!CachedAnimInstance || bMontageDelegatesBound)
	{
		return;
	}

	if (!CachedCharacter || !CachedCharacter->GetMesh())
	{
		return;
	}

	CachedAnimInstance = CachedCharacter->GetMesh()->GetAnimInstance();
	if (!CachedAnimInstance)
	{
		return;
	}

	CachedAnimInstance->OnMontageBlendingOut.AddDynamic(this, &UCombatComponent::HandleMontageBlendingOut);
	CachedAnimInstance->OnMontageEnded.AddDynamic(this, &UCombatComponent::HandleMontageEnded);
	bMontageDelegatesBound = true;
}

void UCombatComponent::UnbindMontageDelegates()
{
	if (CachedAnimInstance && bMontageDelegatesBound)
	{
		CachedAnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UCombatComponent::HandleMontageBlendingOut);
		CachedAnimInstance->OnMontageEnded.RemoveDynamic(this, &UCombatComponent::HandleMontageEnded);
	}

	bMontageDelegatesBound = false;
}

bool UCombatComponent::IsCurrentMontage(UAnimMontage* Montage) const
{
	return Montage && CurrentMoveData.Montage == Montage;
}

void UCombatComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (!IsCurrentMontage(Montage))
	{
		return;
	}

	TryStartNextCombo();
}

void UCombatComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (!IsCurrentMontage(Montage))
	{
		return;
	}

	if (!TryStartNextCombo())
	{
		FinishCurrentMove(bInterrupted);
	}
}
