#include "ATCAircraft.h"
#include "AircraftIdentityComponent.h"
#include "AircraftMovementComponent.h"
#include "AircraftTypeData.h"
#include "ATCGameplayTags.h"
#include "ATCUnits.h"
#include "PushbackControllerComponent.h"
#include "Stand.h"
#include "TaxiControllerComponent.h"
#include "TaxiNetworkSubsystem.h"
#include "TrafficSubsystem.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AATCAircraft::AATCAircraft()
{
	// Ticks only during the takeoff: on the ground the components move the aircraft
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// Traffic is spawned at runtime and moves around: never streamed out by World Partition (an editor-only setting)
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("MainGear"));
	RootComponent->SetMobility(EComponentMobility::Movable);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Wings = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Wings"));
	Wings->SetupAttachment(RootComponent);
	Wings->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Body->SetStaticMesh(Cube.Object);
		Wings->SetStaticMesh(Cube.Object);
	}

	NoseGear = CreateDefaultSubobject<USceneComponent>(TEXT("NoseGear"));
	NoseGear->SetupAttachment(RootComponent);

	Movement = CreateDefaultSubobject<UAircraftMovementComponent>(TEXT("Movement"));
	TaxiController = CreateDefaultSubobject<UTaxiControllerComponent>(TEXT("TaxiController"));
	PushbackController = CreateDefaultSubobject<UPushbackControllerComponent>(TEXT("PushbackController"));
	Identity = CreateDefaultSubobject<UAircraftIdentityComponent>(TEXT("Identity"));
}

void AATCAircraft::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyType();

	// Dragged in the editor along a route: stay on the line
	if (PlacedOnRoute && GetWorld() && !GetWorld()->IsGameWorld())
	{
		PlaceOnRoute(PlacedOnRoute, UAircraftMovementComponent::GetDistanceClosestTo(PlacedOnRoute->Spline, GetActorLocation()));
	}
}

float AATCAircraft::GetNoseOffsetCm() const
{
	return AircraftType ? AircraftType->GetNoseOffsetCm() : 1560.f + 0.12f * 3950.f;
}

void AATCAircraft::LeaveTaxiNetwork()
{
	if (UTaxiNetworkSubsystem* Network = GetWorld() ? GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>() : nullptr)
	{
		Network->ReleaseClaim(this);
	}
}

void AATCAircraft::GetFootprint(FVector& OutTail, FVector& OutNose, float& OutHalfSpan) const
{
	const float Length = (AircraftType ? AircraftType->LengthM : 39.5f) * ATCUnits::MetersToCm;
	const FVector Forward = GetActorForwardVector();
	OutNose = GetActorLocation() + Forward * GetNoseOffsetCm();
	OutTail = OutNose - Forward * Length;
	OutHalfSpan = (AircraftType ? AircraftType->WingspanM : 35.8f) * ATCUnits::MetersToCm * 0.5f;
}

void AATCAircraft::PlaceOnRoute(ATaxiRoute* Route, float DistanceCm)
{
	if (!Route)
	{
		return;
	}
	const float Wheelbase = AircraftType ? AircraftType->GetWheelbaseCm() : 1560.f;
	const float Length = Route->GetLength();
	const FVector MainGear = Route->Spline->GetLocationAtDistanceAlongSpline(FMath::Clamp(DistanceCm, 0.f, Length), ESplineCoordinateSpace::World);
	FVector Heading = Route->Spline->GetLocationAtDistanceAlongSpline(FMath::Min(DistanceCm + Wheelbase, Length), ESplineCoordinateSpace::World) - MainGear;
	if (Heading.SizeSquared() < 1.f)
	{
		Heading = Route->GetDirectionAt(DistanceCm);
	}
	SetActorLocationAndRotation(MainGear, Heading.Rotation());
}

void AATCAircraft::SetAircraftType(UAircraftTypeData* NewType)
{
	AircraftType = NewType;
	ApplyType();
}

