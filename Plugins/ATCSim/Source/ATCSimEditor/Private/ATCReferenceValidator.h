#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "ATCReferenceValidator.generated.h"

/**
 * Blocks assets in the ATC folders that depend on content outside the project, this plugin or stock engine content.
 * In the EHAM prototype imported materials pointed into the engine's Interchange folder and broke on another PC.
 */
UCLASS()
class UATCReferenceValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
