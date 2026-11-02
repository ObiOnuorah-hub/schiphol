#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TaxiRoute.h"
#include "TaxiControllerComponent.generated.h"

class UAircraftMovementComponent;
class UAircraftTypeData;

UENUM(BlueprintType)
enum class ETaxiState : uint8
{
	Idle,
	Taxiing,
	/** Slowing down for a stop, the end of the route or HoldPosition. */
	Braking,
	/** Standing still at a stop or after HoldPosition. */
	Holding,
	Arrived
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTaxiStateChanged, ETaxiState, OldState, ETaxiState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTaxiStopReached, const FTaxiStop&, Stop);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTaxiWaypointPassed, FName, Waypoint);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpeedZoneEntered, float, MaxSpeedKt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRouteFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTrafficAhead, AActor*, Blocker);

/**
 * Drives an aircraft over a taxi route. Every frame it picks the lowest of seven limits (type, zone, turn, stop,
 * command, traffic ahead, end of the reserved stretch) and hands that to the movement component. Contains the taxi
 * state machine. The aircraft stays on the centre of its route; traffic only changes how fast it goes.
 */
UCLASS(ClassGroup = (ATC), meta = (BlueprintSpawnableComponent))
class ATCAIRCRAFT_API UTaxiControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTaxiControllerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Start at the point of the route closest to the aircraft and taxi to the end of the route. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Taxi")
	bool StartTaxi(ATaxiRoute* NewRoute);

	/**
	 * Start at StartDistanceCm (< 0: closest to the aircraft) and arrive at EndDistanceCm (< 0: end of the route).
	 * Stops beyond the end are skipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "ATC|Taxi")
	bool StartTaxiBetween(ATaxiRoute* NewRoute, float StartDistanceCm, float EndDistanceCm);

	/**
	 * Like StartTaxiBetween, on Lane (NewRoute or one of its Alternatives) without letting the taxi network choose: a
	 * landing keeps the lane its runway exit was reserved on. Start and end are given on NewRoute.
	 */
	UFUNCTION(BlueprintCallable, Category = "ATC|Taxi")
	bool StartTaxiOnLane(ATaxiRoute* NewRoute, ATaxiRoute* Lane, float StartDistanceCm, float EndDistanceCm);

	/** Taxi over several routes after each other (for example taxiway pieces): they are joined into one route for this trip. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Taxi")
	bool StartTaxiVia(const TArray<ATaxiRoute*>& Legs);

	/** Where this taxi ends on the route (cm). */
	UFUNCTION(BlueprintPure, Category = "ATC|Taxi")
	float GetEndDistance() const;

	/** Aircraft that holds this one up right now (ahead of it, or holding the stretch it needs), if any. */
	UFUNCTION(BlueprintPure, Category = "ATC|Taxi")
	AActor* GetTrafficAhead() const { return TrafficAhead.Get(); }

	/** End of the stretch reserved for this aircraft (cm along the route, for its nose); BIG_NUMBER = no limit. */
	UFUNCTION(BlueprintPure, Category = "ATC|Taxi")
	float GetAuthority() const { return Authority; }

	/** Stop as soon as possible with normal braking. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Taxi")
	bool HoldPosition();

	/** Clearance: continue after a stop or after HoldPosition. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Taxi")
	bool ContinueTaxi();

	UFUNCTION(BlueprintCallable, Category = "ATC|Taxi")
	void Abort();

	/** Speed instruction from the controller. 0 = no limit. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Taxi")
	void SetCommandedSpeedKt(float Knots) { CommandedSpeedKt = FMath::Max(Knots, 0.f); }

	UFUNCTION(BlueprintPure, Category = "ATC|Taxi")
	ETaxiState GetState() const { return State; }

	/** Allowed speed in a turn: v = sqrt(a / curvature). */
	static float CurveSpeed(float CurvatureInvCm, float MaxLateralAccelCms2);

	/** Lowest allowed speed within braking distance ahead, so the aircraft slows down before a turn, not in it. */
	float GetCurveSpeedLimit(float AtDistance, float CurrentSpeed) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Taxi")
	TObjectPtr<const UAircraftTypeData> AircraftType;

	/** The route it drives: PlannedRoute or the parallel lane the taxi network chose for it. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "ATC|Taxi")
	TObjectPtr<ATaxiRoute> Route;

	/** The route it was sent on. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "ATC|Taxi")
	TObjectPtr<ATaxiRoute> PlannedRoute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Taxi")
	float CommandedSpeedKt = 0.f;

	/** Continue on its own after HoldTime at stops that need no clearance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Taxi")
	bool bAutoContinue = true;

	/** Step for measuring turns ahead (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Taxi")
	float SampleStepCm = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Taxi")
	float MinLookAheadCm = 2000.f;

	/** Within this distance of a stop the aircraft counts as stopped on it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Taxi")
	float StopToleranceCm = 10.f;

	/** Keep distance to other aircraft and only drive on reserved taxiway (see Project Settings > ATC Sim > Traffic). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Taxi")
	bool bKeepSeparation = true;

	/** Fired when another aircraft starts or stops holding this one up (nullptr = clear again). */
	UPROPERTY(BlueprintAssignable, Category = "ATC|Taxi")
	FOnTrafficAhead OnTrafficAhead;

	UPROPERTY(BlueprintAssignable, Category = "ATC|Taxi")
	FOnTaxiStateChanged OnTaxiStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "ATC|Taxi")
	FOnTaxiStopReached OnStopReached;

	UPROPERTY(BlueprintAssignable, Category = "ATC|Taxi")
	FOnTaxiWaypointPassed OnWaypointPassed;

	UPROPERTY(BlueprintAssignable, Category = "ATC|Taxi")
	FOnSpeedZoneEntered OnSpeedZoneEntered;

	UPROPERTY(BlueprintAssignable, Category = "ATC|Taxi")
	FOnRouteFinished OnRouteFinished;

