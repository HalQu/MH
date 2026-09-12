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
	// 动作状态必须排在 CurrentWeaponPath / CombatState 之后：OnRep_ActionState
	// 解析动作时，武器与状态已经在同一批数据里就绪。
	DOREPLIFETIME(UCombatComponent, ActionState);
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
	CancelPredictedMove(false, false);
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

	if (!GetOwner() || !GetOwner()->HasAuthority() || !ActionState.bActive)
	{
		return;
	}

	// 命中窗口是持续状态，服务器在每个 Tick 对武器上一帧到当前帧的轨迹做扫掠。
	if (bHitWindowActive)
	{
		PerformHitSweep();
	}

	// 蓄力开关与播放进度是同一个动作内部的增量数据，直接写进复制状态。
	// 客户端收到后只做表现修正，不会重播蒙太奇（Sequence 没有变化）。
	if (ActionState.bCharging != bIsCharging)
	{
		ActionState.bCharging = bIsCharging;
	}

	ActionPositionSyncAccumulator += DeltaTime;
	if (ActionPositionSyncInterval <= 0.f || ActionPositionSyncAccumulator >= ActionPositionSyncInterval)
	{
		ActionPositionSyncAccumulator = 0.f;
		ActionState.MontagePosition = CurrentMoveTime;
	}
}

bool UCombatComponent::HandleComboInput(UInputAction* InputAction, ETriggerEvent TriggerEvent)
{
	if (!bCombatEnabled || !InputAction || !CachedCharacter || !GetOwner() || !GetWorld())
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
			// 本机已经预测松手，服务器确认之前不要被旧的 bCharging 拉回去。
			bChargeReleasePredicted = true;
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

	CacheOwnerReferences();

	int32 MoveIndex = INDEX_NONE;
	if (CombatState == EMHCombatState::Locomotion || CombatState == EMHCombatState::WeaponSwitch)
	{
		const TMap<FComboCondition, int32>& StartMoves = IsAirborne() ? CurrentWeapon->AirStartMoves : CurrentWeapon->GroundStartMoves;
		MoveIndex = FindBestComboIndex(StartMoves, Input);
	}
	else if (CombatState == EMHCombatState::Attack)
	{
		if (!CurrentMoveData.bCanChain || !IsComboInputAllowed())
		{
			return false;
		}

		MoveIndex = FindBestComboIndex(CurrentMoveData.ComboChain, Input);
	}
	else
	{
		return false;
	}

	const FMHCombatMoveData* Move = GetMove(MoveIndex);
	if (!Move || !Move->Montage)
	{
		return false;
	}

	FMHCombatPredictedMove Prediction;
	Prediction.InputSequence = Input.ClientInputSequence;
	Prediction.WeaponPath = CurrentWeaponPath.IsNull() ? FSoftObjectPath(CurrentWeapon) : CurrentWeaponPath;
	Prediction.MoveIndex = MoveIndex;
	Prediction.SectionName = Move->SectionName;
	Prediction.PlayRate = Move->MontagePlayRate;
	Prediction.StartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Prediction.Montage = Move->Montage;

	if (CombatState == EMHCombatState::Attack && CurrentMoveData.Montage)
	{
		Prediction.bHadPreviousMove = true;
		Prediction.PreviousMoveData = CurrentMoveData;
		Prediction.PreviousMoveIndex = CurrentMoveIndex;
		Prediction.PreviousMoveId = CurrentMoveId;
		Prediction.PreviousMontage = CurrentMoveData.Montage;
		Prediction.PreviousMontagePosition = CachedAnimInstance
			? CachedAnimInstance->Montage_GetPosition(Prediction.PreviousMontage)
			: 0.f;
		Prediction.PreviousEffectivePlayRate = Prediction.PreviousMoveData.MontagePlayRate;
		if (bIsCharging)
		{
			Prediction.PreviousEffectivePlayRate *= ChargePlayRateScale;
		}
		Prediction.bPreviousChargeMove = bIsChargeMove;
		Prediction.bPreviousCharging = bIsCharging;
		Prediction.bPreviousChargeInputHeld = bChargeInputHeld;
	}

	PendingPredictedMove = Prediction;
	bHasPendingPredictedMove = true;

	PlayMovePresentation(CurrentWeapon, MoveIndex, Move->SectionName, Move->MontagePlayRate);
	if (!CachedAnimInstance || !CachedAnimInstance->Montage_IsPlaying(Move->Montage))
	{
		CancelPredictedMove(false);
		return false;
	}

	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] Predicting Move Owner=%s Sequence=%d Weapon=%s MoveIndex=%d Montage=%s Chained=%d"),
		*CachedCharacter->GetName(),
		Input.ClientInputSequence,
		*PendingPredictedMove.WeaponPath.ToString(),
		MoveIndex,
		*Move->Montage->GetName(),
		Prediction.bHadPreviousMove ? 1 : 0);
	return true;
}

