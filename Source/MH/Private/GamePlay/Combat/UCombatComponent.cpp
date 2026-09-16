#include "GamePlay/Combat/UCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#endif
#include "InputAction.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GamePlay/Combat/IMHCombatTargetInterface.h"
#include "GamePlay/Combat/UHitReactionComponent.h"
#include "GamePlay/Combat/UWeaponDataAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogMHCombatNet, Log, All);

UInputAction* FMHCombatInputCommand::ResolveInputAction() const
{
	return InputAction.LoadSynchronous();
}

namespace
{
	/** Combat network logging: 0 off, 1 input/reconciliation summaries, 2 per-command detail. */
	static TAutoConsoleVariable<int32> CVarMHCombatNetLog(
		TEXT("mh.Combat.NetLog"),
		0,
		TEXT("Combat network log level: 0 off, 1 input and prediction summary, 2 per-command detail."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarMHCombatPredictionEnabled(
		TEXT("mh.Combat.Prediction"),
		1,
		TEXT("Enables client-side combat prediction and rollback replay. 0 follows authoritative state only."),
		ECVF_Cheat);

#if ENABLE_DRAW_DEBUG
	static TAutoConsoleVariable<float> CVarMHCombatDrawHitSweep(
		TEXT("mh.Combat.DrawHitSweep"),
		0.f,
		TEXT("Draws server weapon hit sweeps. Value is debug draw duration; 0 disables."),
		ECVF_Cheat);
#endif

	int32 GetCombatNetLogLevel()
	{
		return CVarMHCombatNetLog.GetValueOnAnyThread();
	}

	FString GetComboConditionStableKey(const FComboCondition& Condition)
	{
		return Condition.InputAction ? Condition.InputAction->GetPathName() : FString();
	}

	float GetMontageSectionStartPosition(const UAnimMontage* Montage, FName SectionName)
	{
		if (!Montage || SectionName.IsNone())
		{
			return 0.f;
		}

		const int32 SectionIndex = Montage->GetSectionIndex(SectionName);
		if (!Montage->IsValidSectionIndex(SectionIndex))
		{
			return 0.f;
		}

		float SectionStart = 0.f;
		float SectionEnd = 0.f;
		Montage->GetSectionStartAndEndTime(SectionIndex, SectionStart, SectionEnd);
		return SectionStart;
	}

	/** Independent of TMap iteration order, so server and client select the same move. */
	bool IsStableComboCandidateLess(
		const FComboCondition& Candidate,
		int32 CandidateMoveIndex,
		const FComboCondition& CurrentBest,
		int32 CurrentBestMoveIndex)
	{
		const FString CandidatePath = GetComboConditionStableKey(Candidate);
		const FString CurrentBestPath = GetComboConditionStableKey(CurrentBest);
		if (CandidatePath != CurrentBestPath)
		{
			return CandidatePath < CurrentBestPath;
		}

		if (Candidate.TriggerEvent != CurrentBest.TriggerEvent)
		{
			return static_cast<uint8>(Candidate.TriggerEvent) < static_cast<uint8>(CurrentBest.TriggerEvent);
		}

		if (Candidate.bCheckMoveDirection != CurrentBest.bCheckMoveDirection)
		{
			return Candidate.bCheckMoveDirection;
		}
		if (Candidate.MoveDirectionThreshold.X != CurrentBest.MoveDirectionThreshold.X)
		{
			return Candidate.MoveDirectionThreshold.X < CurrentBest.MoveDirectionThreshold.X;
		}
		if (Candidate.MoveDirectionThreshold.Y != CurrentBest.MoveDirectionThreshold.Y)
		{
			return Candidate.MoveDirectionThreshold.Y < CurrentBest.MoveDirectionThreshold.Y;
		}

		if (Candidate.bCheckHoldDuration != CurrentBest.bCheckHoldDuration)
		{
			return Candidate.bCheckHoldDuration;
		}
		if (Candidate.MinHoldDuration != CurrentBest.MinHoldDuration)
		{
			return Candidate.MinHoldDuration < CurrentBest.MinHoldDuration;
		}

		return CandidateMoveIndex < CurrentBestMoveIndex;
	}
}

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
	// ActionState is the single logical-state source. Clients restore it, then replay unacknowledged commands.
	DOREPLIFETIME(UCombatComponent, ActionState);
	// 命中表现事件走复制属性下发，不再依赖多播 RPC（详见头文件说明）。
	DOREPLIFETIME(UCombatComponent, ReplicatedHitEvents);
	// Only the owning client needs the acknowledgement watermark for prediction reconciliation.
	DOREPLIFETIME_CONDITION(UCombatComponent, LastProcessedInputSequence, COND_OwnerOnly);
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

	// Late join: ActionState may already be valid but OnRep will not fire again.
	if (GetOwner() && !GetOwner()->HasAuthority() && ActionState.bActive)
	{
		bPredictionReconcilePending = true;
	}
}

void UCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindMontageDelegates();
	UpdateMovementLock(false);
	AttackInputPressTimes.Reset();
	PendingInputCommands.Reset();
	ClearBufferedComboInput();
	CurrentChargeInputAction = nullptr;
	bIsCharging = false;
	bChargeInputHeld = false;
	bChargeWindowActive = false;
	Super::EndPlay(EndPlayReason);
}

void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner || !GetWorld())
	{
		return;
	}

	const bool bAuthority = Owner->HasAuthority();

	if (!bAuthority)
	{
		// Autonomous proxy: one reconcile pass per authoritative snapshot.
		ReconcilePrediction();

		// The local grace deadline is based on montage position, not wall-clock time.
		if (ComboWindowState == EMHCombatComboWindowState::Buffered && HasComboWindowExpired())
		{
			SetComboWindowState(EMHCombatComboWindowState::Closed);
		}
	}
	else
	{
		if (bHitWindowActive && ActionState.bActive)
		{
			// The hit window is a continuous server-only state.
			PerformHitSweep();
		}

		if (ComboWindowState == EMHCombatComboWindowState::Buffered && HasComboWindowExpired())
		{
			SetComboWindowState(EMHCombatComboWindowState::Closed);
			CommitActionState(false);
		}

		// Charge state and montage position are incremental data inside an action.
		ActionPositionSyncAccumulator += DeltaTime;
		if (ActionPositionSyncInterval <= 0.f || ActionPositionSyncAccumulator >= ActionPositionSyncInterval)
		{
			ActionPositionSyncAccumulator = 0.f;
			if (ActionState.bActive)
			{
				WriteActionStateFromLogic();
			}
		}
	}

	if (CombatState == EMHCombatState::Attack && CachedAnimInstance && CurrentMoveData.Montage)
	{
		CurrentMoveTime = CachedAnimInstance->Montage_GetPosition(CurrentMoveData.Montage);

		// Sections can advance naturally as well as via Montage_JumpToSection. Mirror the actual
		// section into the logical state so a later reconcile does not jump back to the charge part.
		const FName ActiveSection = CurrentMoveData.Montage->GetSectionName(
			CurrentMoveData.Montage->GetSectionIndexFromPosition(CurrentMoveTime));
		if (!ActiveSection.IsNone())
		{
			PresentedSectionName = ActiveSection;
			if ((bAuthority || (CachedCharacter && CachedCharacter->IsLocallyControlled())) && CurrentSectionName != ActiveSection)
			{
				CurrentSectionName = ActiveSection;
				if (bAuthority)
				{
					CommitActionState(false);
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool UCombatComponent::HandleComboInput(UInputAction* InputAction, ETriggerEvent TriggerEvent)
{
	CacheOwnerReferences();

	if (!bCombatEnabled || !InputAction || !CachedCharacter || !GetOwner() || !GetWorld())
	{
		return false;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	float HoldDuration = 0.f;
	if (TriggerEvent == ETriggerEvent::Started)
	{
		AttackInputPressTimes.Add(InputAction, Now);
	}
	else if (TriggerEvent == ETriggerEvent::Triggered)
	{
		if (const float* PressTime = AttackInputPressTimes.Find(InputAction))
		{
			HoldDuration = Now - *PressTime;
		}
	}
	else if (TriggerEvent == ETriggerEvent::Completed)
	{
		if (const float* PressTime = AttackInputPressTimes.Find(InputAction))
		{
			HoldDuration = Now - *PressTime;
		}
		AttackInputPressTimes.Remove(InputAction);
	}

	FMHCombatInputCommand Command;
	Command.InputAction = InputAction;
	Command.TriggerEvent = TriggerEvent;
	Command.MoveInput = CurrentMoveInput;
	Command.HoldDuration = HoldDuration;
	Command.bAirborne = IsAirborne();
	Command.ClientWorldTime = Now;

	const ENetRole OwnerRole = GetOwner()->GetLocalRole();

	if (OwnerRole == ROLE_Authority)
	{
		// Server-local input from debug or Blueprint does not require prediction acknowledgement.
		Command.Sequence = ++LocalInputSequence;
		if (GetCombatNetLogLevel() >= 2)
		{
			UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] ServerLocalInput Owner=%s Sequence=%d Action=%s Trigger=%d"),
				*CachedCharacter->GetName(), Command.Sequence, *InputAction->GetName(), static_cast<int32>(TriggerEvent));
		}
		return ExecuteCombatCommand(Command, InputAction, false);
	}

	if (OwnerRole == ROLE_AutonomousProxy && CachedCharacter->IsLocallyControlled())
	{
		Command.Sequence = ++LocalInputSequence;

		if (IsPredictionReconcileEnabled())
		{
			PendingInputCommands.Add(Command);
			TrimPendingInputCommands();

			if (GetCombatNetLogLevel() >= 1)
			{
				UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] InputQueued Owner=%s Sequence=%d Action=%s Trigger=%d Hold=%.3f Airborne=%d Pending=%d"),
					*CachedCharacter->GetName(), Command.Sequence, *InputAction->GetName(), static_cast<int32>(TriggerEvent),
					HoldDuration, Command.bAirborne ? 1 : 0, PendingInputCommands.Num());
			}

			Server_HandleComboInput(Command);

			// Predict immediately; the next authoritative snapshot reconciles this command.
			bPredictionReconcilePending = true;
			ReconcilePrediction();
		}
		else
		{
			// Prediction disabled: still send the input, but let the replicated state drive presentation.
			Server_HandleComboInput(Command);
			if (GetCombatNetLogLevel() >= 2)
			{
				UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] InputSentWithoutPrediction Owner=%s Sequence=%d Action=%s Trigger=%d"),
					*CachedCharacter->GetName(), Command.Sequence, *InputAction->GetName(), static_cast<int32>(TriggerEvent));
			}
		}

		return true;
	}

	if (GetCombatNetLogLevel() >= 2)
	{
		UE_LOG(LogMHCombatNet, Verbose, TEXT("[CombatNet] InputIgnoredOwnerRole Owner=%s Role=%d"),
			*CachedCharacter->GetName(), static_cast<int32>(OwnerRole));
	}
	return false;
}

