#include "TaxiControllerComponent.h"
#include "AircraftCommands.h"
#include "AircraftMovementComponent.h"
#include "AircraftTypeData.h"
#include "ATCSimSettings.h"
#include "ATCUnits.h"
#include "TaxiNetworkSubsystem.h"
#include "TrafficSubsystem.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"

UTaxiControllerComponent::UTaxiControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UTaxiControllerComponent::BeginPlay()
{
	Super::BeginPlay();

	// The controller sets the target speed first, then the movement moves
	Movement = GetOwner()->FindComponentByClass<UAircraftMovementComponent>();
	if (Movement)
	{
		Movement->AddTickPrerequisiteComponent(this);
	}
	else
	{
		UE_LOG(LogATC, Error, TEXT("%s: TaxiController needs an AircraftMovementComponent."), *GetOwner()->GetName());
	}
}

void UTaxiControllerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(Route))
	{
		Route->ClearGuidance(this);
	}
	if (UTaxiNetworkSubsystem* Network = GetWorld() ? GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>() : nullptr)
	{
		Network->ReleaseClaim(GetOwner());
	}
	if (JoinedRoute.IsValid())
	{
		JoinedRoute->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

float UTaxiControllerComponent::GetNoseOffsetCm() const
{
	return AircraftType ? AircraftType->GetNoseOffsetCm() : 1560.f + 0.12f * 3950.f;
}

float UTaxiControllerComponent::GetLengthCm() const
{
	return AircraftType ? AircraftType->GetLengthCm() : 3950.f;
}

float UTaxiControllerComponent::GetEndDistance() const
{
	if (!Route)
	{
		return 0.f;
	}
	return EndDistance >= 0.f ? FMath::Min(EndDistance, Route->GetLength()) : Route->GetLength();
}

float UTaxiControllerComponent::GetMaxSpeedCms() const
{
	return AircraftType ? AircraftType->GetMaxTaxiSpeedCms() : 30.f * ATCUnits::KnotsToCms;
}

float UTaxiControllerComponent::GetDecelerationCms2() const
{
	return AircraftType ? AircraftType->GetDecelerationCms2() : 100.f;
}

float UTaxiControllerComponent::GetLateralAccelCms2() const
{
	return AircraftType ? AircraftType->GetMaxLateralAccelCms2() : 50.f;
}

bool UTaxiControllerComponent::CanTransition(ETaxiState From, ETaxiState To)
{
	switch (From)
	{
	case ETaxiState::Idle:    return To == ETaxiState::Taxiing;
	case ETaxiState::Taxiing: return To == ETaxiState::Braking || To == ETaxiState::Holding || To == ETaxiState::Arrived || To == ETaxiState::Idle;
	case ETaxiState::Braking: return To == ETaxiState::Holding || To == ETaxiState::Arrived || To == ETaxiState::Taxiing || To == ETaxiState::Idle;
	case ETaxiState::Holding: return To == ETaxiState::Taxiing || To == ETaxiState::Idle;
	case ETaxiState::Arrived: return To == ETaxiState::Idle;
	}
	return false;
}

bool UTaxiControllerComponent::TryTransition(ETaxiState NewState)
{
	if (NewState == State)
	{
		return true;
	}
	if (!CanTransition(State, NewState))
	{
		UE_LOG(LogATC, Warning, TEXT("%s: taxi state %s -> %s not allowed, ignored."), *GetOwner()->GetName(),
			*UEnum::GetValueAsString(State), *UEnum::GetValueAsString(NewState));
		return false;
	}

	const ETaxiState OldState = State;
	State = NewState;
	if (Route && (NewState == ETaxiState::Idle || NewState == ETaxiState::Arrived))
	{
		Route->ClearGuidance(this);
	}
	OnTaxiStateChanged.Broadcast(OldState, NewState);
	return true;
}

bool UTaxiControllerComponent::StartTaxi(ATaxiRoute* NewRoute)
{
	return StartTaxiBetween(NewRoute, -1.f, -1.f);
}

bool UTaxiControllerComponent::StartTaxiBetween(ATaxiRoute* NewRoute, float StartDistanceCm, float EndDistanceCm)
{
	// A route with parallel lanes: the taxi network picks the lane
	ATaxiRoute* Lane = NewRoute;
	UTaxiNetworkSubsystem* Network = bKeepSeparation && GetWorld() ? GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>() : nullptr;
	if (Network && NewRoute && Movement && NewRoute->Alternatives.Num() > 0)
	{
		const FVector Start = StartDistanceCm >= 0.f ? NewRoute->GetLocationAt(StartDistanceCm) : GetOwner()->GetActorLocation();
		FString Reason;
		Lane = Network->ChooseLane(GetOwner(), NewRoute, Start, &Reason);
		UE_LOG(LogATC, Display, TEXT("%s takes %s"), GetOwner()->Implements<UAircraftInfo>()
			? *IAircraftInfo::Execute_GetCallsign(GetOwner()).ToString() : *GetOwner()->GetName(), *Reason);
	}
	return StartTaxiOnLane(NewRoute, Lane, StartDistanceCm, EndDistanceCm);
}

bool UTaxiControllerComponent::StartTaxiOnLane(ATaxiRoute* NewRoute, ATaxiRoute* Lane, float StartDistanceCm, float EndDistanceCm)
{
	if (!NewRoute || !Lane || !Movement)
	{
		return false;
	}
	if (State == ETaxiState::Arrived || State == ETaxiState::Holding || State == ETaxiState::Taxiing || State == ETaxiState::Braking)
	{
		TryTransition(ETaxiState::Idle);
	}
	if (State != ETaxiState::Idle)
	{
		return false;
	}

	// On a parallel lane start and end stay at the same places
	PlannedRoute = NewRoute;
	if (Lane != NewRoute)
	{
		const FVector Start = StartDistanceCm >= 0.f ? NewRoute->GetLocationAt(StartDistanceCm) : GetOwner()->GetActorLocation();
		StartDistanceCm = StartDistanceCm >= 0.f ? UAircraftMovementComponent::GetDistanceClosestTo(Lane->Spline, Start) : StartDistanceCm;
		EndDistanceCm = EndDistanceCm >= 0.f ? UAircraftMovementComponent::GetDistanceClosestTo(Lane->Spline, NewRoute->GetLocationAt(EndDistanceCm)) : EndDistanceCm;
	}
	if (JoinedRoute.IsValid() && JoinedRoute.Get() != Lane)
	{
		JoinedRoute->Destroy();
		JoinedRoute = nullptr;
	}

	Route = Lane;
	EndDistance = EndDistanceCm;
	const float StartDistance = StartDistanceCm >= 0.f ? FMath::Min(StartDistanceCm, Route->GetLength())
		: UAircraftMovementComponent::GetDistanceClosestTo(Route->Spline, GetOwner()->GetActorLocation());
	Movement->SetPath(Route->Spline, ETravelDirection::Forward, true, StartDistance);
	TrafficAhead = nullptr;
	CachedBlocker = nullptr;
	CachedGap = BIG_NUMBER;
	TrafficRefresh = 0.f;
	Authority = BIG_NUMBER;
	ClaimBlocker = nullptr;

	const TArray<FTaxiStop>& Stops = Route->GetAllStops();
	NextStopIndex = 0;
	while (NextStopIndex < Stops.Num() && Stops[NextStopIndex].Distance <= StartDistance + StopToleranceCm)
	{
		++NextStopIndex;
	}
	NextWaypointIndex = 0;
	while (NextWaypointIndex < Route->Waypoints.Num() && Route->Waypoints[NextWaypointIndex].Distance <= StartDistance)
	{
		++NextWaypointIndex;
	}
	CurrentZoneIndex = INDEX_NONE;
	bHoldCommanded = false;
	bWaitingForClearance = false;

	return TryTransition(ETaxiState::Taxiing);
}

bool UTaxiControllerComponent::StartTaxiVia(const TArray<ATaxiRoute*>& InLegs)
{
	ATaxiRoute* Joined = ATaxiRoute::JoinRoutes(this, InLegs);
	if (!Joined)
	{
		return false;
	}
	ATaxiRoute* Old = JoinedRoute.Get();
	JoinedRoute = Joined;
	const bool bStarted = StartTaxi(Joined);
	if (Old && Old != Route)
	{
		Old->Destroy();
	}
	return bStarted;
}

bool UTaxiControllerComponent::HoldPosition()
{
	if (State != ETaxiState::Taxiing && State != ETaxiState::Braking)
	{
		return false;
	}
	bHoldCommanded = true;
	return TryTransition(ETaxiState::Braking);
}

bool UTaxiControllerComponent::ContinueTaxi()
{
	if (State == ETaxiState::Braking && bHoldCommanded)
	{
		// Hold cancelled before standing still
		bHoldCommanded = false;
		return TryTransition(ETaxiState::Taxiing);
	}
	if (State == ETaxiState::Taxiing || State == ETaxiState::Braking)
	{
		// Clearance before reaching the stop ("cleared to line up" while still taxiing): drive through it
		const TArray<FTaxiStop>& Stops = Route->GetAllStops();
		if (Stops.IsValidIndex(NextStopIndex) && Stops[NextStopIndex].bRequiresClearance)
		{
			++NextStopIndex;
			return TryTransition(ETaxiState::Taxiing);
		}
		return false;
	}
	if (State != ETaxiState::Holding)
	{
		return false;
	}

	if (bHoldCommanded)
	{
		bHoldCommanded = false;
	}
	else
	{
		++NextStopIndex;
	}
	bWaitingForClearance = false;
	return TryTransition(ETaxiState::Taxiing);
}

void UTaxiControllerComponent::Abort()
{
	if (Movement)
	{
		Movement->StopImmediately();
	}
	TryTransition(ETaxiState::Idle);
}

float UTaxiControllerComponent::CurveSpeed(float CurvatureInvCm, float MaxLateralAccelCms2)
{
	return CurvatureInvCm > KINDA_SMALL_NUMBER ? FMath::Sqrt(MaxLateralAccelCms2 / CurvatureInvCm) : BIG_NUMBER;
}

float UTaxiControllerComponent::GetCurveSpeedLimit(float AtDistance, float CurrentSpeed) const
{
	const float Decel = GetDecelerationCms2();
	const float BrakeDistance = CurrentSpeed * CurrentSpeed / (2.f * Decel) + MinLookAheadCm;
	const float LateralAccel = GetLateralAccelCms2();
	float Limit = GetMaxSpeedCms();

	// The route's own table: pieces at fixed places along the route, so the limit does not hunt as the aircraft moves on
	const float Step = FMath::Max(SampleStepCm, 50.f);
	const TArray<float>& Curvature = Route->GetCurvatureTable(Step);
	for (int32 Index = FMath::Max(FMath::FloorToInt(AtDistance / Step), 0); Index < Curvature.Num(); ++Index)
	{
		const float Ahead = FMath::Max(Index * Step - AtDistance, 0.f);
		if (Ahead >= BrakeDistance)
		{
			break;
		}
		const float TurnSpeed = CurveSpeed(Curvature[Index], LateralAccel);
		if (TurnSpeed < Limit)
		{
			// Speed allowed now = turn speed plus what can still be braked away before that point: v^2 = v_turn^2 + 2*a*d
			Limit = FMath::Min(Limit, FMath::Sqrt(TurnSpeed * TurnSpeed + 2.f * Decel * Ahead));
		}
	}
	return Limit;
}

void UTaxiControllerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Route || !Movement)
	{
		return;
	}
	if (State == ETaxiState::Taxiing || State == ETaxiState::Braking)
	{
		TickDriving(DeltaTime);
	}
	else if (State == ETaxiState::Holding)
	{
		TickHolding(DeltaTime);
	}
	if (State == ETaxiState::Taxiing || State == ETaxiState::Braking || State == ETaxiState::Holding)
	{
		Route->ShowGuidance(this, Movement->Distance, NextStopIndex);
	}
}