void UCombatComponent::ConfirmPredictedMove()
{
	if (!bHasPendingPredictedMove)
	{
		return;
	}

	const int32 ConfirmedSequence = PendingPredictedMove.InputSequence;
	bHasPendingPredictedMove = false;
	PendingPredictedMove = FMHCombatPredictedMove();

	// 本地预测已经在放同一个蒙太奇，这里只把派生缓存对齐到权威值，
	// 不重播；后续由 SyncActionPresentation 修正蓄力速率与进度。
	CurrentMoveIndex = ActionState.MoveIndex;
	if (const FMHCombatMoveData* ConfirmedMove = GetMove(ActionState.MoveIndex))
	{
		CurrentMoveId = ConfirmedMove->MoveId;
	}

	UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] Prediction confirmed Owner=%s Sequence=%d MoveIndex=%d"),
		CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"), ConfirmedSequence, ActionState.MoveIndex);
}

void UCombatComponent::CancelPredictedMove(bool bTimedOut, bool bRestorePrevious)
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

	const bool bPredictionWasCurrent = CurrentMoveData.Montage == Prediction.Montage;
	if (bPredictionWasCurrent && bRestorePrevious && Prediction.bHadPreviousMove && Prediction.PreviousMontage && CachedCharacter)
	{
		CurrentMoveData = Prediction.PreviousMoveData;
		CurrentMoveIndex = Prediction.PreviousMoveIndex;
		CurrentMoveId = Prediction.PreviousMoveId;
		bIsChargeMove = Prediction.bPreviousChargeMove;
		bIsCharging = Prediction.bPreviousCharging;
		bChargeInputHeld = Prediction.bPreviousChargeInputHeld;
		CurrentChargeInputAction = nullptr;

		const float ElapsedTime = GetWorld()
			? FMath::Max(0.f, GetWorld()->GetTimeSeconds() - Prediction.StartTime)
			: 0.f;
		float RestorePosition = Prediction.PreviousMontagePosition + ElapsedTime * Prediction.PreviousEffectivePlayRate;
		const float PlayLength = Prediction.PreviousMontage->GetPlayLength();
		if (PlayLength > 0.f)
		{
			RestorePosition = FMath::Min(RestorePosition, PlayLength);
		}

		CachedCharacter->PlayAnimMontage(Prediction.PreviousMontage, Prediction.PreviousMoveData.MontagePlayRate, Prediction.PreviousMoveData.SectionName);
		if (CachedAnimInstance)
		{
			CachedAnimInstance->Montage_SetPosition(Prediction.PreviousMontage, RestorePosition);
			CachedAnimInstance->Montage_SetPlayRate(Prediction.PreviousMontage, Prediction.PreviousEffectivePlayRate);
		}
		CurrentMoveTime = RestorePosition;
	}
	else if (bPredictionWasCurrent)
	{
		CurrentMoveData = FMHCombatMoveData();
		CurrentMoveTime = 0.f;
		bIsChargeMove = false;
		bIsCharging = false;
		bChargeInputHeld = false;
		CurrentChargeInputAction = nullptr;
	}

	const int32 RestoredPrevious = bRestorePrevious && Prediction.bHadPreviousMove && bPredictionWasCurrent ? 1 : 0;
	if (bTimedOut)
	{
		UE_LOG(LogMHCombatNet, Warning,
			TEXT("[CombatNet] Prediction cancelled Owner=%s Sequence=%d TimedOut=1 RestoredPrevious=%d"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"), Prediction.InputSequence, RestoredPrevious);
	}
	else
	{
		UE_LOG(LogMHCombatNet, Log,
			TEXT("[CombatNet] Prediction cancelled Owner=%s Sequence=%d TimedOut=0 RestoredPrevious=%d"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"), Prediction.InputSequence, RestoredPrevious);
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
	if (!bCombatEnabled)
	{
		return false;
	}

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

void UCombatComponent::SetCombatEnabled(bool bEnabled)
{
	if (bCombatEnabled == bEnabled)
	{
		return;
	}

	bCombatEnabled = bEnabled;
	if (bEnabled)
	{
		return;
	}

	ClearBufferedComboInput();
	CancelPredictedMove(false, false);

	if (GetOwner() && GetOwner()->HasAuthority() && CombatState == EMHCombatState::Attack)
	{
		FinishCurrentMove(true);
	}
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
				CachedAnimInstance->Montage_SetPlayRate(CurrentMoveData.Montage, CurrentMoveData.MontagePlayRate * ChargePlayRateScale);
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
	case EMHCombatNotifyStateType::AttackHitWindow:
		if (!GetOwner() || !GetOwner()->HasAuthority())
		{
			break;
		}

		if (StateEvent == EMHCombatNotifyStateEvent::Begin)
		{
			if (MovePhase == EMHCombatMovePhase::Active)
			{
				BeginHitWindow();
			}
			else
			{
				UE_LOG(LogMHCombatNet, Warning,
					TEXT("[CombatNet] AttackHitWindow ignored before Active phase. Owner=%s Move=%s Phase=%d"),
					CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"),
					*CurrentMoveData.MoveId.ToString(),
					static_cast<int32>(MovePhase));
			}
		}
		else if (StateEvent == EMHCombatNotifyStateEvent::End)
		{
			EndHitWindow();
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

	// 武器换掉之后，还在播放的权威动作要按新武器的动作表刷新一次缓存。
	if (ActionState.bActive)
	{
		if (const FMHCombatMoveData* Move = GetMove(ActionState.MoveIndex))
		{
			CurrentMoveData = *Move;
			CurrentMoveIndex = ActionState.MoveIndex;
			CurrentMoveId = Move->MoveId;
		}
	}
}

void UCombatComponent::OnRep_CombatState() const
{
	OnCombatStateChanged.Broadcast(CombatState, MovePhase);
}

/*
 * 动作状态是“状态”而不是“事件”：客户端不依赖单次 RPC 是否到达，
 * 只要复制状态变了就会被调用。因此丢包重传、迟到加入、相关性恢复都能自动补齐。
 */
void UCombatComponent::OnRep_ActionState()
{
	CacheOwnerReferences();

	// 服务器确认蓄力结束，本机“松手”的预测可以收回了。
	if (!ActionState.bCharging)
	{
		bChargeReleasePredicted = false;
	}

	if (AppliedActionSequence == ActionState.Sequence)
	{
		// 同一个动作内部的增量刷新（蓄力状态、播放进度），绝不能重播蒙太奇。
		SyncActionPresentation();
		return;
	}

	const bool bPredictionConfirmed = ActionState.bActive
		&& bHasPendingPredictedMove
		&& PendingPredictedMove.InputSequence == ActionState.InputSequence
		&& PendingPredictedMove.MoveIndex == ActionState.MoveIndex;

	AppliedActionSequence = ActionState.Sequence;

	if (bPredictionConfirmed)
	{
		// 服务端认可了这次预测：本地蒙太奇已经在放，不要重新播。
		ConfirmPredictedMove();
		SyncActionPresentation();
		return;
	}

	if (bHasPendingPredictedMove)
	{
		// 服务器选了另一个动作（或直接结束了）：回滚预测，再应用权威状态。
		CancelPredictedMove(false, false);
	}

	ApplyActionState();
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

void UCombatComponent::PlayMovePresentation(UWeaponDataAsset* MoveWeapon, int32 MoveIndex, FName SectionName, float PlayRate, float StartPosition)
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

	// 换动作时先收掉上一条蒙太奇；同一条蒙太奇的重播交给 PlayAnimMontage。
	if (CurrentMoveData.Montage && CurrentMoveData.Montage != Move.Montage)
	{
		CachedCharacter->StopAnimMontage(CurrentMoveData.Montage);
	}

	CurrentMoveData = Move;
	CurrentMoveIndex = MoveIndex;
	CurrentMoveId = Move.MoveId;
	CurrentMoveTime = 0.f;

	// 服务器上的按键/蓄力上下文由 StartMove 写入，这里不能覆盖；
	// 纯表现端没有输入上下文，按动作自身的数据补齐。
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
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
		return;
	}

	// 迟到加入或相关性恢复时，服务器会把当前进度一并复制过来，这里直接对齐。
	if (StartPosition > 0.f && CachedAnimInstance)
	{
		const float TargetPosition = FMath::Clamp(StartPosition, 0.f, PlayLength);
		CachedAnimInstance->Montage_SetPosition(Move.Montage, TargetPosition);
		CurrentMoveTime = TargetPosition;
	}

	bPresentationActive = true;
	OnAttackStarted.Broadcast(Move.MoveId);
}

UWeaponDataAsset* UCombatComponent::ResolveCurrentWeaponFromPath()
{
	if (CurrentWeaponPath.IsNull())
	{
		return CurrentWeapon;
	}

	if (CurrentWeapon && FSoftObjectPath(CurrentWeapon) == CurrentWeaponPath)
	{
		return CurrentWeapon;
	}

	if (UWeaponDataAsset* Loaded = Cast<UWeaponDataAsset>(CurrentWeaponPath.TryLoad()))
	{
		CurrentWeapon = Loaded;
	}

	return CurrentWeapon;
}

void UCombatComponent::CommitActionState()
{
	++ActionState.Sequence;
	ActionPositionSyncAccumulator = 0.f;
	ApplyActionState();
}

void UCombatComponent::ApplyActionState()
{
	CacheOwnerReferences();
	AppliedActionSequence = ActionState.Sequence;

	if (!ActionState.bActive)
	{
		ClearPresentationState(ActionState.bInterrupted);
		return;
	}

	UWeaponDataAsset* MoveWeapon = ResolveCurrentWeaponFromPath();
	if (!MoveWeapon || !MoveWeapon->Moves.IsValidIndex(ActionState.MoveIndex))
	{
		UE_LOG(LogMHCombatNet, Warning,
			TEXT("[CombatNet] ApplyActionState could not resolve move. Owner=%s Weapon=%d MoveIndex=%d"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("null"),
			MoveWeapon ? 1 : 0,
			ActionState.MoveIndex);
		return;
	}

	PlayMovePresentation(MoveWeapon, ActionState.MoveIndex, ActionState.SectionName, ActionState.PlayRate, ActionState.MontagePosition);
	ApplyChargePresentation();
}

void UCombatComponent::SyncActionPresentation()
{
	if (!ActionState.bActive)
	{
		return;
	}

	// 同一动作内的状态刷新：只要蒙太奇还在放就不重播，只修正蓄力。
	if (!CurrentMoveData.Montage || !CachedAnimInstance || !CachedAnimInstance->Montage_IsPlaying(CurrentMoveData.Montage))
	{
		// 本地表现已经丢了（相关性恢复、被别的蒙太奇顶掉等），用权威进度重新拉起。
		ApplyActionState();
		return;
	}

	ApplyChargePresentation();
}

void UCombatComponent::ApplyChargePresentation()
{
	if (!CachedAnimInstance || !CurrentMoveData.Montage)
	{
		return;
	}

	if (bChargeReleasePredicted)
	{
		// 本机已经预测松手，等服务器把 bCharging 置回 false 再对齐。
		return;
	}

	const bool bShouldCharge = ActionState.bActive && ActionState.bCharging;
	if (bIsCharging == bShouldCharge)
	{
		return;
	}

	bIsCharging = bShouldCharge;
	if (CachedAnimInstance->Montage_IsPlaying(CurrentMoveData.Montage))
	{
		CachedAnimInstance->Montage_SetPlayRate(
			CurrentMoveData.Montage,
			CurrentMoveData.MontagePlayRate * (bShouldCharge ? ChargePlayRateScale : 1.f));
	}
}

void UCombatComponent::ClearPresentationState(bool bInterrupted)
{
	CacheOwnerReferences();

	if (CachedCharacter)
	{
		if (UAnimMontage* PlayingMontage = CurrentMoveData.Montage)
		{
			CachedCharacter->StopAnimMontage(PlayingMontage);
		}
	}

	const bool bWasPresenting = bPresentationActive;
	bPresentationActive = false;

	CurrentMoveData = FMHCombatMoveData();
	CurrentMoveId = NAME_None;
	CurrentMoveIndex = INDEX_NONE;
	CurrentMoveTime = 0.f;
	CurrentChargeInputAction = nullptr;
	bIsChargeMove = false;
	bIsCharging = false;
	bChargeInputHeld = false;
	bChargeReleasePredicted = false;
	bHitWindowActive = false;
	bHasPreviousHitOrigin = false;

	if (bWasPresenting)
	{
		OnAttackEnded.Broadcast(bInterrupted);
	}
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

	CurrentMoveData = Move;
	CurrentMoveIndex = MoveIndex;
	CurrentMoveId = Move.MoveId;
	CurrentMoveTime = 0.f;
	ClearBufferedComboInput();
	bHitExecuted = false;
	bHitWindowActive = false;
	bHasPreviousHitOrigin = false;
	bWeaponSwitchAllowed = false;
	bComboWindowOpen = false;
	bComboWindowPending = true;
	bComboWindowClosed = false;
	ComboWindowCloseTime = 0.f;
	HitActorsThisMove.Reset();

	bIsChargeMove = Move.bIsChargeMove;
	CurrentChargeInputAction = Move.bIsChargeMove ? SourceInputAction : nullptr;
	bChargeInputHeld = Move.bIsChargeMove && SourceInputAction != nullptr;
	bIsCharging = false;
	bChargeReleasePredicted = false;
	
	if (bLockGroundMovementDuringAttack && !IsAirborne())
	{
		UpdateMovementLock(true);
	}

	BindMontageDelegates();
	const EMHCombatMovePhase InitialPhase = Move.bIsChargeMove
		? EMHCombatMovePhase::Charge
		: EMHCombatMovePhase::Startup;
	SetCombatState(EMHCombatState::Attack, InitialPhase);

	// 把新动作写进复制状态：自增 Sequence 后立即在本机应用。
	// 客户端靠 OnRep_ActionState 拿到同一份状态并开始播放。
	ActionState.InputSequence = ClientInputSequence;
	ActionState.bActive = true;
	ActionState.MoveIndex = MoveIndex;
	ActionState.SectionName = Move.SectionName;
	ActionState.PlayRate = Move.MontagePlayRate;
	ActionState.bCharging = false;
	ActionState.bInterrupted = false;
	ActionState.MontagePosition = 0.f;
	CommitActionState();
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

	UpdateMovementLock(false);
	SetCombatState(EMHCombatState::Locomotion, EMHCombatMovePhase::None);

	ClearBufferedComboInput();
	bHitExecuted = false;
	bHitWindowActive = false;
	bHasPreviousHitOrigin = false;
	bWeaponSwitchAllowed = false;
	bComboWindowOpen = false;
	bComboWindowPending = false;
	bComboWindowClosed = false;
	ComboWindowCloseTime = 0.f;
	HitActorsThisMove.Reset();
	// 停止表现（停蒙太奇、清缓存、广播 AttackEnded）统一交给状态层：
	// 服务器和每个客户端走同一条路径，只有真正播出过动作的一端会广播。
	ActionState.bActive = false;
	ActionState.bInterrupted = bInterrupted;
	ActionState.bCharging = false;
	ActionState.MontagePosition = 0.f;
	CommitActionState();
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

void UCombatComponent::BeginHitWindow()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !CachedCharacter || !CachedMesh)
	{
		return;
	}

	bHitWindowActive = true;
	bHasPreviousHitOrigin = true;
	PreviousHitOrigin = ResolveHitOrigin();

	// 窗口开始时目标可能已经和武器重叠，先做一次零长度查询。
	PerformHitQuery(PreviousHitOrigin, PreviousHitOrigin);

	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] Attack hit window begin. Attacker=%s Move=%s Origin=%s Radius=%.1f"),
		CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"),
		*CurrentMoveData.MoveId.ToString(),
		*PreviousHitOrigin.ToString(),
		FMath::Max(CurrentMoveData.HitRadius, 20.f));
}

void UCombatComponent::EndHitWindow()
{
	if (bHitWindowActive)
	{
		UE_LOG(LogMHCombatNet, Log,
			TEXT("[CombatNet] Attack hit window end. Attacker=%s Move=%s HitTargets=%d"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"),
			*CurrentMoveData.MoveId.ToString(),
			HitActorsThisMove.Num());
	}

	bHitWindowActive = false;
	bHasPreviousHitOrigin = false;
}

void UCombatComponent::PerformHitSweep()
{
	if (!bHitWindowActive || !CachedCharacter || !CachedMesh || !CurrentWeapon || !GetWorld())
	{
		return;
	}

	const FVector CurrentHitOrigin = ResolveHitOrigin();
	if (!bHasPreviousHitOrigin)
	{
		PreviousHitOrigin = CurrentHitOrigin;
		bHasPreviousHitOrigin = true;
	}

	PerformHitQuery(PreviousHitOrigin, CurrentHitOrigin);
	PreviousHitOrigin = CurrentHitOrigin;
}

FVector UCombatComponent::ResolveHitOrigin() const
{
	if (!CachedCharacter || !CachedMesh)
	{
		return FVector::ZeroVector;
	}

	const FName SocketName = CurrentMoveData.HitOriginSocketName.IsNone()
		? DefaultHitOriginSocketName
		: CurrentMoveData.HitOriginSocketName;

	if (CachedMesh->DoesSocketExist(SocketName))
	{
		return CachedMesh->GetSocketLocation(SocketName);
	}

	const float ForwardOffset = FMath::Max(CurrentMoveData.HitRange, 0.f) * 0.5f;
	return CachedCharacter->GetActorLocation()
		+ CachedCharacter->GetActorForwardVector() * ForwardOffset;
}

void UCombatComponent::PerformHitQuery(const FVector& Start, const FVector& End)
{
	if (!GetWorld() || !CachedCharacter || !CachedMesh)
	{
		return;
	}

	const float Radius = FMath::Max(CurrentMoveData.HitRadius, 20.f);
	const FCollisionShape HitShape = FCollisionShape::MakeSphere(Radius);
	const FCollisionQueryParams QueryParams(FName(TEXT("MHCombatHit")), false, CachedCharacter);
	const bool bHasMovement = FVector::DistSquared(Start, End) > FMath::Square(0.1f);

	if (!bHasMovement)
	{
		TArray<FOverlapResult> Overlaps;
		if (!GetWorld()->OverlapMultiByChannel(Overlaps, End, FQuat::Identity, ECC_Pawn, HitShape, QueryParams))
		{
			return;
		}

		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* Target = Overlap.GetActor();
			if (!Target)
			{
				continue;
			}

			const FVector HitLocation = Overlap.Component.IsValid()
				? Overlap.Component->GetComponentLocation()
				: Target->GetActorLocation();
			TryApplyHit(Target, HitLocation, -CachedCharacter->GetActorForwardVector());
		}

		return;
	}

	TArray<FHitResult> Hits;
	if (!GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Pawn, HitShape, QueryParams))
	{
		return;
	}

	for (const FHitResult& Hit : Hits)
	{
		AActor* Target = Hit.GetActor();
		if (!Target)
		{
			continue;
		}

		FVector HitLocation(Hit.ImpactPoint);
		if (HitLocation.IsNearlyZero())
		{
			HitLocation = Hit.Component.IsValid()
				? Hit.Component->GetComponentLocation()
				: Target->GetActorLocation();
		}

		const FVector HitNormal = Hit.ImpactNormal.IsNearlyZero()
			? FVector(-CachedCharacter->GetActorForwardVector())
			: FVector(Hit.ImpactNormal);
		TryApplyHit(Target, HitLocation, HitNormal);
	}
}

bool UCombatComponent::TryApplyHit(AActor* Target, const FVector& HitLocation, const FVector& HitNormal)
{
	if (!Target
		|| Target == CachedCharacter
		|| !Target->GetClass()->ImplementsInterface(UMHCombatTargetInterface::StaticClass())
		|| HitActorsThisMove.ContainsByPredicate([Target](const TWeakObjectPtr<AActor>& ExistingHit)
		{
			return ExistingHit.Get() == Target;
		}))
	{
		return false;
	}

	HitActorsThisMove.Add(Target);
	ApplyDamageToTarget(Target, HitLocation, HitNormal);
	return true;
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
		UE_LOG(LogMHCombatNet, Log,
			TEXT("[CombatNet] Hit check found no overlap. Attacker=%s Move=%s Socket=%s Origin=%s Radius=%.1f"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"),
			*CurrentMoveData.MoveId.ToString(),
			*SocketName.ToString(),
			*Origin.ToString(),
			Radius);
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

	UE_LOG(LogMHCombatNet, Log,
		TEXT("[CombatNet] Hit resolved Attacker=%s Target=%s Move=%s RequestedDamage=%.2f"),
		CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"),
		*Target->GetName(),
		*CurrentMoveData.MoveId.ToString(),
		DamageEvent.Damage);
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
