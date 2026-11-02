#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ScenarioData.generated.h"

class AATCAircraft;
class UAirportData;
class UAircraftTypeData;
class UTrafficMixData;
class UDataLayerAsset;
class UWeatherPresetData;

/** One aircraft in the exercise. */
USTRUCT(BlueprintType)
struct FScenarioAircraft
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FName Callsign;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	TSoftObjectPtr<UAircraftTypeData> Type;

	/** Empty = AATCAircraft; or a Blueprint such as BP_Aircraft_Base. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	TSoftClassPtr<AATCAircraft> AircraftClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FGameplayTag Stand;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FGameplayTag TaxiRoute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	bool bAutoTaxiAfterPushback = true;

	/** Remove it 30 s after it arrives at the end of its route (departed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	bool bRemoveAtRouteEnd = false;
};

/**
 * Traffic that enters during the exercise through the spawn manager: from a spawn point (Spawn.*) or straight onto
 * a route. Count > 1 makes a stream, one every IntervalSeconds.
 */
USTRUCT(BlueprintType)
struct FScenarioSpawn
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	float TimeOffsetSeconds = 0.f;

	/** Route, place, end and traffic mix come from this spawn point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FGameplayTag SpawnPoint;

	/** Without a spawn point: the route, the place on it and the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FGameplayTag Route;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario", meta = (Units = "m"))
	float StartDistanceM = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FName EndStop;

	/** Empty = automatic. With Count > 1 the stream gets KLM1001, KLM1002, ... from KLM1001. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FName Callsign;

	/** Empty = the spawn point's type or traffic mix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	TSoftObjectPtr<UAircraftTypeData> Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	TSoftObjectPtr<UTrafficMixData> TrafficMix;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario", meta = (ClampMin = "1"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario", meta = (Units = "s"))
	float IntervalSeconds = 90.f;
};

/** A command at a time after the start, for example T+02:00 KLM1023 Cmd.Pushback. */
USTRUCT(BlueprintType)
struct FScenarioEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	float TimeOffsetSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FName Callsign;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FGameplayTag Command;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FGameplayTag Parameter;
};

/** One complete exercise: airport, tower position, start time, weather and traffic. */
UCLASS(BlueprintType)
class ATCAIRPORTS_API UScenarioData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("Scenario"), GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TSoftObjectPtr<UAirportData> Airport;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	FGameplayTag TowerPosition;

	/**
	 * Real time and real weather: the clock is the computer's UTC time, and the weather comes from the airport's METAR or
	 * changes by itself (Project Settings > Plugins > ATC Sim > Environment). Off: this scenario's StartTimeUTC, TimeScale
	 * and Weather, and the weather stays as it is (an exercise that needs fog at night).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	bool bLiveEnvironment = true;

	/** UTC, so the traffic picture is the same in summer and winter. Only with bLiveEnvironment off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario", meta = (EditCondition = "!bLiveEnvironment"))
	FDateTime StartTimeUTC = FDateTime(2026, 10, 13, 9, 30, 0);

	/** Speed of the clock (sun); traffic always runs in real time. Only with bLiveEnvironment off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario", meta = (EditCondition = "!bLiveEnvironment"))
	float TimeScale = 1.f;

	/** Empty = the airport's default weather. With bLiveEnvironment: the weather until the real one is in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TSoftObjectPtr<UWeatherPresetData> Weather;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TArray<FScenarioAircraft> Aircraft;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TArray<FScenarioEvent> Events;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TArray<FScenarioSpawn> Spawns;

	/** Start the map's traffic director: arrivals, turnarounds and departures on their own, like a real day. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario|Traffic")
	bool bStartTrafficDirector = false;

	/** With the traffic director: a simulated colleague gives the clearances. Off = the trainee gives every one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario|Traffic")
	bool bAutoClearances = true;

	/** With the traffic director: < 0 keeps the director's own setting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario|Traffic")
	float ArrivalsPerHour = -1.f;

	/** World Partition data layers to activate, for example construction work or a closed taxiway. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TArray<TSoftObjectPtr<UDataLayerAsset>> ActiveDataLayers;
};