void AATCAircraft::ApplyType()
{
	Movement->ApplyAircraftType(AircraftType);
	TaxiController->AircraftType = AircraftType;
	PushbackController->AircraftType = AircraftType;

	const float Wheelbase = AircraftType ? AircraftType->GetWheelbaseCm() : 1560.f;
	NoseGear->SetRelativeLocation(FVector(Wheelbase, 0.f, 0.f));

	// The airline's own model when the type has one (KLM1427 -> the KLM livery), else the type's model
	UStaticMesh* Model = nullptr;
	FTransform Offset;
	if (const FAircraftLivery* Livery = AircraftType ? AircraftType->FindLivery(Identity->Callsign) : nullptr)
	{
		Model = Livery->Mesh.LoadSynchronous();
		Offset = Livery->MeshOffset;
	}
	if (!Model && AircraftType)
	{
		Model = AircraftType->Mesh.LoadSynchronous();
		Offset = AircraftType->MeshOffset;
	}
	if (Model)
	{
		Body->SetStaticMesh(Model);
		Body->SetRelativeTransform(Offset);
		Wings->SetVisibility(false);
		return;
	}

	// Placeholder: a fuselage box and a wing box with the real length and span, so types are recognisable
	const float Length = (AircraftType ? AircraftType->LengthM : 39.5f) * ATCUnits::MetersToCm;
	const float Span = (AircraftType ? AircraftType->WingspanM : 35.8f) * ATCUnits::MetersToCm;
	const float NoseTip = Wheelbase + 0.12f * Length;
	const float Fuselage = FMath::Max(Span * 0.11f, 380.f);

	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Body->SetStaticMesh(Cube);
	}
	Body->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(NoseTip - Length * 0.5f, 0.f, Fuselage * 0.5f + 150.f),
		FVector(Length, Fuselage, Fuselage) / 100.f));
	Wings->SetVisibility(true);
	Wings->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(Length * 0.05f, 0.f, 260.f),
		FVector(Length * 0.16f, Span, 40.f) / 100.f));
}

void AATCAircraft::BeginPlay()
{
	Super::BeginPlay();
	ApplyType();

	TaxiController->OnTaxiStateChanged.AddDynamic(this, &AATCAircraft::HandleTaxiState);
	TaxiController->OnStopReached.AddDynamic(this, &AATCAircraft::HandleStopReached);
	TaxiController->OnWaypointPassed.AddDynamic(this, &AATCAircraft::HandleWaypoint);
	TaxiController->OnSpeedZoneEntered.AddDynamic(this, &AATCAircraft::HandleSpeedZone);
	TaxiController->OnRouteFinished.AddDynamic(this, &AATCAircraft::HandleRouteFinished);
	TaxiController->OnTrafficAhead.AddDynamic(this, &AATCAircraft::HandleTrafficAhead);
	PushbackController->OnPushbackStateChanged.AddDynamic(this, &AATCAircraft::HandlePushbackState);

	if (UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>())
	{
		Traffic->RegisterAircraft(this);
	}

	// Placed by hand: start next frame, when the route has resolved its stops in its own BeginPlay
	if (PlacedOnRoute)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &AATCAircraft::StartPlacedTaxi);
	}
}

void AATCAircraft::StartPlacedTaxi()
{
	if (!PlacedOnRoute)
	{
		return;
	}
	const float Start = UAircraftMovementComponent::GetDistanceClosestTo(PlacedOnRoute->Spline, GetActorLocation());
	Identity->AssignedTaxiRoute = PlacedOnRoute->RouteTag;
	Movement->SetPath(PlacedOnRoute->Spline, ETravelDirection::Forward, true, Start, true);
	if (bTaxiOnBeginPlay)
	{
		TaxiController->StartTaxiBetween(PlacedOnRoute, Start, PlacedEndStop.IsNone() ? -1.f : PlacedOnRoute->FindNamedDistance(PlacedEndStop));
	}
}

void AATCAircraft::HandleTrafficAhead(AActor* Blocker)
{
	const FName Other = Blocker && Blocker->Implements<UAircraftInfo>() ? IAircraftInfo::Execute_GetCallsign(Blocker) : NAME_None;
	Report(TAG_ATC_Event_Taxi_Traffic, Blocker ? FString::Printf(TEXT("waiting for %s"), *Other.ToString()) : FString(TEXT("clear")));
}