void UCombatComponent::Server_HandleComboInput_Implementation(const FMHCombatInputCommand& Command)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !GetWorld())
	{
		return;
	}

	if (Command.Sequence <= LastProcessedInputSequence)
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] Server dropped stale input Owner=%s Sequence=%d Acked=%d"),
			*Owner->GetName(), Command.Sequence, LastProcessedInputSequence);
		return;
	}

	// Reliable RPCs preserve order. A contiguous sequence prevents a malformed command from
	// advancing the acknowledgement watermark past an input the client still has queued.
	if (Command.Sequence != LastProcessedInputSequence + 1)
	{
		UE_LOG(LogMHCombatNet, Error,
			TEXT("[CombatNet] Server dropped out-of-order input Owner=%s Sequence=%d Expected=%d"),
			*Owner->GetName(), Command.Sequence, LastProcessedInputSequence + 1);
		return;
	}

	// Every command is acknowledged, including commands that cannot open an action.
	LastProcessedInputSequence = Command.Sequence;

	if (!bCombatEnabled)
	{
		return;
	}

	UInputAction* InputAction = Command.ResolveInputAction();
	if (!InputAction)
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] Server received unknown input action Owner=%s Sequence=%d Path=%s"),
			*Owner->GetName(), Command.Sequence, *Command.InputAction.ToSoftObjectPath().ToString());
		return;
	}

	CurrentMoveInput = Command.MoveInput;

	const bool bExecuted = ExecuteCombatCommand(Command, InputAction, /*bReplay=*/false);

	if (GetCombatNetLogLevel() >= 1)
	{
		UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] ServerProcessed Owner=%s Sequence=%d Action=%s Trigger=%d Executed=%d MoveIndex=%d State=%d"),
			*Owner->GetName(), Command.Sequence, *InputAction->GetName(), static_cast<int32>(Command.TriggerEvent),
			bExecuted ? 1 : 0, CurrentMoveIndex, static_cast<int32>(CombatState));
	}
}

void UCombatComponent::Server_RequestEquipWeapon_Implementation(const FSoftObjectPath& WeaponPath)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UWeaponDataAsset* RequestedWeapon = Cast<UWeaponDataAsset>(WeaponPath.TryLoad());
	if (!RequestedWeapon)
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] Rejected unknown weapon path. Owner=%s Path=%s"),
			*GetOwner()->GetName(), *WeaponPath.ToString());
		return;
	}

	ApplyEquipWeapon(RequestedWeapon);
}

bool UCombatComponent::ExecuteCombatCommand(const FMHCombatInputCommand& Command, UInputAction* InputAction, bool bReplay)
{
	if (!bCombatEnabled || !InputAction || !Command.IsValidInput())
	{
		return false;
	}

	if (!GetOwner() || (!bReplay && !GetOwner()->HasAuthority()))
	{
		return false;
	}

	// Hit stun is authoritative. It also blocks local replay so a predicted attack cannot
	// restart over the reaction montage while the cancellation snapshot is in flight.
	if (IsOwnerReacting())
	{
		return false;
	}

	const float EvalTime = Command.ClientWorldTime > 0.f ? Command.ClientWorldTime : ResolveEvalTime();

	if (CombatState == EMHCombatState::Attack)
	{
		// Charge release belongs to the same held input action.
		if (bIsChargeMove
			&& bChargeInputHeld
			&& Command.TriggerEvent == ETriggerEvent::Completed
			&& !CurrentChargeInputAction.IsNull()
			&& Command.InputAction == CurrentChargeInputAction)
		{
			return ReleaseChargeLogic(InputAction);
		}

		return BufferNextCombo(Command, InputAction, EvalTime);
	}

	if (CombatState == EMHCombatState::Locomotion || CombatState == EMHCombatState::WeaponSwitch)
	{
		return TryStartAttack(Command, InputAction, bReplay);
	}

	return false;
}

bool UCombatComponent::TryStartAttack(const FMHCombatInputCommand& Input, UInputAction* InputAction, bool bReplay)
{
	if (GetCombatNetLogLevel() >= 2)
	{
		UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] TryStartAttack Owner=%s Action=%s Trigger=%d Airborne=%d"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"), *InputAction->GetName(),
			static_cast<int32>(Input.TriggerEvent), Input.bAirborne ? 1 : 0);
	}

	if (!CurrentWeapon)
	{
		return false;
	}

	// Startup selection uses the airborne flag captured at the input edge, so replay is deterministic.
	const TMap<FComboCondition, int32>& StartMoves = Input.bAirborne
		? CurrentWeapon->AirStartMoves
		: CurrentWeapon->GroundStartMoves;

	const int32 MoveIndex = FindBestComboIndex(StartMoves, Input, InputAction);
	if (MoveIndex == INDEX_NONE)
	{
		return false;
	}

	const FMHCombatMoveData* Move = GetMove(MoveIndex);
	if (!Move)
	{
		return false;
	}

	return StartMove(*Move, MoveIndex, &Input, Input.Sequence, bReplay);
}

