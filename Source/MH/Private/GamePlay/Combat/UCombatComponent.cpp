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
	Super::EndPlay(EndPlayReason);
}

void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CombatState == EMHCombatState::Attack || CombatState == EMHCombatState::AirAttack)
	{
		UpdateMoveTiming(DeltaTime);
	}
}

bool UCombatComponent::TryAttack(bool bForceAirAttack)
{
	if (!CachedCharacter || !CurrentWeapon)
	{
		return false;
	}

	if (CombatState == EMHCombatState::Locomotion || CombatState == EMHCombatState::WeaponSwitch)
	{
		const bool bAirMove = bForceAirAttack || IsAirborne();
		const FMHCombatMoveData* Move = GetMoveForAttack(bAirMove, 0);
		return Move && StartMove(*Move, bAirMove, 0);
	}

	if (CombatState == EMHCombatState::Attack || CombatState == EMHCombatState::AirAttack)
	{
		const bool bAirMove = CombatState == EMHCombatState::AirAttack;
		const bool bHasNextMove = GetMoveForAttack(bAirMove, CurrentMoveIndex + 1) != nullptr;
		const bool bCanBuffer = CurrentMoveTime <= CurrentMoveData.ComboWindowEnd + AttackInputBufferDuration;

		if (CurrentMoveData.bCanChain && bCanBuffer && bHasNextMove)
		{
			bAttackInputQueued = true;
			return true;
		}
	}

	return false;
}

