#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AircraftCommands.h"
#include "TaxiControllerComponent.h"
#include "ATCAircraft.generated.h"

class UAircraftIdentityComponent;
class UAircraftMovementComponent;
class UAircraftTypeData;
class UPushbackControllerComponent;
class UStaticMeshComponent;
class UTaxiControllerComponent;
struct FTaxiStop;

/**
 * Base aircraft (BP_Aircraft_Base derives from this). The origin is the main gear on the ground, the nose
 * points along +X. All behaviour lives in the components; this class only wires them together.
 */
UCLASS(Blueprintable)
class ATCAIRCRAFT_API AATCAircraft : public AActor, public IAircraftCommands, public IAircraftInfo
{
	GENERATED_BODY()

public:
	AATCAircraft();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Takeoff roll along the nose direction, rotation at VR and climb-out; removed once out of sight. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Aircraft")
	void StartTakeoff();

	UFUNCTION(BlueprintPure, Category = "ATC|Aircraft")
	bool IsTakingOff() const { return bTakingOff; }

	/**
	 * Final approach on a 3 degree glide path, flare, touchdown and rollout on the runway behind the start of Route (an
	 * arrival route that starts on the runway). At StartCm it joins the route at RolloutEndSpeedKt and taxis to EndCm.
	 * On final it asks the taxi network for its runway exit; not reserved 1.5 km before touchdown: it goes around.
	 */
	void StartLanding(ATaxiRoute* Route, float StartCm, float EndCm, float RolloutEndSpeedKt);

	UFUNCTION(BlueprintPure, Category = "ATC|Aircraft")
	bool IsLanding() const { return bLanding; }

	/** Missed approach: climbing away because its runway exit was not free. */
	UFUNCTION(BlueprintPure, Category = "ATC|Aircraft")
	bool IsGoingAround() const { return bGoingAround; }

	/** Final approach starts this far before the touchdown point (from about 300 m up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Aircraft", meta = (Units = "m"))
	float ApproachDistanceM = 6000.f;

	/** Rotation speed. Light twins rotate around 140 kt, heavy long-haul aircraft around 160 kt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Aircraft")
	float RotateSpeedKt = 145.f;

	UFUNCTION(BlueprintCallable, Category = "ATC|Aircraft")
	void SetAircraftType(UAircraftTypeData* NewType);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ATC|Aircraft")
	TObjectPtr<UAircraftTypeData> AircraftType;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Aircraft")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Only visible with the placeholder (type without a mesh). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Aircraft")
	TObjectPtr<UStaticMeshComponent> Wings;

	/** The tug attaches here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Aircraft")
	TObjectPtr<USceneComponent> NoseGear;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Aircraft")
	TObjectPtr<UAircraftMovementComponent> Movement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Aircraft")
	TObjectPtr<UTaxiControllerComponent> TaxiController;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Aircraft")
	TObjectPtr<UPushbackControllerComponent> PushbackController;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Aircraft")
	TObjectPtr<UAircraftIdentityComponent> Identity;

	/**
	 * Placed by hand on a taxi route: in the editor the aircraft snaps onto the spline (main gear on the line, nose
	 * along the route); at Play it starts there. Empty = an ordinary aircraft (stand or spawn manager).
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "ATC|Placement")
	TObjectPtr<ATaxiRoute> PlacedOnRoute;

	/** Start taxiing at Play; otherwise it waits on the route for Cmd.Taxi. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "ATC|Placement", meta = (EditCondition = "PlacedOnRoute != nullptr"))
	bool bTaxiOnBeginPlay = true;

	/** Stop or waypoint name where the taxi ends. Empty = end of the route. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "ATC|Placement", meta = (EditCondition = "PlacedOnRoute != nullptr"))
	FName PlacedEndStop;

	/** Ground footprint: tail and nose on the centreline (world) and half the wingspan (cm). */
	void GetFootprint(FVector& OutTail, FVector& OutNose, float& OutHalfSpan) const;

	/** Main gear (actor origin) to the nose tip, cm. */
	UFUNCTION(BlueprintPure, Category = "ATC|Aircraft")
	float GetNoseOffsetCm() const;

	/** Puts the main gear on the route at Distance with the nose along the route (no movement yet). */
	UFUNCTION(BlueprintCallable, Category = "ATC|Aircraft")
	void PlaceOnRoute(ATaxiRoute* Route, float DistanceCm);

	/**
	 * Takes the place of the parked aircraft models of the map on its stand (static meshes): they are hidden while this
	 * aircraft is around, and shown again when this one is removed before it left the stand.
	 */
	void StandInFor(const TArray<TObjectPtr<AActor>>& Models);

	// IAircraftCommands
	virtual bool RequestPushback_Implementation(const FPushbackRequest& Request) override;
	virtual bool StartTaxi_Implementation(ATaxiRoute* Route) override;
	virtual bool HoldPosition_Implementation() override;
	virtual bool ContinueTaxi_Implementation() override;
	virtual void SetSpeedLimit_Implementation(float Knots) override;

	// IAircraftInfo
	virtual FName GetCallsign_Implementation() const override;
	virtual UAircraftTypeData* GetAircraftType_Implementation() const override;
	virtual FText GetStatusText_Implementation() const override;

private:
	void ApplyType();
	void Report(const FGameplayTag& EventTag, const FString& Detail);

	UFUNCTION()
	void HandleTaxiState(ETaxiState OldState, ETaxiState NewState);

	UFUNCTION()
	void HandleStopReached(const FTaxiStop& Stop);

	UFUNCTION()
	void HandleWaypoint(FName Waypoint);

	UFUNCTION()
	void HandleSpeedZone(float MaxSpeedKt);

	UFUNCTION()
	void HandleRouteFinished();

	UFUNCTION()
	void HandlePushbackState(EPushbackState OldState, EPushbackState NewState);

	UFUNCTION()
	void HandleTrafficAhead(AActor* Blocker);

	void StartPlacedTaxi();
	void Remove();
	/** Gives back the taxiway it reserved: parked, airborne or gone. */
	void LeaveTaxiNetwork();
	void TickTakeoff(float DeltaSeconds);
	void TickLanding(float DeltaSeconds);

	/**
	 * Claims the runway behind the landing route's start and the exit up to where the tail is clear of the runway, on the
	 * landing route or one of its parallel lanes, whichever is free (ExitLane).
	 */
	bool ReserveRunwayExit();
	void StartGoAround();

	bool bTakingOff = false;
	float TakeoffSpeedCms = 0.f;
	float TakeoffSeconds = 0.f;
	float PitchDeg = 0.f;

	bool bLanding = false;
	bool bExitReserved = false;
	bool bGoingAround = false;
	TWeakObjectPtr<AActor> ExitBlocker;
	TArray<TWeakObjectPtr<AActor>> StoodInFor;
	TWeakObjectPtr<ATaxiRoute> LandingRoute;
	/** The lane its exit is reserved on: it taxis in on that one. */
	TWeakObjectPtr<ATaxiRoute> ExitLane;
	float LandingStartCm = 0.f;
	float LandingEndCm = -1.f;
	FVector TouchdownPoint = FVector::ZeroVector;
	FVector LandingDirection = FVector::ForwardVector;
	/** Distance to the touchdown point; negative = that far down the runway on the rollout. */
	float LandingToGoCm = 0.f;
	float LandingSpeedCms = 0.f;
	float TouchdownSpeedCms = 0.f;
	float ApproachSpeedCms = 0.f;
	float RolloutCm = 0.f;
	float RolloutDecelCms2 = 0.f;
	float RolloutEndSpeedCms = 0.f;
};
