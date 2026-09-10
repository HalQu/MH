#include "GamePlay/Combat/UCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GamePlay/Combat/IMHCombatTargetInterface.h"
#include "GamePlay/Combat/UWeaponDataAsset.h"

UCombatComponent::UCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UCombatComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedCharacter = Cast<ACharacter>(GetOwner());
	if (!CachedCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Owner is not a character."));
		return;
	}

	CachedMesh = CachedCharacter->GetMesh();
	CachedMovement = CachedCharacter->GetCharacterMovement();
	CachedAnimInstance = CachedMesh ? CachedMesh->GetAnimInstance() : nullptr;
	BindMontageDelegates();

	LoadoutWeapons.RemoveAll([](const TObjectPtr<UWeaponDataAsset>& Weapon)
	{
		return !Weapon;
	});

	if (!CurrentWeapon)
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
	CurrentChargeInputAction = nullptr;
	bIsCharging = false;
	bChargeInputHeld = false;
	Super::EndPlay(EndPlayReason);
}

void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CombatState == EMHCombatState::Attack && CachedAnimInstance && CurrentMoveData.Montage)
	{
		CurrentMoveTime = CachedAnimInstance->Montage_GetPosition(CurrentMoveData.Montage);
	}
}

bool UCombatComponent::HandleComboInput(UInputAction* InputAction, ETriggerEvent TriggerEvent)
{
	if (!InputAction || !CachedCharacter || !CurrentWeapon || !GetWorld())
	{
		return false;
	}

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

	const FMHCombatInputSnapshot InputSnapshot{InputAction, TriggerEvent, CurrentMoveInput, HoldDuration};

	if (CombatState == EMHCombatState::Attack
		&& bIsChargeMove
		&& TriggerEvent == ETriggerEvent::Completed
		&& CurrentChargeInputAction.Get() == InputAction
		&& bChargeInputHeld)
	{
		UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] Charge input released for move: %s"), *CurrentMoveData.MoveId.ToString());
		if (bIsCharging)
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
	return Move && StartMove(*Move, MoveIndex, Input.TriggerEvent == ETriggerEvent::Started ? Input.InputAction : nullptr);
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

	if (NewWeapon->MeshAsset != nullptr)
	{
		if (CurrentWeaponMesh)
		{
			CurrentWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			CurrentWeaponMesh->UnregisterComponent();
			CurrentWeaponMesh->DestroyComponent();
			CurrentWeaponMesh = nullptr;
		}
		CurrentWeaponMesh = NewObject<UStaticMeshComponent>(CachedCharacter);
		if (!CurrentWeaponMesh) return false;

		CurrentWeaponMesh->SetStaticMesh(NewWeapon->MeshAsset);

		CurrentWeaponMesh->RegisterComponent();

		USkeletalMeshComponent* SkeletalMesh = CachedCharacter->GetMesh();
		if (SkeletalMesh)
		{
			CurrentWeaponMesh->AttachToComponent(
				SkeletalMesh,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				DefaultHitOriginSocketName // 或 NewWeapon->SocketName
			);
		}

		CurrentWeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		CurrentWeaponMesh->SetVisibility(true);
		UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] Equipped weapon: %s"), *NewWeapon->GetName());
	}
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
	if (CombatState != EMHCombatState::Attack || !IsCurrentMontage(SourceMontage))
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

void UCombatComponent::OnMove(const FVector2D& MoveInput)
{
	CurrentMoveInput = MoveInput;
}

bool UCombatComponent::StartMove(const FMHCombatMoveData& Move, int32 MoveIndex, UInputAction* SourceInputAction)
{
	UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] StartMove() called with MoveId: %s, MoveIndex: %d"), *Move.MoveId.ToString(), MoveIndex);
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

	if (CurrentMoveData.Montage)
	{
		CachedCharacter->PlayAnimMontage(CurrentMoveData.Montage, CurrentMoveData.MontagePlayRate, CurrentMoveData.SectionName);
	}

	OnAttackStarted.Broadcast(CurrentMoveData.MoveId);
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
	ClearBufferedComboInput();
	return StartMove(*NextMove, NextMoveIndex, SourceInputAction);
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
	if (CombatState != EMHCombatState::Attack)
	{
		return;
	}

	UAnimMontage* PlayingMontage = CurrentMoveData.Montage;

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
	if (!IsCurrentMontage(Montage))
	{
		return;
	}

	TryStartNextCombo();
}

void UCombatComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!IsCurrentMontage(Montage))
	{
		return;
	}

	if (!TryStartNextCombo())
	{
		FinishCurrentMove(bInterrupted);
	}
}