bool UCombatComponent::BufferNextCombo(const FMHCombatInputCommand& Input, UInputAction* InputAction, float EvalTime)
{
	if (!CurrentMoveData.bCanChain)
	{
		return false;
	}

	const int32 NextMoveIndex = FindBestComboIndex(CurrentMoveData.ComboChain, Input, InputAction);
	if (NextMoveIndex == INDEX_NONE)
	{
		return false;
	}

	if (!IsComboInputAllowed(EvalTime))
	{
		return false;
	}

	// Keep only the latest matching buffered input, matching the original behaviour.
	BufferedComboInput = Input;
	bHasBufferedComboInput = true;

	TryStartNextCombo(EvalTime, /*bReplay=*/!GetOwner()->HasAuthority());

	// If the window is not open yet, the buffered command must be included in the authoritative
	// snapshot before its sequence is acknowledged. Otherwise a client can briefly roll back to
	// the pre-buffer state while waiting for the next periodic action-state update.
	if (GetOwner()->HasAuthority())
	{
		CommitActionState(false);
	}
	return true;
}

bool UCombatComponent::TryStartNextCombo(float EvalTime, bool bReplay)
{
	if (!bHasBufferedComboInput || !CurrentMoveData.bCanChain)
	{
		return false;
	}

	const float Time = EvalTime >= 0.f ? EvalTime : ResolveEvalTime();
	if (!CanStartBufferedCombo(Time))
	{
		return false;
	}

	const FMHCombatInputCommand Command = BufferedComboInput;
	UInputAction* InputAction = Command.ResolveInputAction();
	const int32 NextMoveIndex = FindBestComboIndex(CurrentMoveData.ComboChain, Command, InputAction);
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

	ClearBufferedComboInput();
	return StartMove(*NextMove, NextMoveIndex, &Command, Command.Sequence, bReplay);
}

bool UCombatComponent::ReleaseChargeLogic(UInputAction* InputAction)
{
	if (!bIsChargeMove)
	{
		return false;
	}

	bChargeInputHeld = false;
	bIsCharging = false;
	bChargeWindowActive = false;

	// A charge montage usually contains separate Charge and Attack sections. Releasing before the
	// AttackStart notify fast-forwards to the configured attack section and lets it play normally.
	const FName ConfiguredAttackSection = CurrentMoveData.AttackSectionName;
	if (!ConfiguredAttackSection.IsNone() && ConfiguredAttackSection != CurrentSectionName)
	{
		if (CurrentMoveData.Montage && CurrentMoveData.Montage->IsValidSectionName(ConfiguredAttackSection))
		{
			CurrentSectionName = ConfiguredAttackSection;
			CurrentMoveTime = GetMontageSectionStartPosition(CurrentMoveData.Montage, ConfiguredAttackSection);
		}
		else
		{
			UE_LOG(LogMHCombatNet, Warning,
				TEXT("[CombatNet] Charge release section is missing. Owner=%s Montage=%s Section=%s"),
				CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"),
				CurrentMoveData.Montage ? *CurrentMoveData.Montage->GetName() : TEXT("null"),
				*ConfiguredAttackSection.ToString());
		}
	}

	if (GetCombatNetLogLevel() >= 1)
	{
		UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] Charge released Owner=%s Action=%s Move=%s Section=%s"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"),
			InputAction ? *InputAction->GetName() : TEXT("null"),
			*CurrentMoveData.MoveId.ToString(), *CurrentSectionName.ToString());
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		CommitActionState(false);
	}

	return true;
}

bool UCombatComponent::StartMove(const FMHCombatMoveData& Move, int32 MoveIndex, const FMHCombatInputCommand* SourceInput, int32 InputSequence, bool bReplay)
{
	if (!Move.Montage)
	{
		return false;
	}

	if (!bReplay && (!GetOwner() || !GetOwner()->HasAuthority()))
	{
		return false;
	}

	if (IsOwnerReacting())
	{
		return false;
	}

	CacheOwnerReferences();
	if (!CachedCharacter)
	{
		return false;
	}

	if (GetCombatNetLogLevel() >= 1)
	{
		UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] StartMove Owner=%s Replay=%d Authority=%d MoveId=%s MoveIndex=%d Sequence=%d"),
			*CachedCharacter->GetName(), bReplay ? 1 : 0, GetOwner()->HasAuthority() ? 1 : 0,
			*Move.MoveId.ToString(), MoveIndex, InputSequence);
	}

	CurrentMoveData = Move;
	CurrentMoveIndex = MoveIndex;
	CurrentMoveId = Move.MoveId;
	CurrentSectionName = Move.SectionName;
	CurrentMoveTime = 0.f;
	ClearBufferedComboInput();

	bHitExecuted = false;
	bHitWindowActive = false;
	bHasPreviousHitOrigin = false;
	bWeaponSwitchAllowed = false;
	HitActorsThisMove.Reset();

	SetComboWindowState(EMHCombatComboWindowState::Pending);

	// Charge context only exists when the action starts from a Started input edge.
	bIsChargeMove = Move.bIsChargeMove;
	bChargeWindowActive = false;
	bIsCharging = false;
	const bool bHasHeldSource = SourceInput && SourceInput->TriggerEvent == ETriggerEvent::Started;
	bChargeInputHeld = Move.bIsChargeMove && bHasHeldSource;
	CurrentChargeInputAction = bChargeInputHeld ? SourceInput->InputAction : TSoftObjectPtr<UInputAction>();

	if (!bReplay && bLockGroundMovementDuringAttack && !IsAirborne())
	{
		UpdateMovementLock(true);
	}

	const EMHCombatMovePhase InitialPhase = Move.bIsChargeMove
		? EMHCombatMovePhase::Charge
		: EMHCombatMovePhase::Startup;
	SetCombatState(EMHCombatState::Attack, InitialPhase);

	if (bReplay)
	{
		// Replayed actions start on the local timeline now; presentation alignment decides the actual position.
		PredictedMoveStartTime = ResolveEvalTime();
		return true;
	}

	ActionState.InputSequence = InputSequence;
	CommitActionState(true);
	return true;
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
	SetComboWindowState(EMHCombatComboWindowState::Closed);
	ClearBufferedComboInput();

	bHitExecuted = false;
	bHitWindowActive = false;
	bHasPreviousHitOrigin = false;
	bWeaponSwitchAllowed = false;
	HitActorsThisMove.Reset();

	bIsCharging = false;
	bChargeInputHeld = false;
	bChargeWindowActive = false;
	CurrentChargeInputAction = nullptr;

	ActionState.bInterrupted = bInterrupted;
	// Action end is an action boundary, so the client can reliably finish presentation.
	CommitActionState(true);
}

void UCombatComponent::ClearBufferedComboInput()
{
	bHasBufferedComboInput = false;
	BufferedComboInput = FMHCombatInputCommand();
}

// ---------------------------------------------------------------------------
// State write / restore
// ---------------------------------------------------------------------------

void UCombatComponent::WriteActionStateFromLogic()
{
	ActionState.bActive = (CombatState == EMHCombatState::Attack);
	ActionState.MoveIndex = ActionState.bActive ? CurrentMoveIndex : INDEX_NONE;
	ActionState.SectionName = ActionState.bActive ? CurrentSectionName : NAME_None;
	ActionState.PlayRate = CurrentMoveData.MontagePlayRate;
	ActionState.bCharging = bIsCharging;
	ActionState.bChargeWindowActive = bChargeWindowActive;
	ActionState.bChargeInputHeld = bChargeInputHeld;
	ActionState.ChargeInputAction = CurrentChargeInputAction;
	ActionState.CombatState = CombatState;
	ActionState.MovePhase = MovePhase;
	ActionState.ComboWindowState = ActionState.bActive ? ComboWindowState : EMHCombatComboWindowState::Closed;
	ActionState.ComboWindowClosePosition = (ComboWindowState == EMHCombatComboWindowState::Buffered) ? ComboWindowClosePosition : 0.f;
	ActionState.bWeaponSwitchAllowed = ActionState.bActive && bWeaponSwitchAllowed;
	ActionState.bHasBufferedInput = bHasBufferedComboInput;
	ActionState.BufferedInput = bHasBufferedComboInput ? BufferedComboInput : FMHCombatInputCommand();
	ActionState.MontagePosition = ActionState.bActive ? CurrentMoveTime : 0.f;
}