void AATCAircraft::Remove()
{
	Report(TAG_ATC_Event_Traffic_Removed, bGoingAround ? FString(TEXT("went around")) : bTakingOff ? FString(TEXT("departed")) : FString(TEXT("removed")));
	Destroy();
}

void AATCAircraft::StandInFor(const TArray<TObjectPtr<AActor>>& Models)
{
	for (AActor* Model : Models)
	{
		if (Model)
		{
			Model->SetActorHiddenInGame(true);
			StoodInFor.Add(Model);
		}
	}
}

void AATCAircraft::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UTrafficSubsystem>() : nullptr)
	{
		Traffic->UnregisterAircraft(this);
	}
	LeaveTaxiNetwork();
	// Removed while still parked (a scenario clears the airport): the map's own model stands there again
	for (const TWeakObjectPtr<AActor>& Model : StoodInFor)
	{
		if (Model.IsValid() && PushbackController->GetState() == EPushbackState::Parked)
		{
			Model->SetActorHiddenInGame(false);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AATCAircraft::Report(const FGameplayTag& EventTag, const FString& Detail)
{
	UE_LOG(LogATC, Display, TEXT("%s %s %s"), *Identity->Callsign.ToString(), *EventTag.ToString(), *Detail);
	if (UTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UTrafficSubsystem>() : nullptr)
	{
		Traffic->BroadcastAircraftEvent(this, EventTag, Detail);
	}
}

void AATCAircraft::HandleTaxiState(ETaxiState OldState, ETaxiState NewState)
{
	Report(TAG_ATC_Event_Taxi_State, StaticEnum<ETaxiState>()->GetNameStringByValue(static_cast<int64>(NewState)));
}

void AATCAircraft::HandleStopReached(const FTaxiStop& Stop)
{
	Report(TAG_ATC_Event_Taxi_StopReached, FString::Printf(TEXT("%s%s"), *Stop.Name.ToString(), Stop.bRequiresClearance ? TEXT(" (waiting for clearance)") : TEXT("")));
}

void AATCAircraft::HandleWaypoint(FName Waypoint)
{
	Report(TAG_ATC_Event_Taxi_Waypoint, Waypoint.ToString());
}

void AATCAircraft::HandleSpeedZone(float MaxSpeedKt)
{
	Report(TAG_ATC_Event_Taxi_SpeedZone, FString::Printf(TEXT("%.0f kt"), MaxSpeedKt));
}

void AATCAircraft::HandleRouteFinished()
{
	Report(TAG_ATC_Event_Taxi_Arrived, TaxiController->Route ? TaxiController->Route->RouteTag.ToString() : FString());
	if (Identity->bRemoveAtRouteEnd)
	{
		// Lined up: wait for the gap, then go
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, this, &AATCAircraft::StartTakeoff, FMath::Max(Identity->RemoveDelaySeconds, 0.01f));
	}
	else
	{
		LeaveTaxiNetwork();   // parked on its stand, clear of the taxiways
	}
}

void AATCAircraft::StartTakeoff()
{
	if (bTakingOff)
	{
		return;
	}
	bTakingOff = true;
	TakeoffSpeedCms = Movement->Speed;
	TakeoffSeconds = 0.f;
	PitchDeg = 0.f;
	Movement->ClearPath();   // off the taxi network: from here the aircraft flies itself
	LeaveTaxiNetwork();
	SetActorTickEnabled(true);
	Report(TAG_ATC_Event_Traffic_Takeoff, TaxiController->Route ? TaxiController->Route->RouteTag.ToString() : FString());
}

void AATCAircraft::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bLanding)
	{
		TickLanding(DeltaSeconds);
	}
	else if (bTakingOff)
	{
		TickTakeoff(DeltaSeconds);
	}
}

