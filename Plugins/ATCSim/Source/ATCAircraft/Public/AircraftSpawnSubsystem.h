#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "AircraftSpawnSubsystem.generated.h"

class AAircraftSpawnPoint;
class AATCAircraft;
class ATaxiRoute;
class UAircraftTypeData;
class UTrafficMixData;

/** Everything needed to put one aircraft on a taxi route (or on a stand). Empty fields are filled in by the spawn manager. */
USTRUCT(BlueprintType)
struct ATCAIRCRAFT_API FAircraftSpawnRequest
{
	GENERATED_BODY()

	/** Empty = airline from the traffic mix plus a free flight number, for example KLM1427. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FName Callsign;

	/** Empty = picked from TrafficMix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TObjectPtr<UAircraftTypeData> Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TObjectPtr<UTrafficMixData> TrafficMix;

	/** Empty = the traffic mix's class, else BP_Aircraft_Base. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TSubclassOf<AATCAircraft> AircraftClass;

	/** The route to put the aircraft on; or RouteTag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TObjectPtr<ATaxiRoute> Route;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FGameplayTag RouteTag;

	/** Main gear position along the route, metres from its start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (Units = "m"))
	float StartDistanceM = 0.f;

	/** Stop or waypoint where the taxi ends, for example "Holding point 36L". Empty = EndDistanceM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FName EndStop;

	/** Metres from the route start where the taxi ends; < 0 = end of the route. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (Units = "m"))
	float EndDistanceM = -1.f;

	/** Start taxiing at once; otherwise wait on the route for Cmd.Taxi. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bStartTaxi = true;

	/** Remove the aircraft after it arrives at the end (a departure that has taken off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bRemoveAtRouteEnd = true;

	/** Wait this long at the route end before the takeoff roll (lined up, waiting for the gap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (Units = "s"))
	float RemoveDelaySeconds = 30.f;

	/** Speed at the moment it appears, for example 40 kt for a landing aircraft on its rollout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	float InitialSpeedKt = 0.f;

	/**
	 * Appear on final approach and land on the runway behind the route start (an arrival route that starts on the
	 * runway); the aircraft joins the route at InitialSpeedKt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bLand = false;

	/** Instead of a route: park on this stand; RouteTag is then the taxi route after pushback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FGameplayTag StandTag;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAircraftSpawned, AATCAircraft*, Aircraft, FName, Callsign);

/**
 * Aircraft Spawn Manager: puts aircraft on taxi routes at runtime. A request names a route (or a spawn point
 * fills it in), a place along it, a type or traffic mix and an end point. When another aircraft is in the way the
 * request waits in a queue and is retried every half second, so aircraft never overlap. Spawned aircraft are
 * ordinary AATCAircraft: they use the same taxi, pushback, speed, separation and lights as all others.
 */
UCLASS()
class ATCAIRCRAFT_API UAircraftSpawnSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	/** Spawn now if there is room, otherwise queue it. Returns the callsign (NAME_None when the request is unusable). */
	UFUNCTION(BlueprintCallable, Category = "ATC|Spawn")
	FName RequestSpawn(const FAircraftSpawnRequest& Request);

	/** Spawn now; nullptr when the place is taken (OutBlocker) or the request is unusable. Does not queue. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Spawn")
	AATCAircraft* TrySpawn(const FAircraftSpawnRequest& Request, AActor*& OutBlocker);

	/** Removes an aircraft, or a request that is still waiting. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Spawn")
	bool Despawn(FName Callsign);

	UFUNCTION(BlueprintPure, Category = "ATC|Spawn")
	TArray<FName> GetQueuedCallsigns() const;

	/** A free callsign: one of the airlines plus a flight number, for example KLM1427. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Spawn")
	FName MakeCallsign(const TArray<FString>& Airlines);

	UFUNCTION(BlueprintPure, Category = "ATC|Spawn")
	bool IsCallsignInUse(FName Callsign) const;

	void RegisterSpawnPoint(AAircraftSpawnPoint* SpawnPoint);
	void UnregisterSpawnPoint(AAircraftSpawnPoint* SpawnPoint);

	UFUNCTION(BlueprintPure, Category = "ATC|Spawn")
	AAircraftSpawnPoint* FindSpawnPoint(FGameplayTag SpawnPointTag) const;

	UPROPERTY(BlueprintAssignable, Category = "ATC|Spawn")
	FOnAircraftSpawned OnAircraftSpawned;

private:
	/** Fills in route, type, class and callsign. False when there is no route or type. */
	bool Resolve(FAircraftSpawnRequest& Request);
	void ProcessQueue();

	/** Logs and broadcasts Event.Traffic.Spawned. */
	void ReportSpawned(AATCAircraft* Aircraft, ATaxiRoute* Route, float StartCm);

	/**
	 * Holding stack: true while a landing should not come yet. It comes when the last landing on its runway came
	 * LandingSpacingSeconds ago and one of its runway exits can be cleared now (nobody on it, nobody who has waited for
	 * it longer than the landing's head start); after MaxHoldingSeconds also without the exit. OutBlocker = who holds it up.
	 */
	bool HoldLanding(const FAircraftSpawnRequest& Request, ATaxiRoute* Route, float StartCm, AActor*& OutBlocker);

	UPROPERTY(Transient)
	TArray<FAircraftSpawnRequest> Queue;

	/** Landings in the holding stack since (game seconds), by callsign. */
	TMap<FName, float> HoldingSince;

	/** The last landings: where they join their runway, the landing direction, when. */
	struct FRecentLanding
	{
		FVector Start = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		float Time = 0.f;
		TWeakObjectPtr<AActor> Aircraft;
	};
	TArray<FRecentLanding> RecentLandings;

	TArray<TWeakObjectPtr<AAircraftSpawnPoint>> SpawnPoints;
	FTimerHandle QueueTimer;

	/** Fixed seed: the same exercise gives the same callsigns and types every run. */
	FRandomStream Random = FRandomStream(1013);
};