void UCombatComponent::CommitActionState(bool bActionBoundary)
{
	if (bActionBoundary)
	{
		++ActionState.Sequence;
		ActionPositionSyncAccumulator = 0.f;
	}

	WriteActionStateFromLogic();

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	AppliedActionSequence = ActionState.Sequence;

	if (bActionBoundary)
	{
		ApplyActionState();
	}
	else
	{
		SyncActionPresentation();
	}
}

void UCombatComponent::ApplyReplicatedLogicState()
{
	if (!GetWorld())
	{
		return;
	}

	SetCombatState(ActionState.CombatState, ActionState.MovePhase);

	if (!ActionState.bActive)
	{
		CurrentMoveData = FMHCombatMoveData();
		CurrentMoveIndex = INDEX_NONE;
		CurrentMoveId = NAME_None;
		CurrentSectionName = NAME_None;
		CurrentMoveTime = 0.f;
		bIsChargeMove = false;
		bIsCharging = false;
		bChargeInputHeld = false;
		bChargeWindowActive = false;
		CurrentChargeInputAction = nullptr;
		SetComboWindowState(EMHCombatComboWindowState::Closed);
		bWeaponSwitchAllowed = false;
		ClearBufferedComboInput();
		PredictedMoveStartTime = -1.f;
		return;
	}

	if (const FMHCombatMoveData* Move = GetMove(ActionState.MoveIndex))
	{
		CurrentMoveData = *Move;
		CurrentMoveIndex = ActionState.MoveIndex;
		CurrentMoveId = Move->MoveId;
		bIsChargeMove = Move->bIsChargeMove;
	}
	else
	{
		CurrentMoveData = FMHCombatMoveData();
		CurrentMoveIndex = ActionState.MoveIndex;
		CurrentMoveId = NAME_None;
		bIsChargeMove = false;
	}

	CurrentSectionName = ActionState.SectionName;
	CurrentMoveTime = ActionState.MontagePosition;
	bChargeInputHeld = ActionState.bChargeInputHeld;
	CurrentChargeInputAction = ActionState.ChargeInputAction;
	bWeaponSwitchAllowed = ActionState.bWeaponSwitchAllowed;

	// The authoritative snapshot is the base state for replay. Local notify callbacks may
	// refresh presentation, but they never become a second game-logic authority.
	bChargeWindowActive = ActionState.bChargeWindowActive;
	bIsCharging = bChargeWindowActive && bChargeInputHeld;

	SetComboWindowState(ActionState.ComboWindowState);
	ComboWindowClosePosition = ActionState.ComboWindowClosePosition;

	bHasBufferedComboInput = ActionState.bHasBufferedInput;
	BufferedComboInput = ActionState.BufferedInput;

	// Reconstruct the local start time from the authoritative position so rollback never restarts a move visually.
	const float Rate = FMath::Max(CurrentMoveData.MontagePlayRate, 0.01f);
	PredictedMoveStartTime = GetWorld()->GetTimeSeconds() - ActionState.MontagePosition / Rate;
}

void UCombatComponent::ReconcilePrediction()
{
	if (!bPredictionReconcilePending)
	{
		return;
	}

	bPredictionReconcilePending = false;

	if (!IsPredictionReconcileEnabled())
	{
		return;
	}

	CacheOwnerReferences();
	if (!GetWorld())
	{
		return;
	}

	const int32 AckedSequence = FMath::Max(LastProcessedInputSequence, 0);
	const int32 PreviousMoveIndex = CurrentMoveIndex;
	const bool bWasAttacking = (CombatState == EMHCombatState::Attack);

	// 1) Drop commands the server has already acknowledged.
	DiscardAcknowledgedInputs(AckedSequence);

	// 2) Restore the authoritative logical snapshot.
	ApplyReplicatedLogicState();

	// 3) Replay every still-unacknowledged command in order.
	int32 ReplayedCount = 0;
	for (const FMHCombatInputCommand& Command : PendingInputCommands)
	{
		if (Command.Sequence <= AckedSequence)
		{
			continue;
		}

		UInputAction* InputAction = Command.ResolveInputAction();
		if (!InputAction)
		{
			continue;
		}

		ExecuteCombatCommand(Command, InputAction, /*bReplay=*/true);
		++ReplayedCount;
	}
	LastReconcileReplayedCount = ReplayedCount;

	const bool bCorrected = (PreviousMoveIndex != CurrentMoveIndex)
		|| (bWasAttacking != (CombatState == EMHCombatState::Attack));

	// 4) Align presentation once after replay so rollback does not spam montage restarts.
	AppliedActionSequence = ActionState.Sequence;
	AlignPresentationToLogicState();

	if (GetCombatNetLogLevel() >= 1)
	{
		UE_LOG(LogMHCombatNet, Log,
			TEXT("[CombatNet] PredictionReconciled Owner=%s Acked=%d Replayed=%d Pending=%d MoveIndex=%d State=%d Corrected=%d"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"),
			AckedSequence, ReplayedCount, PendingInputCommands.Num(), CurrentMoveIndex,
			static_cast<int32>(CombatState), bCorrected ? 1 : 0);
	}

	OnPredictionReconciled.Broadcast(AckedSequence, ReplayedCount, bCorrected);
}

void UCombatComponent::DiscardAcknowledgedInputs(int32 AckedSequence)
{
	if (AckedSequence <= LastReconciledInputSequence)
	{
		return;
	}

	LastReconciledInputSequence = AckedSequence;
	PendingInputCommands.RemoveAll([AckedSequence](const FMHCombatInputCommand& Command)
	{
		return Command.Sequence <= AckedSequence;
	});
}

void UCombatComponent::TrimPendingInputCommands()
{
	UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	if (MaxPredictedInputHistory > 0 && PendingInputCommands.Num() > MaxPredictedInputHistory)
	{
		PendingInputCommands.RemoveAt(0, PendingInputCommands.Num() - MaxPredictedInputHistory);
	}

	if (PredictedInputHistorySeconds <= 0.f)
	{
		return;
	}

	int32 ExpiredCount = 0;
	for (const FMHCombatInputCommand& Command : PendingInputCommands)
	{
		if (Now - Command.ClientWorldTime <= PredictedInputHistorySeconds)
		{
			break;
		}
		++ExpiredCount;
	}

	if (ExpiredCount > 0)
	{
		PendingInputCommands.RemoveAt(0, ExpiredCount);
	}
}

bool UCombatComponent::IsPredictionReconcileEnabled() const
{
	if (CVarMHCombatPredictionEnabled.GetValueOnAnyThread() == 0)
	{
		return false;
	}

	if (!bEnablePrediction)
	{
		return false;
	}

	const AActor* Owner = GetOwner();
	if (!Owner || Owner->HasAuthority())
	{
		return false;
	}

	const APawn* Pawn = Cast<APawn>(Owner);
	return Owner->GetLocalRole() == ROLE_AutonomousProxy && Pawn && Pawn->IsLocallyControlled();
}

int32 UCombatComponent::GetPendingInputCount() const
{
	return PendingInputCommands.Num();
}

bool UCombatComponent::IsOwnerReacting() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	const UHitReactionComponent* HitReaction = Owner->FindComponentByClass<UHitReactionComponent>();
	return HitReaction && HitReaction->IsReacting();
}

// ---------------------------------------------------------------------------
// Replication callbacks
// ---------------------------------------------------------------------------

void UCombatComponent::OnRep_LastProcessedInputSequence()
{
	bPredictionReconcilePending = true;
}

void UCombatComponent::OnRep_ActionState()
{
	CacheOwnerReferences();
	bPredictionReconcilePending = true;

	if (IsPredictionReconcileEnabled())
	{
		// Autonomous proxy: Tick performs restore + replay + presentation alignment once.
		return;
	}

	ApplyReplicatedLogicState();

	const bool bActionBoundary = (AppliedActionSequence != ActionState.Sequence);
	AppliedActionSequence = ActionState.Sequence;

	if (bActionBoundary)
	{
		ApplyActionState();
	}
	else
	{
		SyncActionPresentation();
	}
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

	// ActionState and CurrentWeaponPath can arrive in either order. Resolve the move again now
	// that the weapon is available, otherwise a late joiner can miss the montage completely.
	if (!ActionState.bActive)
	{
		return;
	}

	if (IsPredictionReconcileEnabled())
	{
		bPredictionReconcilePending = true;
		return;
	}

	ApplyReplicatedLogicState();
	ApplyActionState();
}

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