void AATCAircraft::StartLanding(ATaxiRoute* Route, float StartCm, float EndCm, float RolloutEndSpeedKt)
{
	if (!Route)
	{
		return;
	}
	bExitReserved = false;
	bGoingAround = false;
	LandingRoute = Route;
	ExitLane = Route;
	LandingStartCm = StartCm;
	LandingEndCm = EndCm;
	// Direction from two points on the rollout: the spline tangent at its very first point can be zero
	const FVector Start = Route->Spline->GetLocationAtDistanceAlongSpline(StartCm, ESplineCoordinateSpace::World);
	LandingDirection = (Route->Spline->GetLocationAtDistanceAlongSpline(FMath::Min(StartCm + 2000.f, Route->GetLength()), ESplineCoordinateSpace::World) - Start).GetSafeNormal2D();
	if (LandingDirection.IsNearlyZero())
	{
		LandingDirection = GetActorForwardVector().GetSafeNormal2D();
	}

	// Touchdown about 450 m past the runway end; with a short runway behind the start the braking is harder
	RolloutCm = Route->RunwayBeforeStartCm > 0.f ? FMath::Clamp(Route->RunwayBeforeStartCm - 45000.f, 50000.f, 160000.f) : 90000.f;
	TouchdownPoint = Start - LandingDirection * RolloutCm;
	const bool bHeavy = AircraftType && AircraftType->WakeCategory >= EWakeCategory::Heavy;
	ApproachSpeedCms = (bHeavy ? 145.f : 135.f) * ATCUnits::KnotsToCms;
	TouchdownSpeedCms = ApproachSpeedCms - 5.f * ATCUnits::KnotsToCms;
	RolloutEndSpeedCms = FMath::Max(RolloutEndSpeedKt, 5.f) * ATCUnits::KnotsToCms;
	RolloutDecelCms2 = FMath::Clamp((FMath::Square(TouchdownSpeedCms) - FMath::Square(RolloutEndSpeedCms)) / (2.f * RolloutCm), 100.f, 350.f);
	LandingToGoCm = ApproachDistanceM * ATCUnits::MetersToCm;
	LandingSpeedCms = ApproachSpeedCms;
	PitchDeg = 2.5f;

	// In the air nobody taxis behind it: no path until it joins the route. Its runway exit stays reserved (spawn manager)
	Movement->ClearPath();
	bLanding = true;
	SetActorTickEnabled(true);
	TickLanding(0.f);
	UE_LOG(LogATC, Display, TEXT("%s on final for %s, touchdown %.0f m before the exit route"), *Identity->Callsign.ToString(),
		*Route->RouteTag.ToString(), RolloutCm / 100.f);
}

