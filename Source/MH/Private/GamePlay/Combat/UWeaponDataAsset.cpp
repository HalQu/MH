#include "GamePlay/Combat/UWeaponDataAsset.h"

FPrimaryAssetId UWeaponDataAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(GetClass()->GetFName(), GetFName());
}
