#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TrafficMixData.generated.h"

class AATCAircraft;
class UAircraftTypeData;

/** One kind of aircraft in a traffic mix: the type, the airlines that fly it and how often it comes. */
USTRUCT(BlueprintType)
struct ATCAIRCRAFT_API FTrafficMixEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TSoftObjectPtr<UAircraftTypeData> Type;

	/** ICAO airline codes for the callsign, for example KLM or TRA. A random one is picked, plus a flight number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TArray<FString> Airlines;

	/** Relative share: weight 3 comes three times as often as weight 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0"))
	float Weight = 1.f;
};

/**
 * Which aircraft a spawn point or scenario sends out, for example "Schiphol morning": mostly KLM 737s and
 * 787s, some easyJet A320s. A new mix is a new asset, no code.
 */
UCLASS(BlueprintType)
class ATCAIRCRAFT_API UTrafficMixData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("TrafficMix"), GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	TArray<FTrafficMixEntry> Entries;

	/** Empty = BP_Aircraft_Base. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	TSoftClassPtr<AATCAircraft> AircraftClass;

	/** Weighted random entry; nullptr when the mix is empty. */
	const FTrafficMixEntry* Pick(FRandomStream& Random) const;
};