void AATCAircraft::TickLanding(float DeltaSeconds)
{
	ATaxiRoute* Route = LandingRoute.Get();
	if (!Route)
	{
		Remove();
		return;
	}

	// Landing clearance: on final it asks for its runway exit (and so joins the queue for it, with a head start over the
	// ground); still not free 1.5 km before touchdown, it goes around
	if (!bExitReserved)
	{
		bExitReserved = ReserveRunwayExit();
		if (!bExitReserved && LandingToGoCm < 150000.f)
		{
			StartGoAround();
			return;
		}
	}

	// Approach: a little slower towards the threshold. Rollout: braking down to taxi speed
	const float Approach = ApproachDistanceM * ATCUnits::MetersToCm;
	if (LandingToGoCm > 0.f)
	{
		LandingSpeedCms = FMath::Lerp(TouchdownSpeedCms, ApproachSpeedCms, FMath::Clamp(LandingToGoCm / Approach, 0.f, 1.f));
	}
	else
	{
		LandingSpeedCms = FMath::Max(LandingSpeedCms - RolloutDecelCms2 * DeltaSeconds, RolloutEndSpeedCms);
	}
	LandingToGoCm -= LandingSpeedCms * DeltaSeconds;

	// 3 degree glide path; from 12 m the flare: the sink rate drops to a third at touchdown (smooth cubic)
	const float Glide = FMath::Tan(FMath::DegreesToRadians(3.f));
	const float FlareCm = 1200.f / Glide;
	float Height = 0.f;
	if (LandingToGoCm > FlareCm)
	{
		Height = LandingToGoCm * Glide;
		PitchDeg = 2.5f;
	}
	else if (LandingToGoCm > 0.f)
	{
		const float U = LandingToGoCm / FlareCm;
		Height = 1200.f * (0.3f * U + 1.4f * U * U - 0.7f * U * U * U);
		PitchDeg = FMath::Lerp(5.5f, 2.5f, U);
	}
	else
	{
		// Nose wheel down in about three seconds
		PitchDeg = FMath::FInterpConstantTo(PitchDeg, 0.f, DeltaSeconds, 2.f);
	}

	const FVector Location = TouchdownPoint - LandingDirection * LandingToGoCm + FVector(0.f, 0.f, Height);
	SetActorLocationAndRotation(Location, FRotator(PitchDeg, LandingDirection.Rotation().Yaw, 0.f));

	// At the start of the arrival route: from here the taxi controller drives (exit, taxi in, stand)
	if (-LandingToGoCm >= RolloutCm)
	{
		bLanding = false;
		SetActorTickEnabled(false);
		// On the lane its exit is reserved on
		ATaxiRoute* Lane = ExitLane.IsValid() ? ExitLane.Get() : Route;
		Movement->SetPath(Route->Spline, ETravelDirection::Forward, true, LandingStartCm, true);
		TaxiController->StartTaxiOnLane(Route, Lane, LandingStartCm, LandingEndCm);
		Movement->Speed = LandingSpeedCms;
		UE_LOG(LogATC, Display, TEXT("%s landed, rolling out at %.0f kt onto %s"), *Identity->Callsign.ToString(),
			LandingSpeedCms / ATCUnits::KnotsToCms, *Lane->RouteTag.ToString());
	}
}

bool AATCAircraft::ReserveRunwayExit()
{
	UTaxiNetworkSubsystem* Network = GetWorld() ? GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>() : nullptr;
	const TArray<FTaxiClaimRequest> Exits = UTaxiNetworkSubsystem::RunwayExitRequests(LandingRoute.Get(), LandingStartCm, AircraftType);
	if (!Network || Exits.Num() == 0)
	{
		return true;
	}
	// The first lane that is free; none free: it queues on the landing route
	AActor* Blocker = nullptr;
	const FTaxiClaimRequest* Free = Exits.FindByPredicate([this, Network, &Blocker](const FTaxiClaimRequest& Exit) { return Network->ClaimStretch(this, Exit, Blocker, true); });
	const FTaxiClaimRequest& Exit = Free ? *Free : Exits[0];
	const bool bReserved = Network->ClaimStretch(this, Exit, Blocker);
	ExitBlocker = Blocker;
	ExitLane = Exit.Route;
	return bReserved;
}

void AATCAircraft::StartGoAround()
{
	// Missed approach: full power and climb straight ahead; it leaves the circuit (it does not come back here)
	const AActor* Blocker = ExitBlocker.Get();
	UE_LOG(LogATC, Display, TEXT("%s goes around: runway exit of %s not free (%s)"), *Identity->Callsign.ToString(),
		LandingRoute.IsValid() ? *LandingRoute->RouteTag.ToString() : TEXT("?"), Blocker && Blocker->Implements<UAircraftInfo>()
		? *FString::Printf(TEXT("held by %s"), *IAircraftInfo::Execute_GetCallsign(Blocker).ToString()) : TEXT("held"));
	LeaveTaxiNetwork();
	bLanding = false;
	bGoingAround = true;
	bTakingOff = true;
	TakeoffSpeedCms = LandingSpeedCms;
	TakeoffSeconds = 0.f;
}