private:
	bool TryTransition(ETaxiState NewState);
	static bool CanTransition(ETaxiState From, ETaxiState To);
	void TickDriving(float DeltaTime);
	void TickHolding(float DeltaTime);

	float GetMaxSpeedCms() const;
	float GetDecelerationCms2() const;
	float GetLateralAccelCms2() const;

	/**
	 * Speed that still stops the nose the separation distance short of the aircraft ahead, and before the end of the
	 * reserved stretch; BIG_NUMBER when the path is clear. OutStopRoomCm: how much room that leaves to stop in.
	 */
	float GetTrafficSpeedLimit(float DeltaTime, float& OutStopRoomCm);

	/** Asks the taxi network to keep the reservation up to date, LookAheadCm past the nose. */
	void UpdateClaim(float LookAheadCm, float SeparationCm);

	float GetNoseOffsetCm() const;
	float GetLengthCm() const;

	UPROPERTY(Transient)
	TObjectPtr<UAircraftMovementComponent> Movement;

	TWeakObjectPtr<AActor> TrafficAhead;
	float EndDistance = -1.f;

	/** The traffic picture is refreshed five times a second; in between the gap shrinks by the distance covered. */
	float TrafficRefresh = 0.f;
	TWeakObjectPtr<AActor> CachedBlocker;
	float CachedGap = BIG_NUMBER;
	float CachedAtDistance = 0.f;

	/** Reserved stretch: the nose may go up to Authority; ClaimBlocker holds (or queued earlier for) the next piece. */
	float Authority = BIG_NUMBER;
	TWeakObjectPtr<AActor> ClaimBlocker;

	/** How long nobody has held this aircraft up: "clear" is only reported after 2 s, not at every short gap. */
	float ClearSeconds = 0.f;

	/** Route made by StartTaxiVia; removed when the aircraft is done with it. */
	TWeakObjectPtr<ATaxiRoute> JoinedRoute;

	ETaxiState State = ETaxiState::Idle;
	int32 NextStopIndex = 0;
	int32 NextWaypointIndex = 0;
	int32 CurrentZoneIndex = INDEX_NONE;
	bool bHoldCommanded = false;
	bool bWaitingForClearance = false;
	float HoldTimer = 0.f;
};
