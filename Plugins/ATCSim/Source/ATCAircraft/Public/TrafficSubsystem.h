#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "TrafficSubsystem.generated.h"

class AATCAircraft;
class AStand;
class ATaxiRoute;
class UAircraftTypeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnAircraftEvent, AActor*, Aircraft, FName, Callsign, FGameplayTag, EventTag, const FString&, Detail);

/**
 * Register of all aircraft, stands and routes in the world. Spawns scenario traffic, forwards commands and
 * bundles every aircraft event into one OnAircraftEvent, so UI and scenario listen in one place.
 * Actors register themselves in BeginPlay: nobody searches the level.
 */
UCLASS()
class ATCAIRCRAFT_API UTrafficSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	void RegisterAircraft(AActor* Aircraft);
	void UnregisterAircraft(AActor* Aircraft);
	void RegisterStand(AStand* Stand);
	void UnregisterStand(AStand* Stand);
	void RegisterRoute(ATaxiRoute* Route);
	void UnregisterRoute(ATaxiRoute* Route);

	UFUNCTION(BlueprintPure, Category = "ATC|Traffic")
	AActor* FindAircraft(FName Callsign) const;

	UFUNCTION(BlueprintPure, Category = "ATC|Traffic")
	AStand* FindStand(FGameplayTag StandTag) const;

	UFUNCTION(BlueprintPure, Category = "ATC|Traffic")
	ATaxiRoute* FindRoute(FGameplayTag RouteTag) const;

	UFUNCTION(BlueprintPure, Category = "ATC|Traffic")
	TArray<AActor*> GetAllAircraft() const;

	/** Every taxi route in play: the taxi network is built from these. */
	TArray<ATaxiRoute*> GetAllRoutes() const;

	/** Spawns an aircraft parked on a stand. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Traffic")
	AATCAircraft* SpawnAircraft(TSubclassOf<AATCAircraft> AircraftClass, UAircraftTypeData* Type, FName Callsign, FGameplayTag StandTag,
		FGameplayTag TaxiRoute, bool bAutoTaxiAfterPushback = true);

	/**
	 * First aircraft on Self's taxi path within LookAheadCm of its nose: the one to keep the separation to. Only aircraft
	 * on a path count (pushing, taxiing, holding, waiting on a route): parked aircraft stand clear of the routes. When two
	 * taxiing aircraft head for the same spot, the one further from it gives way (the reservations of the taxi network
	 * normally keep that from happening). OutGapCm = from Self's nose to that aircraft, along the path.
	 */
	AActor* FindTrafficAhead(const AActor* Self, float LookAheadCm, float& OutGapCm) const;

	/**
	 * No aircraft within reach of a footprint from Tail to Nose, stretched ClearanceCm forward and backward, with
	 * HalfSpanCm to the side. OutBlocker = the aircraft in the way.
	 */
	bool IsSpaceClear(const FVector& Tail, const FVector& Nose, float HalfSpanCm, float ClearanceCm, AActor*& OutBlocker) const;

	/** Cmd.Pushback (parameter: Pushback.*), Cmd.Taxi (parameter: Route.*, empty = assigned), Cmd.HoldPosition, Cmd.Continue. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Traffic")
	bool SendCommand(FName Callsign, FGameplayTag Command, FGameplayTag Parameter);

	void BroadcastAircraftEvent(AActor* Aircraft, const FGameplayTag& EventTag, const FString& Detail);

	UPROPERTY(BlueprintAssignable, Category = "ATC|Traffic")
	FOnAircraftEvent OnAircraftEvent;

private:
	TArray<TWeakObjectPtr<AActor>> Aircraft;
	TArray<TWeakObjectPtr<AStand>> Stands;
	TArray<TWeakObjectPtr<ATaxiRoute>> Routes;
};