void AATCAircraft::TickTakeoff(float DeltaSeconds)
{
	// Takeoff roll at about 2 m/s2, rotation at VR to 8 degrees nose up, then a steady climb
	TakeoffSeconds += DeltaSeconds;
	const float Rotate = RotateSpeedKt * ATCUnits::KnotsToCms;
	TakeoffSpeedCms += (TakeoffSpeedCms < Rotate ? 200.f : 80.f) * DeltaSeconds;
	if (TakeoffSpeedCms >= Rotate)
	{
		PitchDeg = FMath::Min(PitchDeg + 3.f * DeltaSeconds, 8.f);
	}
	FRotator Rotation = GetActorRotation();
	Rotation.Pitch = PitchDeg;
	const float ClimbDeg = FMath::Max(PitchDeg - 1.5f, 0.f);
	const FVector Flat = FRotator(0.f, Rotation.Yaw, 0.f).Vector();
	const FVector Velocity = (Flat * FMath::Cos(FMath::DegreesToRadians(ClimbDeg)) + FVector::UpVector * FMath::Sin(FMath::DegreesToRadians(ClimbDeg))) * TakeoffSpeedCms;
	SetActorLocationAndRotation(GetActorLocation() + Velocity * DeltaSeconds, Rotation);

	// Out of sight of the tower: about 600 m up, or after two minutes
	if (TakeoffSeconds > 120.f || (ClimbDeg > 0.f && GetActorLocation().Z > 60000.f))
	{
		Remove();
	}
}

void AATCAircraft::HandlePushbackState(EPushbackState OldState, EPushbackState NewState)
{
	const FString Name = StaticEnum<EPushbackState>()->GetNameStringByValue(static_cast<int64>(NewState));
	Report(NewState == EPushbackState::Completed ? TAG_ATC_Event_Pushback_Completed : TAG_ATC_Event_Pushback_State, Name);
}

bool AATCAircraft::RequestPushback_Implementation(const FPushbackRequest& Request)
{
	return PushbackController->RequestPushback(Request, Identity->Stand);
}

bool AATCAircraft::StartTaxi_Implementation(ATaxiRoute* Route)
{
	return TaxiController->StartTaxi(Route);
}

bool AATCAircraft::HoldPosition_Implementation()
{
	return TaxiController->HoldPosition();
}

bool AATCAircraft::ContinueTaxi_Implementation()
{
	return TaxiController->ContinueTaxi();
}

void AATCAircraft::SetSpeedLimit_Implementation(float Knots)
{
	TaxiController->SetCommandedSpeedKt(Knots);
}

FName AATCAircraft::GetCallsign_Implementation() const
{
	return Identity->Callsign;
}

UAircraftTypeData* AATCAircraft::GetAircraftType_Implementation() const
{
	return AircraftType;
}

FText AATCAircraft::GetStatusText_Implementation() const
{
	if (bGoingAround)
	{
		return FText::FromString(FString::Printf(TEXT("Go-around %.0f kt"), TakeoffSpeedCms / ATCUnits::KnotsToCms));
	}
	if (bLanding)
	{
		return FText::FromString(FString::Printf(TEXT("%s %.0f kt"), LandingToGoCm > 0.f ? TEXT("Final approach") : TEXT("Landing rollout"),
			LandingSpeedCms / ATCUnits::KnotsToCms));
	}
	if (bTakingOff)
	{
		return FText::FromString(FString::Printf(TEXT("Takeoff %.0f kt"), TakeoffSpeedCms / ATCUnits::KnotsToCms));
	}
	const float Knots = Movement->Speed / ATCUnits::KnotsToCms;
	if (PushbackController->GetState() != EPushbackState::Parked && PushbackController->GetState() != EPushbackState::Completed)
	{
		return FText::FromString(FString::Printf(TEXT("Pushback: %s %.0f kt"),
			*StaticEnum<EPushbackState>()->GetNameStringByValue(static_cast<int64>(PushbackController->GetState())), Knots));
	}
	const AActor* Traffic = TaxiController->GetTrafficAhead();
	const FString Waiting = Traffic && Traffic->Implements<UAircraftInfo>()
		? FString::Printf(TEXT(", behind %s"), *IAircraftInfo::Execute_GetCallsign(Traffic).ToString()) : FString();
	return FText::FromString(FString::Printf(TEXT("%s %.0f kt%s"),
		*StaticEnum<ETaxiState>()->GetNameStringByValue(static_cast<int64>(TaxiController->GetState())), Knots, *Waiting));
}