void UCombatComponent::ApplyActionState()
{
	CacheOwnerReferences();
	AppliedActionSequence = ActionState.Sequence;

	if (IsOwnerReacting())
	{
		ClearPresentationState(true);
		return;
	}

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
	SyncActionPresentation();
}

void UCombatComponent::SyncActionPresentation()
{
	if (!ActionState.bActive)
	{
		return;
	}

	if (IsOwnerReacting())
	{
		return;
	}

	// Incremental refresh: never restart the montage when it is already presenting the same move.
	if (!PresentedMontage || !CachedAnimInstance || !CachedAnimInstance->Montage_IsPlaying(PresentedMontage))
	{
		ApplyActionState();
		return;
	}

	const FName TargetSection = CurrentSectionName.IsNone() ? CurrentMoveData.SectionName : CurrentSectionName;
	if (!TargetSection.IsNone() && TargetSection != PresentedSectionName)
	{
		CachedAnimInstance->Montage_JumpToSection(TargetSection, PresentedMontage);
		PresentedSectionName = TargetSection;
		CurrentMoveTime = CachedAnimInstance->Montage_GetPosition(PresentedMontage);
	}

	ApplyPresentationPlayRate();
}

void UCombatComponent::AlignPresentationToLogicState()
{
	CacheOwnerReferences();
	if (!GetWorld())
	{
		return;
	}

	if (IsOwnerReacting())
	{
		if (bPresentationActive)
		{
			ClearPresentationState(true);
		}
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	if (CombatState != EMHCombatState::Attack || CurrentMoveIndex == INDEX_NONE || !CurrentMoveData.Montage)
	{
		if (bPresentationActive)
		{
			ClearPresentationState(false);
		}
		return;
	}

	const bool bSameMove = bPresentationActive
		&& PresentedMoveIndex == CurrentMoveIndex
		&& PresentedMontage == CurrentMoveData.Montage
		&& CachedAnimInstance
		&& CachedAnimInstance->Montage_IsPlaying(CurrentMoveData.Montage);

	if (!bSameMove)
	{
		// Recover without restarting from zero after every correction.
		const float Rate = FMath::Max(CurrentMoveData.MontagePlayRate, 0.01f);
		const float StartPosition = PredictedMoveStartTime > 0.f
			? FMath::Max(0.f, (Now - PredictedMoveStartTime) * Rate)
			: 0.f;

		PlayMovePresentation(CurrentWeapon, CurrentMoveIndex, CurrentSectionName, CurrentMoveData.MontagePlayRate, StartPosition);
		return;
	}

	const FName TargetSection = CurrentSectionName.IsNone() ? CurrentMoveData.SectionName : CurrentSectionName;
	if (!TargetSection.IsNone() && TargetSection != PresentedSectionName)
	{
		CachedAnimInstance->Montage_JumpToSection(TargetSection, PresentedMontage);
		PresentedSectionName = TargetSection;
		CurrentMoveTime = CachedAnimInstance->Montage_GetPosition(PresentedMontage);
	}

	// Charge rate is driven by the locally replayed state.
	bIsCharging = bChargeWindowActive && bChargeInputHeld;
	ApplyPresentationPlayRate();

	// Only correct position when every local command has been acknowledged.
	if (PendingInputCommands.Num() == 0
		&& MontagePositionCorrectionThreshold > 0.f
		&& ActionState.bActive
		&& ActionState.MoveIndex == CurrentMoveIndex)
	{
		const float CurrentPosition = CachedAnimInstance->Montage_GetPosition(PresentedMontage);
		const float ServerPosition = ActionState.MontagePosition;
		if (FMath::Abs(CurrentPosition - ServerPosition) > MontagePositionCorrectionThreshold)
		{
			CachedAnimInstance->Montage_SetPosition(PresentedMontage, ServerPosition);
			CurrentMoveTime = ServerPosition;

			if (GetCombatNetLogLevel() >= 2)
			{
				UE_LOG(LogMHCombatNet, Log, TEXT("[CombatNet] PresentationPositionCorrected Owner=%s Local=%.3f Server=%.3f"),
					CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"), CurrentPosition, ServerPosition);
			}
		}
	}
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
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] PlayMovePresentation aborted: montage is null. Owner=%s MoveIndex=%d"),
			*CachedCharacter->GetName(), MoveIndex);
		return;
	}

	if (PresentedMontage && PresentedMontage != Move.Montage)
	{
		CachedCharacter->StopAnimMontage(PresentedMontage);
	}

	CurrentMoveData = Move;
	CurrentMoveIndex = MoveIndex;
	CurrentMoveId = Move.MoveId;
	CurrentMoveTime = StartPosition;

	BindMontageDelegates();

	const FName PlaySection = SectionName.IsNone() ? Move.SectionName : SectionName;
	const float PlayLength = CachedCharacter->PlayAnimMontage(Move.Montage, FMath::Max(PlayRate, 0.01f), PlaySection);

	if (GetCombatNetLogLevel() >= 2)
	{
		UE_LOG(LogMHCombatNet, Log,
			TEXT("[CombatNet] PlayMovePresentation Owner=%s NetMode=%d Authority=%d MoveIndex=%d Section=%s Montage=%s PlayRate=%.3f Start=%.3f PlayLength=%.3f"),
			*CachedCharacter->GetName(),
			static_cast<int32>(GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone),
			GetOwner() && GetOwner()->HasAuthority() ? 1 : 0,
			MoveIndex,
			*PlaySection.ToString(),
			*Move.Montage->GetName(),
			PlayRate,
			StartPosition,
			PlayLength);
	}

	if (PlayLength <= 0.f)
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] PlayAnimMontage failed. Owner=%s Montage=%s"),
			*CachedCharacter->GetName(), *Move.Montage->GetName());
		return;
	}

	PresentedMontage = Move.Montage;
	PresentedMoveIndex = MoveIndex;
	PresentedSectionName = PlaySection;

	// Late join / relevance recovery: align directly to the replicated position.
	if (StartPosition > 0.f && CachedAnimInstance)
	{
		const float TargetPosition = FMath::Clamp(StartPosition, 0.f, PlayLength);
		CachedAnimInstance->Montage_SetPosition(Move.Montage, TargetPosition);
		CurrentMoveTime = TargetPosition;
	}

	bIsCharging = bChargeWindowActive && bChargeInputHeld;
	ApplyPresentationPlayRate();

	bPresentationActive = true;
	OnAttackStarted.Broadcast(Move.MoveId);
}

void UCombatComponent::ApplyPresentationPlayRate()
{
	if (!CachedAnimInstance || !PresentedMontage)
	{
		return;
	}

	if (!CachedAnimInstance->Montage_IsPlaying(PresentedMontage))
	{
		return;
	}

	const float BaseRate = FMath::Max(CurrentMoveData.MontagePlayRate, 0.01f);
	const float EffectiveRate = bIsCharging
		? BaseRate * FMath::Clamp(ChargePlayRateScale, 0.05f, 1.f)
		: BaseRate;

	CachedAnimInstance->Montage_SetPlayRate(PresentedMontage, EffectiveRate);
}

