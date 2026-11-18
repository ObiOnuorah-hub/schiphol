#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AircraftCommands.h"
#include "TaxiControllerComponent.h"
#include "ATCAircraft.generated.h"

class UAircraftIdentityComponent;
class UAircraftMovementComponent;
class UAircraftTypeData;
class UATCSoundComponent;
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
	 * ApproachRoute: first fly the final of that route's runway, then sidestep in a banked turn onto Route's runway short
	 * of its threshold (Schiphol: 27 final -> Oostbaan 22).
	 */
	void StartLanding(ATaxiRoute* Route, float StartCm, float EndCm, float RolloutEndSpeedKt, const ATaxiRoute* ApproachRoute = nullptr);

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

	/** Engine sound made in code: off at the stand, idle while taxiing, full power on takeoff and go-around. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Aircraft")
	TObjectPtr<UATCSoundComponent> EngineSound;

	/** Engine power for the sound now: 0 off, 0.3 idle, 1 takeoff. */
	float GetEngineThrust() const;

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
	/** Sets the engine sound to GetEngineThrust (on every event and at touchdown). */
	void UpdateEngineSound();

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
	/** Climb angle of the flight path (not the nose): 0 until the main gear leaves the runway, then up towards pitch - 6. */
	float FlightPathDeg = 0.f;
	bool bLiftedOff = false;
	/** Seconds since the main gear touched down (-1: in the air): the nose stays up a moment before it comes down. */
	float SinceTouchdown = -1.f;

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
	/** Bank angle (roll), right wing down positive: follows the turn (tan = v^2 / (g R)) at a few degrees a second. */
	float RollDeg = 0.f;

	/**
	 * Hand flying and wind on the final, set per flight in StartLanding (from the callsign, so a run repeats): a slow
	 * wander around the centre line and glide path that dies out towards touchdown, and a crosswind (+ from the right)
	 * that rocks the wings and makes the wander bigger (no crab: the nose stays along the way it flies).
	 */
	float FinalSeconds = 0.f;
	/** Takeoff roll and climb-out along the runway centre line (from the route's lineup straight), not the nose direction. */
	bool bTakeoffOnLine = false;
	/** Takeoff roll acceleration (cm/s2): by wake category and a little per flight (weight). */
	float TakeoffAccelCms2 = 200.f;
	/** After takeoff: the turn still to fly (degrees, + right) once above 150 m, as its departure route (SID) asks. */
	float DepartureTurnLeftDeg = 0.f;
	FVector TakeoffLineStart = FVector::ZeroVector;
	FVector TakeoffLineDir = FVector::ForwardVector;
	float WanderLateralCm = 0.f;
	float WanderVerticalCm = 0.f;
	float WanderPhase[6] = {};
	float CrosswindKt = 0.f;

	/**
	 * A sidestep in the air, measured back from the touchdown point: straight on the landing runway's final (BackStraight),
	 * an arc of SidestepRadius (BackArc long, turning TurnSign: +1 right), then the other runway's final outwards.
	 */
	bool bSidestep = false;
	FVector2D SidestepArcStart = FVector2D::ZeroVector;
	FVector2D SidestepCenter = FVector2D::ZeroVector;
	FVector2D FirstFinal = FVector2D::UnitX();
	float SidestepRadius = 0.f;
	float SidestepTurnRad = 0.f;
	float SidestepTurnSign = 1.f;
	float BackStraight = 0.f;
	float BackArc = 0.f;
	/** Ground point, heading and signed curvature (1/cm, + right) of the sidestep approach this far before touchdown. */
	void SidestepAt(float ToGoCm, FVector2D& OutPoint, FVector2D& OutHeading, float& OutCurvature) const;
	/** Builds the sidestep from the approach route's final to the landing direction; false: no sensible turn. */
	bool PlanSidestep(const ATaxiRoute* ApproachRoute);
};