float UTaxiControllerComponent::GetTrafficSpeedLimit(float DeltaTime, float& OutStopRoomCm)
{
	OutStopRoomCm = BIG_NUMBER;
	UTrafficSubsystem* Traffic = bKeepSeparation && GetWorld() ? GetWorld()->GetSubsystem<UTrafficSubsystem>() : nullptr;
	if (!Traffic)
	{
		return BIG_NUMBER;
	}
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	const float Separation = Settings->TaxiSeparationM * ATCUnits::MetersToCm;
	const float Decel = GetDecelerationCms2();

	TrafficRefresh -= DeltaTime;
	if (TrafficRefresh <= 0.f)
	{
		TrafficRefresh = 0.2f;
		const float LookAhead = Separation + Movement->Speed * Movement->Speed / (2.f * Decel) + Settings->TrafficLookAheadM * ATCUnits::MetersToCm;
		CachedBlocker = Traffic->FindTrafficAhead(GetOwner(), LookAhead, CachedGap);
		CachedAtDistance = Movement->Distance;
		// The reserved stretch reaches as far as the look for traffic: nobody else drives into it
		UpdateClaim(LookAhead, Separation);
	}
	AActor* Blocker = CachedBlocker.Get();
	const float Gap = CachedGap - (Movement->Distance - CachedAtDistance);
	float Limit = Blocker ? FMath::Sqrt(2.f * Decel * FMath::Max(Gap - Separation, 0.f)) : BIG_NUMBER;
	if (Blocker)
	{
		OutStopRoomCm = Gap - Separation;
	}

	// Never past the end of the reserved stretch: the nose stops 2 m before it
	const float Reserved = Authority - (Movement->Distance + GetNoseOffsetCm());
	if (Authority < BIG_NUMBER)
	{
		Limit = FMath::Min(Limit, FMath::Sqrt(2.f * Decel * FMath::Max(Reserved - 200.f, 0.f)));
		OutStopRoomCm = FMath::Min(OutStopRoomCm, Reserved - 200.f);
	}

	// Report only traffic that is close enough to matter, not everything within look-ahead
	AActor* Relevant = Blocker && Gap < Separation + 10000.f ? Blocker : nullptr;
	if (!Relevant && ClaimBlocker.IsValid() && Reserved < 10000.f)
	{
		Relevant = ClaimBlocker.Get();
	}
	ClearSeconds = Relevant ? 0.f : ClearSeconds + DeltaTime;
	if (Relevant ? Relevant != TrafficAhead.Get() : (TrafficAhead.IsValid() && ClearSeconds > 2.f))
	{
		TrafficAhead = Relevant;
		OnTrafficAhead.Broadcast(Relevant);
	}
	return Limit;
}