bool UCombatComponent::TryJumpAttack()
{
	return TryAttack(true);
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

	if (CombatState == EMHCombatState::Attack || CombatState == EMHCombatState::AirAttack)
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

void UCombatComponent::HandleCombatNotify(EMHCombatNotifyType NotifyType)
{
	if (CombatState != EMHCombatState::Attack && CombatState != EMHCombatState::AirAttack)
	{
		return;
	}

	switch (NotifyType)
	{
	case EMHCombatNotifyType::AttackHit:
		PerformHitCheck();
		break;
	case EMHCombatNotifyType::ComboWindowOpen:
		bComboWindowOpen = true;
		TryStartNextCombo();
		break;
	case EMHCombatNotifyType::ComboWindowClose:
		bComboWindowOpen = false;
		break;
	case EMHCombatNotifyType::RecoveryStart:
		SetCombatState(CombatState, EMHCombatMovePhase::Recovery);
		break;
	case EMHCombatNotifyType::WeaponSwitchAllowed:
		bWeaponSwitchAllowed = true;
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
	if (CombatState != EMHCombatState::Attack && CombatState != EMHCombatState::AirAttack)
	{
		return true;
	}

	const bool bInCancelWindow = CurrentMoveTime >= CurrentMoveData.CancelWindowStart
		&& CurrentMoveTime <= CurrentMoveData.CancelWindowEnd;

	return bWeaponSwitchAllowed || bInCancelWindow;
}

void UCombatComponent::OnMove(const FVector2D& MoveInput)
{
	CurrentMoveInput = MoveInput;
}

void UCombatComponent::OnYPressed(UInputAction* InputAction, ETriggerEvent TriggerEvent)
{
	bYIsPressed = true;
	YPressTime = GetWorld()->GetTimeSeconds();
}

void UCombatComponent::OnYReleased(UInputAction* InputAction, ETriggerEvent TriggerEvent)
{
	bYIsPressed = false;
	float HoldDuration = GetWorld()->GetTimeSeconds() - YPressTime;
}

void UCombatComponent::OnBPressed(UInputAction* InputAction, ETriggerEvent TriggerEvent)
{
	bBIsPressed = true;
	BPressTime = GetWorld()->GetTimeSeconds();
}

void UCombatComponent::OnBReleased(UInputAction* InputAction, ETriggerEvent TriggerEvent)
{
	bBIsPressed = false;
	float HoldDuration = GetWorld()->GetTimeSeconds() - BPressTime;
}

bool UCombatComponent::StartMove(const FMHCombatMoveData& Move, bool bAirMove, int32 MoveIndex)
{
	if (!CachedCharacter)
	{
		return false;
	}

	UAnimMontage* PreviousMontage = (CombatState == EMHCombatState::Attack || CombatState == EMHCombatState::AirAttack)
		? CurrentMoveData.Montage
		: nullptr;

	CurrentMoveData = Move;
	CurrentMoveIndex = MoveIndex;
	CurrentMoveId = Move.MoveId;
	CurrentMoveTime = 0.f;
	bIsAirMove = bAirMove;
	bAttackInputQueued = false;
	bHitExecuted = false;
	bWeaponSwitchAllowed = false;
	bComboWindowOpen = false;
	HitActorsThisMove.Reset();

	if (bLockGroundMovementDuringAttack && !bAirMove)
	{
		UpdateMovementLock(true);
	}

	BindMontageDelegates();

	if (PreviousMontage && PreviousMontage != CurrentMoveData.Montage)
	{
		CachedCharacter->StopAnimMontage(PreviousMontage);
	}

	SetCombatState(bAirMove ? EMHCombatState::AirAttack : EMHCombatState::Attack, EMHCombatMovePhase::Startup);

	if (CurrentMoveData.Montage)
	{
		CachedCharacter->PlayAnimMontage(CurrentMoveData.Montage, CurrentMoveData.MontagePlayRate, CurrentMoveData.SectionName);
	}

	OnAttackStarted.Broadcast(CurrentMoveData.MoveId);
	return true;
}

bool UCombatComponent::TryStartNextCombo()
{
	if (!bAttackInputQueued || !CurrentMoveData.bCanChain)
	{
		return false;
	}

	const bool bCanStartChain = bComboWindowOpen
		|| (CurrentMoveTime >= CurrentMoveData.ComboWindowStart
			&& CurrentMoveTime <= CurrentMoveData.ComboWindowEnd + AttackInputBufferDuration);

	if (!bCanStartChain)
	{
		return false;
	}

	const int32 NextIndex = CurrentMoveIndex + 1;
	const bool bAirMove = CombatState == EMHCombatState::AirAttack;
	const FMHCombatMoveData* NextMove = GetMoveForAttack(bAirMove, NextIndex);
	if (!NextMove)
	{
		bAttackInputQueued = false;
		return false;
	}

	bAttackInputQueued = false;
	return StartMove(*NextMove, bAirMove, NextIndex);
}

const FMHCombatMoveData* UCombatComponent::GetMoveForAttack(bool bAirAttack, int32 MoveIndex) const
{
	if (!CurrentWeapon)
	{
		return nullptr;
	}

	const TArray<FMHCombatMoveData>& Moves = bAirAttack ? CurrentWeapon->AirCombo : CurrentWeapon->GroundCombo;
	return Moves.IsValidIndex(MoveIndex) ? &Moves[MoveIndex] : nullptr;
}

void UCombatComponent::FinishCurrentMove(bool bInterrupted)
{
	if (CombatState != EMHCombatState::Attack && CombatState != EMHCombatState::AirAttack)
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
	bIsAirMove = false;
	bAttackInputQueued = false;
	bHitExecuted = false;
	bWeaponSwitchAllowed = false;
	bComboWindowOpen = false;
	HitActorsThisMove.Reset();

	if (PlayingMontage && CachedCharacter)
	{
		CachedCharacter->StopAnimMontage(PlayingMontage);
	}

	OnAttackEnded.Broadcast(bInterrupted);
}

void UCombatComponent::UpdateMoveTiming(float DeltaTime)
{
	CurrentMoveTime += DeltaTime;

	if (!bHitExecuted && CurrentMoveData.HitMoment >= 0.f && CurrentMoveTime >= CurrentMoveData.HitMoment)
	{
		PerformHitCheck();
	}

	if (CurrentMoveTime >= CurrentMoveData.ComboWindowStart && CurrentMoveTime <= CurrentMoveData.ComboWindowEnd)
	{
		if (!bComboWindowOpen)
		{
			bComboWindowOpen = true;
			if (TryStartNextCombo())
			{
				return;
			}
		}
	}
	else if (CurrentMoveTime > CurrentMoveData.ComboWindowEnd)
	{
		bComboWindowOpen = false;
	}

	bWeaponSwitchAllowed = CurrentMoveTime >= CurrentMoveData.CancelWindowStart
		&& CurrentMoveTime <= CurrentMoveData.CancelWindowEnd;

	EMHCombatMovePhase NextPhase = EMHCombatMovePhase::Startup;
	const float ActiveEnd = CurrentMoveData.StartupTime + CurrentMoveData.ActiveTime;
	if (CurrentMoveTime >= ActiveEnd)
	{
		NextPhase = EMHCombatMovePhase::Recovery;
	}
	else if (CurrentMoveTime >= CurrentMoveData.StartupTime)
	{
		NextPhase = EMHCombatMovePhase::Active;
	}

	SetCombatState(CombatState, NextPhase);

	if (bAttackInputQueued && CurrentMoveTime > CurrentMoveData.ComboWindowEnd + AttackInputBufferDuration)
	{
		bAttackInputQueued = false;
	}

	if (CurrentMoveTime >= GetCurrentMoveDuration())
	{
		FinishCurrentMove(false);
		return;
	}

	if (bAttackInputQueued && bComboWindowOpen)
	{
		TryStartNextCombo();
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
	DamageEvent.bIsAirAttack = bIsAirMove;

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

float UCombatComponent::GetCurrentMoveDuration() const
{
	const float FallbackDuration = FMath::Max(CurrentMoveData.GetDuration(), 0.01f);

	if (!CurrentMoveData.Montage)
	{
		return FallbackDuration;
	}

	const float PlayRate = FMath::Max(CurrentMoveData.MontagePlayRate, 0.01f);
	const float MontageDuration = CurrentMoveData.Montage->GetPlayLength() / PlayRate;
	return FMath::Max(FallbackDuration, MontageDuration);
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