void UCombatComponent::ClearPresentationState(bool bInterrupted)
{
	CacheOwnerReferences();

	if (CachedCharacter && PresentedMontage)
	{
		CachedCharacter->StopAnimMontage(PresentedMontage);
	}

	const bool bWasPresenting = bPresentationActive;

	PresentedMontage = nullptr;
	PresentedMoveIndex = INDEX_NONE;
	PresentedSectionName = NAME_None;
	PredictedMoveStartTime = -1.f;
	bPresentationActive = false;

	CurrentMoveData = FMHCombatMoveData();
	CurrentMoveId = NAME_None;
	CurrentMoveIndex = INDEX_NONE;
	CurrentSectionName = NAME_None;
	CurrentMoveTime = 0.f;
	CurrentChargeInputAction = nullptr;
	bIsChargeMove = false;
	bIsCharging = false;
	bChargeInputHeld = false;
	bChargeWindowActive = false;
	bHitWindowActive = false;
	bHasPreviousHitOrigin = false;
	SetComboWindowState(EMHCombatComboWindowState::Closed);
	ClearBufferedComboInput();

	if (bWasPresenting)
	{
		OnAttackEnded.Broadcast(bInterrupted);
	}
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

// ---------------------------------------------------------------------------
// Combo window and condition matching
// ---------------------------------------------------------------------------

float UCombatComponent::ResolveEvalTime() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

float UCombatComponent::ResolveMovePosition() const
{
	if (CachedAnimInstance && PresentedMontage)
	{
		return CachedAnimInstance->Montage_GetPosition(PresentedMontage);
	}

	if (GetOwner() && !GetOwner()->HasAuthority() && ActionState.bActive)
	{
		return ActionState.MontagePosition;
	}

	return CurrentMoveTime;
}

void UCombatComponent::SetComboWindowState(EMHCombatComboWindowState NewState)
{
	if (NewState == EMHCombatComboWindowState::Buffered && ComboWindowState != EMHCombatComboWindowState::Buffered)
	{
		ComboWindowClosePosition = CurrentMoveTime;
	}

	ComboWindowState = NewState;
	bComboWindowOpen = (NewState == EMHCombatComboWindowState::Open);
}

bool UCombatComponent::HasComboWindowExpired() const
{
	if (ComboWindowState != EMHCombatComboWindowState::Buffered)
	{
		return false;
	}

	return ResolveMovePosition() - ComboWindowClosePosition > AttackInputBufferDuration;
}

bool UCombatComponent::IsComboInputAllowed(float EvalTime) const
{
	return ComboWindowState == EMHCombatComboWindowState::Pending
		|| ComboWindowState == EMHCombatComboWindowState::Open
		|| (ComboWindowState == EMHCombatComboWindowState::Buffered && !HasComboWindowExpired());
}

bool UCombatComponent::CanStartBufferedCombo(float EvalTime) const
{
	return ComboWindowState == EMHCombatComboWindowState::Open
		|| (ComboWindowState == EMHCombatComboWindowState::Buffered && !HasComboWindowExpired());
}

int32 UCombatComponent::FindBestComboIndex(const TMap<FComboCondition, int32>& ComboMoves, const FMHCombatInputCommand& Input, UInputAction* InputAction) const
{
	int32 BestIndex = INDEX_NONE;
	int32 BestPriority = MIN_int32;
	float BestScore = -1.f;
	FComboCondition BestCondition;

	for (const TPair<FComboCondition, int32>& Pair : ComboMoves)
	{
		const FComboCondition& Condition = Pair.Key;
		if (!MatchesComboCondition(Condition, Input, InputAction))
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

		bool bBetter = (BestIndex == INDEX_NONE);
		if (!bBetter && Condition.Priority != BestPriority)
		{
			bBetter = Condition.Priority > BestPriority;
		}
		else if (!bBetter && ConditionScore > BestScore + KINDA_SMALL_NUMBER)
		{
			bBetter = true;
		}
		else if (!bBetter && FMath::IsNearlyEqual(ConditionScore, BestScore, KINDA_SMALL_NUMBER))
		{
			bBetter = IsStableComboCandidateLess(Condition, Pair.Value, BestCondition, BestIndex);
		}

		if (bBetter)
		{
			BestPriority = Condition.Priority;
			BestScore = ConditionScore;
			BestCondition = Condition;
			BestIndex = Pair.Value;
		}
	}

	return BestIndex;
}

bool UCombatComponent::MatchesComboCondition(const FComboCondition& Condition, const FMHCombatInputCommand& Input, UInputAction* InputAction) const
{
	if (!Condition.InputAction || !InputAction || Condition.TriggerEvent == ETriggerEvent::None)
	{
		return false;
	}

	if (Condition.InputAction.Get() != InputAction || Condition.TriggerEvent != Input.TriggerEvent)
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

const FMHCombatMoveData* UCombatComponent::GetMove(int32 MoveIndex) const
{
	if (!CurrentWeapon || !CurrentWeapon->Moves.IsValidIndex(MoveIndex))
	{
		return nullptr;
	}

	return &CurrentWeapon->Moves[MoveIndex];
}

// ---------------------------------------------------------------------------
// Animation notifies
// ---------------------------------------------------------------------------

void UCombatComponent::HandleCombatNotify(EMHCombatNotifyType NotifyType, UAnimMontage* SourceMontage)
{
	const bool bAuthority = GetOwner() && GetOwner()->HasAuthority();
	const bool bPredictLocalNotifies = !bAuthority && IsPredictionReconcileEnabled();
	if (!bAuthority && !bPredictLocalNotifies)
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
		// Damage is server-authoritative and never resolved from a client prediction.
		if (bAuthority && MovePhase == EMHCombatMovePhase::Active)
		{
			PerformHitCheck();
		}
		break;
	case EMHCombatNotifyType::RecoveryStart:
		SetCombatState(CombatState, EMHCombatMovePhase::Recovery);
		if (bAuthority)
		{
			CommitActionState(false);
		}
		break;
	case EMHCombatNotifyType::MoveEnd:
		{
			if (!TryStartNextCombo(-1.f, /*bReplay=*/!bAuthority) && bAuthority)
			{
				FinishCurrentMove(false);
			}
		}
		break;
	}
}

void UCombatComponent::HandleCombatNotifyState(EMHCombatNotifyStateType StateType, EMHCombatNotifyStateEvent StateEvent, UAnimMontage* SourceMontage)
{
	if (!IsCurrentMontage(SourceMontage) || CombatState != EMHCombatState::Attack)
	{
		return;
	}

	const bool bAuthority = GetOwner() && GetOwner()->HasAuthority();

	// Charge and combo windows drive local presentation/input prediction; the server still
	// writes their authoritative values into ActionState. Attack hit windows stay server-only.
	if (StateType == EMHCombatNotifyStateType::ChargeWindow)
	{
		if (StateEvent == EMHCombatNotifyStateEvent::Begin)
		{
			bChargeWindowActive = true;
		}
		else if (StateEvent == EMHCombatNotifyStateEvent::End)
		{
			bChargeWindowActive = false;
		}

		const bool bWasCharging = bIsCharging;
		bIsCharging = bChargeWindowActive && bChargeInputHeld;

		if (bAuthority)
		{
			if (bWasCharging != bIsCharging)
			{
				CommitActionState(false);
			}
		}
		else
		{
			ApplyPresentationPlayRate();
		}
		return;
	}

	// Weapon switch windows are needed by the autonomous proxy for local gating and by
	// simulated proxies for animation state. Only the server writes the replicated state.
	if (StateType == EMHCombatNotifyStateType::WeaponSwitchAllowed)
	{
		bWeaponSwitchAllowed = (StateEvent == EMHCombatNotifyStateEvent::Begin);
		if (bAuthority)
		{
			CommitActionState(false);
		}
		return;
	}

	// Combo windows drive input acceptance, so the autonomous proxy predicts them from its
	// local montage. The next authoritative snapshot still corrects any divergence.
	if (StateType == EMHCombatNotifyStateType::ComboWindow)
	{
		if (!bAuthority && !IsPredictionReconcileEnabled())
		{
			return;
		}

		if (StateEvent == EMHCombatNotifyStateEvent::Begin)
		{
			SetComboWindowState(EMHCombatComboWindowState::Open);
		}
		else if (StateEvent == EMHCombatNotifyStateEvent::End)
		{
			SetComboWindowState(EMHCombatComboWindowState::Buffered);
		}

		if (bAuthority)
		{
			CommitActionState(false);
		}

		TryStartNextCombo(-1.f, /*bReplay=*/!bAuthority);
		return;
	}

	if (!bAuthority)
	{
		return;
	}

	switch (StateType)
	{
	case EMHCombatNotifyStateType::AttackHitWindow:
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

	default:
		break;
	}
}

void UCombatComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsCurrentMontage(Montage))
	{
		return;
	}

	TryStartNextCombo();
}

void UCombatComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsCurrentMontage(Montage))
	{
		return;
	}

	if (!TryStartNextCombo())
	{
		FinishCurrentMove(bInterrupted);
	}
}