void UTaxiControllerComponent::UpdateClaim(float LookAheadCm, float SeparationCm)
{
	UTaxiNetworkSubsystem* Network = GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>();
	if (!Network)
	{
		Authority = BIG_NUMBER;
		return;
	}
	const float Nose = Movement->Distance + GetNoseOffsetCm();
	FTaxiClaimRequest Request;
	Request.Route = Route;
	Request.FrontCm = Nose;
	Request.RearCm = Nose - GetLengthCm();
	Request.WantedCm = Nose + LookAheadCm;
	// Not past a stop where it will wait for a clearance, nor past the end of this taxi
	Request.LimitCm = GetEndDistance() + GetNoseOffsetCm();
	const TArray<FTaxiStop>& Stops = Route->GetAllStops();
	for (int32 Index = NextStopIndex; Index < Stops.Num(); ++Index)
	{
		if (Stops[Index].bRequiresClearance)
		{
			Request.LimitCm = FMath::Min(Request.LimitCm, Stops[Index].Distance + GetNoseOffsetCm());
			break;
		}
	}
	Request.LimitCm += 100.f;
	Request.HalfSpanCm = AircraftType ? AircraftType->GetHalfSpanCm() : 1790.f;
	Request.LengthCm = GetLengthCm();
	Request.NoseOffsetCm = GetNoseOffsetCm();
	Request.RoomAheadCm = CachedBlocker.IsValid() ? FMath::Max(CachedGap - SeparationCm, 0.f) : BIG_NUMBER;
	// Standing at its distance behind the aircraft ahead: it waits for that one
	Request.HeldBy = CachedBlocker.IsValid() && Movement->Speed < 100.f && CachedGap - SeparationCm < 1000.f ? CachedBlocker.Get() : nullptr;
	AActor* Blocker = nullptr;
	Authority = Network->UpdateClaim(GetOwner(), Request, Blocker);
	ClaimBlocker = Blocker;
}

