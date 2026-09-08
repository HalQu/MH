#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GamePlay/Combat/MHCombatTypes.h"
#include "UWeaponDataAsset.generated.h"

/*
 * Passive weapon definition. CombatComponent owns the combo state machine;
 * weapons only describe which moves are available.
 */
UCLASS(BlueprintType, Blueprintable)
class MH_API UWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FName WeaponId = TEXT("DefaultWeapon");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TObjectPtr<UStaticMesh> MeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|MoveSet")
	TArray<FMHCombatMoveData> Moves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Start")
	TMap<FComboCondition, int32> GroundStartMoves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Start")
	TMap<FComboCondition, int32> AirStartMoves;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