// ---------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------

bool UCombatComponent::CycleWeapon(int32 Delta)
{
	if (LoadoutWeapons.IsEmpty())
	{
		return DefaultWeapon ? EquipWeapon(DefaultWeapon) : false;
	}

	int32 Index = CurrentWeaponIndex;
	if (Index == INDEX_NONE && CurrentWeapon)
	{
		Index = LoadoutWeapons.IndexOfByPredicate([this](const TObjectPtr<UWeaponDataAsset>& Weapon)
		{
			return Weapon == CurrentWeapon;
		});
	}

	if (Index == INDEX_NONE)
	{
		Index = 0;
	}

	const int32 WeaponCount = LoadoutWeapons.Num();
	const int32 Direction = Delta >= 0 ? 1 : -1;
	const int32 NextIndex = ((Index + FMath::Abs(Delta) * Direction) % WeaponCount + WeaponCount) % WeaponCount;
	return EquipWeapon(LoadoutWeapons[NextIndex]);
}

bool UCombatComponent::EquipWeapon(UWeaponDataAsset* NewWeapon)
{
	if (!NewWeapon || !bCombatEnabled || !GetOwner())
	{
		return false;
	}

	// Client calls are requests. They do not mutate replicated weapon state locally, because
	// the server remains the only writer for CurrentWeaponPath.
	if (!GetOwner()->HasAuthority())
	{
		const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		if (GetOwner()->GetLocalRole() != ROLE_AutonomousProxy || !OwnerPawn || !OwnerPawn->IsLocallyControlled())
		{
			return false;
		}

		Server_RequestEquipWeapon(FSoftObjectPath(NewWeapon));
		return true;
	}

	return ApplyEquipWeapon(NewWeapon);
}

bool UCombatComponent::ApplyEquipWeapon(UWeaponDataAsset* NewWeapon)
{
	if (!NewWeapon || !bCombatEnabled || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	// The server only accepts weapons that belong to this character's loadout.
	const bool bInLoadout = LoadoutWeapons.ContainsByPredicate([NewWeapon](const TObjectPtr<UWeaponDataAsset>& Weapon)
	{
		return Weapon == NewWeapon;
	});
	if (!bInLoadout && NewWeapon != DefaultWeapon)
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] Rejected weapon outside loadout. Owner=%s Weapon=%s"),
			*GetOwner()->GetName(), *NewWeapon->GetName());
		return false;
	}

	if (CurrentWeapon == NewWeapon)
	{
		return true;
	}

	if (!CanSwitchWeaponNow())
	{
		UE_LOG(LogMHCombatNet, Warning, TEXT("[CombatNet] Weapon switch rejected: current action does not allow it."));
		return false;
	}

	if (CombatState == EMHCombatState::Attack)
	{
		FinishCurrentMove(false);
	}

	CurrentWeapon = NewWeapon;
	CurrentWeaponPath = FSoftObjectPath(NewWeapon);
	CurrentWeaponIndex = LoadoutWeapons.IndexOfByPredicate([NewWeapon](const TObjectPtr<UWeaponDataAsset>& Weapon)
	{
		return Weapon == NewWeapon;
	});

	UpdateWeaponMesh(NewWeapon);
	CurrentMoveIndex = INDEX_NONE;
	CurrentMoveId = NAME_None;
	OnWeaponChanged.Broadcast(CurrentWeapon);
	return true;
}