void UTaxiControllerComponent::TickDriving(float DeltaTime)
{
	const float D = Movement->Distance;
	const TArray<FTaxiStop>& Stops = Route->GetAllStops();

	// Events along the way
	while (NextWaypointIndex < Route->Waypoints.Num() && Route->Waypoints[NextWaypointIndex].Distance <= D)
	{
		OnWaypointPassed.Broadcast(Route->Waypoints[NextWaypointIndex].Name);
		++NextWaypointIndex;
	}
	const int32 ZoneIndex = Route->GetZoneIndexAt(D);
	if (ZoneIndex != CurrentZoneIndex)
	{
		CurrentZoneIndex = ZoneIndex;
		if (ZoneIndex != INDEX_NONE)
		{
			OnSpeedZoneEntered.Broadcast(Route->SpeedZones[ZoneIndex].MaxSpeedKt);
		}
	}

	// HoldPosition: just brake to zero, then hold where we are
	if (bHoldCommanded)
	{
		Movement->SetTargetSpeed(0.f);
		if (Movement->Speed <= KINDA_SMALL_NUMBER)
		{
			TryTransition(ETaxiState::Holding);
		}
		return;
	}

	// The lowest of the six limits
	float Target = FMath::Min(GetMaxSpeedCms(), Route->GetZoneLimitKt(D) * ATCUnits::KnotsToCms);
	if (CommandedSpeedKt > 0.f)
	{
		Target = FMath::Min(Target, CommandedSpeedKt * ATCUnits::KnotsToCms);
	}
	Target = FMath::Min(Target, GetCurveSpeedLimit(D, Movement->Speed));

	// A stop beyond the end of this taxi does not count: the aircraft arrives at the end
	const float End = GetEndDistance();
	const bool bHasStop = Stops.IsValidIndex(NextStopIndex) && Stops[NextStopIndex].Distance < End - StopToleranceCm;
	const float StopDistance = bHasStop ? Stops[NextStopIndex].Distance : End;
	const float Remaining = FMath::Max(StopDistance - D, 0.f);
	Target = FMath::Min(Target, FMath::Sqrt(2.f * GetDecelerationCms2() * Remaining));
	if (Remaining > StopToleranceCm)
	{
		// Creep the last centimetres, otherwise sqrt(2ad) never quite reaches the line
		Target = FMath::Max(Target, FMath::Min(20.f, Remaining / FMath::Max(DeltaTime, 0.001f)));
	}
	float StopRoom = BIG_NUMBER;
	Target = FMath::Min(Target, GetTrafficSpeedLimit(DeltaTime, StopRoom));
	Movement->SetTargetSpeed(Target);

	// Normally the planned braking of the type; when traffic or the end of the reservation needs more (just after a
	// landing, or traffic seen late) it brakes harder, up to three times that, like a real aircraft can
	const float Planned = GetDecelerationCms2();
	const float Needed = StopRoom < BIG_NUMBER ? FMath::Square(Movement->Speed) / (2.f * FMath::Max(StopRoom, 50.f)) : 0.f;
	Movement->DecelerationCms2 = FMath::Clamp(Needed * 1.2f, Planned, 3.f * Planned);

	if (State == ETaxiState::Taxiing && Remaining <= Movement->GetBrakingDistance() + 500.f)
	{
		TryTransition(ETaxiState::Braking);
	}
	else if (State == ETaxiState::Braking && Remaining > Movement->GetBrakingDistance() + 1500.f)
	{
		TryTransition(ETaxiState::Taxiing);
	}

	if (Remaining > StopToleranceCm)
	{
		return;
	}

	Movement->StopImmediately();
	if (bHasStop)
	{
		const FTaxiStop& Stop = Stops[NextStopIndex];
		bWaitingForClearance = Stop.bRequiresClearance || !bAutoContinue;
		HoldTimer = Stop.HoldTime;
		TryTransition(ETaxiState::Holding);
		OnStopReached.Broadcast(Stop);
	}
	else
	{
		TryTransition(ETaxiState::Arrived);
		OnRouteFinished.Broadcast();
	}
}

void UTaxiControllerComponent::TickHolding(float DeltaTime)
{
	Movement->SetTargetSpeed(0.f);
	if (bHoldCommanded || bWaitingForClearance)
	{
		return;
	}

	HoldTimer -= DeltaTime;
	if (HoldTimer <= 0.f)
	{
		ContinueTaxi();
	}
}
