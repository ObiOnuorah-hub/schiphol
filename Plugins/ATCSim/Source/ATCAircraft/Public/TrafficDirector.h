#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "TrafficDirector.generated.h"

class AATCAircraft;
class ATaxiRoute;
class UAircraftTypeData;
class UTrafficMixData;

/** A gate in the traffic program: its stand, how arrivals get there and where departures go. */
USTRUCT(BlueprintType)
struct ATCAIRCRAFT_API FTrafficGate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FGameplayTag Stand;

	/** From the landing runway exit to the stand. Empty = no arrivals here, only aircraft parked at the start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FGameplayTag ArrivalRoute;

	/** From the end of the pushback to the departure runway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FGameplayTag DepartureRoute;

	/** Empty = the stand's first pushback option. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FGameplayTag PushbackOption;

	/** Wide-body gate (code E): 787, 777, A350. Otherwise code C: A320, 737. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bWideBody = false;

	/**
	 * The parked aircraft of the map itself (static or skeletal meshes) that stand on this gate, normally one. When the
	 * traffic starts, an aircraft of ParkedType with a callsign of ParkedAirline takes their place (they are hidden), and departs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parked model")
	TArray<TObjectPtr<AActor>> ParkedModels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parked model")
	TSoftObjectPtr<UAircraftTypeData> ParkedType;

	/** ICAO airline of the parked model (KLM, DAL, ...): the callsign, and with it the livery. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parked model")
	FString ParkedAirline;
};

/**
 * Runs an airport's traffic on its own, the way a real day looks from the tower: arrivals roll out on the landing
 * runway, take the exit and taxi to a free gate; after the turnaround they push back, taxi to the departure runway,
 * line up and take off. With bAutoClearances a simulated colleague gives the runway crossing and line-up
 * clearances (one departure per runway at a time); without it the trainee gives every clearance (ATC.Cmd ... Continue).
 */
UCLASS(Blueprintable)
class ATCAIRCRAFT_API ATrafficDirector : public AActor
{
	GENERATED_BODY()

public:
	ATrafficDirector();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TArray<FTrafficGate> Gates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TObjectPtr<UTrafficMixData> TrafficMix;

	/**
	 * Start on plain Play (EHAM: on, set with ATC.TrafficOnPlay). Off: only a scenario, ATC.TrafficDirector start or
	 * Blueprint starts it. A scenario with its own traffic stops it and clears the airport first.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bStartOnBeginPlay = false;

	/**
	 * At the start one departure at every spawn point of the map (EHAM: the queues for 36L and 24), just before the
	 * holding point: the first takeoffs come within minutes instead of after the first turnarounds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bDeparturesAtStart = true;

	/** How far before the holding point those first departures start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (Units = "m", EditCondition = "bDeparturesAtStart"))
	float StartDepartureBeforeHoldM = 400.f;

	/** Share of the gates with an aircraft on it when the traffic starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0", ClampMax = "1"))
	float InitialOccupancy = 0.6f;

	/** Schiphol handles up to about 70 an hour on two landing runways. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0"))
	float ArrivalsPerHour = 40.f;

	/** Time at the gate before pushback, min and max. Real turnarounds take 45-90 minutes; compressed for training. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FVector2D TurnaroundSeconds = FVector2D(240.f, 480.f);

	/**
	 * The aircraft that took the place of the map's parked models push back somewhere in this time after the start:
	 * a few at a time, not all of them at once.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FVector2D ParkedModelDepartureSeconds = FVector2D(30.f, 3600.f);

	/** A simulated colleague gives crossing and line-up clearances. Off = the trainee does it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bAutoClearances = true;

	/** Reaction time of that colleague, min and max. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FVector2D ClearanceDelaySeconds = FVector2D(6.f, 20.f);

	/** One departure per runway in this time (line-up, roll, out of the way). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (Units = "s"))
	float DepartureIntervalSeconds = 80.f;

	/**
	 * Departure management, like the start-up times of a CDM system: no more than this many departures push back and taxi
	 * to one runway at a time; the next one stays at its gate a little longer. 0 = no limit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0"))
	int32 MaxDeparturesTaxiing = 5;

	/** Seconds lined up before the takeoff roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (Units = "s"))
	float LineUpSeconds = 12.f;

	/**
	 * Speed of an arrival when it turns off the runway onto its arrival route, at the end of the rollout. The landing
	 * reserves room to brake from it, so a lower speed lets landings follow each other closer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float ArrivalSpeedKt = 30.f;

	/** Arrivals fly the final approach and land. Off: they appear on the runway at the end of the rollout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bVisibleLandings = true;

	/** A landing keeps its runway closed for crossings this long: final approach and rollout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (Units = "s"))
	float LandingRunwaySeconds = 120.f;

	UFUNCTION(BlueprintCallable, Category = "ATC|Traffic")
	void StartTraffic();

	UFUNCTION(BlueprintCallable, Category = "ATC|Traffic")
	void StopTraffic();

	UFUNCTION(BlueprintPure, Category = "ATC|Traffic")
	bool IsRunning() const { return bRunning; }

	/** One line per gate and the runway queue, for ATC.TrafficDirector status. */
	FString Describe() const;

private:
	void ScheduleArrival();
	void SpawnArrival();
	void Depart(FName Callsign);
	void TryClear(FName Callsign, FString StopName);

	UFUNCTION()
	void HandleAircraftEvent(AActor* Aircraft, FName Callsign, FGameplayTag EventTag, const FString& Detail);

	UFUNCTION()
	void HandleSpawned(AATCAircraft* Aircraft, FName Callsign);

	/** A free gate (no aircraft, nothing on the way to it), optionally one that has an arrival route. INDEX_NONE = all taken. */
	int32 PickFreeGate(bool bNeedArrivalRoute, const TSet<int32>& Skip = TSet<int32>());

	/**
	 * Where an aircraft of Type stands every 2 m along Route from FromCm to ToCm (negative: that far before the end), and
	 * along the route's parallel lanes too: like the movement, main gear on the line and nose gear a wheelbase further
	 * (on a pushback a wheelbase back towards the stand).
	 */
	static void AddPoses(const ATaxiRoute* Route, float FromCm, float ToCm, const UAircraftTypeData* Type, bool bTailFirst, TArray<FTransform>& OutPoses);

	/** The aircraft on a stand that an aircraft of Type would touch somewhere in these poses; nullptr = the way is free. */
	const AATCAircraft* FindInTheWay(const AActor* Self, const UAircraftTypeData* Type, const TArray<FTransform>& Poses) const;

	/** Type and airlines from the mix that fit the gate size. */
	bool PickType(bool bWideBody, UAircraftTypeData*& OutType, TArray<FString>& OutAirlines);

	float Now() const;

	/** Callsign -> gate it is going to or standing on. */
	TMap<FName, int32> GateOf;

	/** Aircraft that take the place of a parked model, until they are spawned. */
	TSet<FName> ParkedModelStandIns;

	/** Runway designator ("36L") -> time from which the next departure or crossing may use it. */
	TMap<FString, float> RunwayFreeAt;

	FTimerHandle ArrivalTimer;
	FRandomStream Random = FRandomStream(2026);
	bool bRunning = false;
};