bool UCombatComponent::EquipWeapon_Default()
{
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

	PendingInputCommands.Reset();
	ClearBufferedComboInput();

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
	if (!Weapon || CurrentWeapon == Weapon)
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

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

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

float UCombatComponent::GetCurrentMoveLength() const
{
	if (!CurrentMoveData.Montage)
	{
		return 0.f;
	}

	if (!CurrentSectionName.IsNone())
	{
		const int32 SectionIndex = CurrentMoveData.Montage->GetSectionIndex(CurrentSectionName);
		if (CurrentMoveData.Montage->IsValidSectionIndex(SectionIndex))
		{
			return CurrentMoveData.Montage->GetSectionLength(SectionIndex) / FMath::Max(CurrentMoveData.MontagePlayRate, 0.01f);
		}
	}

	return CurrentMoveData.Montage->GetPlayLength() / FMath::Max(CurrentMoveData.MontagePlayRate, 0.01f);
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

void UCombatComponent::OnMove(const FVector2D& MoveInput)
{
	CurrentMoveInput = MoveInput;
}

void UCombatComponent::RequestPredictionReconcile()
{
	if (IsPredictionReconcileEnabled())
	{
		bPredictionReconcilePending = true;
	}
}

// ---------------------------------------------------------------------------
// Hit detection (server only)
// ---------------------------------------------------------------------------

void UCombatComponent::HandleAttackStart()
{
	bIsCharging = false;
	bChargeInputHeld = false;
	bChargeWindowActive = false;

	if (CurrentMoveData.bIsChargeMove
		&& !CurrentMoveData.AttackSectionName.IsNone()
		&& CurrentMoveData.Montage
		&& CurrentMoveData.Montage->IsValidSectionName(CurrentMoveData.AttackSectionName))
	{
		CurrentSectionName = CurrentMoveData.AttackSectionName;
	}

	if (MovePhase == EMHCombatMovePhase::Startup || MovePhase == EMHCombatMovePhase::Charge)
	{
		SetCombatState(CombatState, EMHCombatMovePhase::Active);
		if (GetOwner() && GetOwner()->HasAuthority())
		{
			CommitActionState(false);
		}
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

	const float Radius = FMath::Max(CurrentMoveData.HitRadius, 20.f);
	const bool bHit = PerformHitQuery(PreviousHitOrigin, PreviousHitOrigin);
	DrawDebugHitSweep(PreviousHitOrigin, PreviousHitOrigin, Radius, bHit);

	if (GetCombatNetLogLevel() >= 1)
	{
		UE_LOG(LogMHCombatNet, Log,
			TEXT("[CombatNet] Attack hit window begin. Attacker=%s Move=%s Origin=%s Radius=%.1f"),
			*CachedCharacter->GetName(), *CurrentMoveData.MoveId.ToString(), *PreviousHitOrigin.ToString(), Radius);
	}
}

void UCombatComponent::EndHitWindow()
{
	if (bHitWindowActive && GetCombatNetLogLevel() >= 1)
	{
		UE_LOG(LogMHCombatNet, Log,
			TEXT("[CombatNet] Attack hit window end. Attacker=%s Move=%s HitTargets=%d"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"),
			*CurrentMoveData.MoveId.ToString(), HitActorsThisMove.Num());
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

	const bool bHit = PerformHitQuery(PreviousHitOrigin, CurrentHitOrigin);
	DrawDebugHitSweep(PreviousHitOrigin, CurrentHitOrigin, FMath::Max(CurrentMoveData.HitRadius, 20.f), bHit);
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
	return CachedCharacter->GetActorLocation() + CachedCharacter->GetActorForwardVector() * ForwardOffset;
}

bool UCombatComponent::PerformHitQuery(const FVector& Start, const FVector& End)
{
	if (!GetWorld() || !CachedCharacter || !CachedMesh)
	{
		return false;
	}

	bool bAppliedHit = false;
	const float Radius = FMath::Max(CurrentMoveData.HitRadius, 20.f);
	const FCollisionShape HitShape = FCollisionShape::MakeSphere(Radius);
	const FCollisionQueryParams QueryParams(FName(TEXT("MHCombatHit")), false, CachedCharacter);
	const bool bHasMovement = FVector::DistSquared(Start, End) > FMath::Square(0.1f);

	if (!bHasMovement)
	{
		TArray<FOverlapResult> Overlaps;
		if (!GetWorld()->OverlapMultiByChannel(Overlaps, End, FQuat::Identity, ECC_Pawn, HitShape, QueryParams))
		{
			return false;
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
			bAppliedHit |= TryApplyHit(Target, HitLocation, -CachedCharacter->GetActorForwardVector());
		}

		return bAppliedHit;
	}

	TArray<FHitResult> Hits;
	if (!GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Pawn, HitShape, QueryParams))
	{
		return false;
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
		bAppliedHit |= TryApplyHit(Target, HitLocation, HitNormal);
	}

	return bAppliedHit;
}

void UCombatComponent::DrawDebugHitSweep(const FVector& Start, const FVector& End, float Radius, bool bHit) const
{
#if ENABLE_DRAW_DEBUG
	const float DrawTime = CVarMHCombatDrawHitSweep.GetValueOnAnyThread();
	if (DrawTime <= 0.f || !GetWorld())
	{
		return;
	}

	const FColor Color = bHit ? FColor::Red : FColor::Yellow;
	DrawDebugSphere(GetWorld(), Start, Radius, 16, Color, false, DrawTime, 0, 1.f);
	DrawDebugSphere(GetWorld(), End, Radius, 16, Color, false, DrawTime, 0, 1.f);
	DrawDebugLine(GetWorld(), Start, End, Color, false, DrawTime, 0, 1.5f);

	const FVector Delta = End - Start;
	if (!Delta.IsNearlyZero())
	{
		const FVector Center = (Start + End) * 0.5f;
		const FQuat Rotation = Delta.Rotation().Quaternion();
		DrawDebugCapsule(
			GetWorld(),
			Center,
			Delta.Size() * 0.5f,
			Radius,
			Rotation,
			FColor(Color.R, Color.G, Color.B, 48),
			false,
			DrawTime,
			0,
			0.5f);
	}
#endif
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

	const bool bHandled = ApplyDamageToTarget(Target, HitLocation, HitNormal);
	if (bHandled)
	{
		// One action can hit the same target only once, even while the hit window runs.
		HitActorsThisMove.Add(Target);
	}
	return bHandled;
}

void UCombatComponent::PerformHitCheck()
{
	if (bHitExecuted || !CachedCharacter || !CachedMesh || !CurrentWeapon || !GetWorld())
	{
		return;
	}

	bHitExecuted = true;

	const FVector Origin = ResolveHitOrigin();
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
		if (!Target)
		{
			continue;
		}

		const FVector HitLocation = Overlap.Component.IsValid() ? Overlap.Component->GetComponentLocation() : Target->GetActorLocation();
		TryApplyHit(Target, HitLocation, -CachedCharacter->GetActorForwardVector());
	}
}

bool UCombatComponent::ApplyDamageToTarget(AActor* Target, const FVector& HitLocation, const FVector& HitNormal)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	FMHDamageEvent DamageEvent;
	DamageEvent.Source = CachedCharacter;
	DamageEvent.Weapon = CurrentWeapon;
	DamageEvent.Damage = ResolveDamage(CurrentMoveData);
	DamageEvent.HitLocation = HitLocation;
	DamageEvent.HitNormal = HitNormal;
	// 只传强度，击退方向由受击方按当时的相对位置推算，避免锁死世界坐标方向。
	DamageEvent.LaunchStrength = CurrentMoveData.LaunchStrength;
	DamageEvent.HitId = ++LastHitId;
	DamageEvent.MoveIndex = CurrentMoveIndex;
	DamageEvent.MoveId = CurrentMoveData.MoveId;
	DamageEvent.HitReactionId = CurrentMoveData.HitReactionId;
	DamageEvent.PoiseDamage = CurrentMoveData.PoiseDamage;
	DamageEvent.HitStunDuration = CurrentMoveData.HitStunDuration;
	DamageEvent.bInterruptTarget = CurrentMoveData.bInterruptTarget;

	const FMHDamageResult DamageResult = IMHCombatTargetInterface::Execute_ReceiveDamage(Target, DamageEvent);
	if (!DamageResult.bHit)
	{
		return false;
	}

	FMHCombatHitEvent HitEvent;
	HitEvent.HitId = DamageEvent.HitId;
	HitEvent.Attacker = CachedCharacter;
	HitEvent.Target = Target;
	HitEvent.Weapon = CurrentWeapon;
	HitEvent.MoveIndex = CurrentMoveIndex;
	HitEvent.MoveId = CurrentMoveData.MoveId;
	HitEvent.HitReactionId = DamageResult.HitReactionId.IsNone() ? DamageEvent.HitReactionId : DamageResult.HitReactionId;
	HitEvent.ReactionDirection = DamageResult.ReactionDirection;
	HitEvent.HitLocation = HitLocation;
	HitEvent.HitNormal = HitNormal;
	HitEvent.AppliedDamage = DamageResult.AppliedDamage;
	HitEvent.RemainingHealth = DamageResult.RemainingHealth;
	HitEvent.bKilled = DamageResult.bKilled;
	HitEvent.bInvulnerable = DamageResult.bInvulnerable;
	HitEvent.bSuperArmorBlocked = DamageResult.bSuperArmorBlocked;
	HitEvent.Feedback = CurrentMoveData.HitFeedback;

	// 服务器本地先处理一份，保证监听服务器（主机自己就是玩家）没有额外延迟；
	// 其余端从 ReplicatedHitEvents 复制过去，ProcessConfirmedHit 内部按 HitId 去重。
	ProcessConfirmedHit(HitEvent);
	// 服务器本地立即处理一份（监听服务器要保持零延迟），其余端由复制队列补上。
	PushReplicatedHitEvent(HitEvent);

	if (GetCombatNetLogLevel() >= 1)
	{
		UE_LOG(LogMHCombatNet, Log,
			TEXT("[CombatNet] Hit resolved Attacker=%s Target=%s Move=%s HitId=%d Applied=%.2f Remaining=%.2f Invulnerable=%d SuperArmor=%d"),
			CachedCharacter ? *CachedCharacter->GetName() : TEXT("null"),
			*Target->GetName(),
			*CurrentMoveData.MoveId.ToString(),
			HitEvent.HitId,
			HitEvent.AppliedDamage,
			HitEvent.RemainingHealth,
			HitEvent.bInvulnerable ? 1 : 0,
			HitEvent.bSuperArmorBlocked ? 1 : 0);
	}

	return true;
}

float UCombatComponent::ResolveDamage(const FMHCombatMoveData& MoveData) const
{
	return MoveData.Damage * DamageMultiplier;
}

void UCombatComponent::PushReplicatedHitEvent(const FMHCombatHitEvent& HitEvent)
{
	// 只保留最近若干条：复制队列不是日志，旧事件再发一遍没有意义，反而占带宽。
	constexpr int32 MaxHistory = 8;
	while (ReplicatedHitEvents.Num() >= MaxHistory)
	{
		ReplicatedHitEvents.RemoveAt(0, 1, EAllowShrinking::No);
	}

	ReplicatedHitEvents.Add(HitEvent);
}

void UCombatComponent::OnRep_ReplicatedHitEvents()
{
	// 复制的是整个数组，已经处理过的条目靠 ProcessConfirmedHit 里的 HitId 去重。
	for (const FMHCombatHitEvent& HitEvent : ReplicatedHitEvents)
	{
		ProcessConfirmedHit(HitEvent);
	}
}

void UCombatComponent::ProcessConfirmedHit(const FMHCombatHitEvent& HitEvent)
{
	if (HitEvent.HitId != INDEX_NONE && ProcessedHitIds.Contains(HitEvent.HitId))
	{
		return;
	}

	if (HitEvent.HitId != INDEX_NONE)
	{
		ProcessedHitIds.Add(HitEvent.HitId);
		if (ProcessedHitIds.Num() > 256)
		{
			ProcessedHitIds.Reset();
			ProcessedHitIds.Add(HitEvent.HitId);
		}
	}

	LastConfirmedHitEvent = HitEvent;
	OnHitConfirmed.Broadcast(LastConfirmedHitEvent);

	if (GetCombatNetLogLevel() >= 1 && GetWorld())
	{
		// 每台机器都会打一行：排查「主机看得到、客户端看不到」时，先看客户端有没有这行。
		UE_LOG(LogMHCombatNet, Log,
			TEXT("[CombatNet] Hit event processed. HitId=%d NetMode=%d Owner=%s Attacker=%s Effect=%s"),
			HitEvent.HitId,
			static_cast<int32>(GetWorld()->GetNetMode()),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(HitEvent.Attacker),
			*HitEvent.Feedback.ImpactEffect.ToString());
	}
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

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
