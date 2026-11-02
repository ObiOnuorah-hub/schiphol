#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RadioVoiceData.generated.h"

class USoundBase;

/**
 * The recorded fragments of the radio phraseology: digits, NATO letters, airlines and phrases, one set for the
 * controller and one for the pilots. Made by Plugins/ATCSim/Tools/MakeRadioVoice.ps1 and ATC.ImportRadioVoice.
 */
UCLASS(BlueprintType)
class ATCAIRPORTS_API URadioVoiceData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("RadioVoice"), GetFName()); }

	/** Token (d3, lg, klm, cleared_for_takeoff, squelch_open, ...) -> fragment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio")
	TMap<FName, TObjectPtr<USoundBase>> Controller;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio")
	TMap<FName, TObjectPtr<USoundBase>> Pilot;
};
