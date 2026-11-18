#include "ATCAircraft.h"
#include "AircraftIdentityComponent.h"
#include "AircraftMovementComponent.h"
#include "AircraftTypeData.h"
#include "ATCGameplayTags.h"
#include "ATCSoundComponent.h"
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
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<float> CVarCrosswindShare(TEXT("atc.CrosswindShare"), 0.25f,
	TEXT("Share of landings that come in with a gusty crosswind (crabbed, kicked straight in the flare); 1 = all, for tests"));

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

	EngineSound = CreateDefaultSubobject<UATCSoundComponent>(TEXT("EngineSound"));
	EngineSound->SetupAttachment(RootComponent);
}

float AATCAircraft::GetEngineThrust() const
{
	if (bTakingOff || bGoingAround)
	{
		return 1.f;
	}
	if (bLanding)
	{
		return LandingToGoCm < 0.f ? 0.7f : 0.45f;   // reversers on the rollout, approach power before
	}
	const ETaxiState Taxi = TaxiController->GetState();
	const EPushbackState Push = PushbackController->GetState();
	if (Taxi == ETaxiState::Taxiing || Taxi == ETaxiState::Braking || Taxi == ETaxiState::Holding)
	{
		return 0.3f;
	}
	if (Push == EPushbackState::Pushing || Push == EPushbackState::Stopping || Push == EPushbackState::TugDisconnecting
		|| (Push == EPushbackState::Completed && Taxi == ETaxiState::Idle))
	{
		return 0.22f;   // the engines start during the push
	}
	return 0.f;   // at the stand: engines off
}

void AATCAircraft::UpdateEngineSound()
{
	const float Thrust = GetEngineThrust();
	if (EngineSound && !FMath::IsNearlyEqual(Thrust, EngineSound->GetThrust()))
	{
		EngineSound->SetThrust(Thrust);
	}
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
	UpdateEngineSound();
	if (UTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UTrafficSubsystem>() : nullptr)
	{
		Traffic->BroadcastAircraftEvent(this, EventTag, Detail);
	}
}

void AATCAircraft::HandleTaxiState(ETaxiState OldState, ETaxiState NewState)
{
	if (NewState == ETaxiState::Arrived || NewState == ETaxiState::Idle)
	{
		TaxiController->bPriority = false;   // on its stand: an ordinary aircraft again
	}
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
	FlightPathDeg = 0.f;
	bLiftedOff = false;
	// The runway centre line from the lineup straight (the route's last 60 m lie on the painted centre line): the roll and
	// the climb-out stay on it (user 18-11-2026; the nose direction alone drifted metres off over 2 km)
	// Heavier ones accelerate slower and rotate later: Vr about the approach speed + 10 kt; a few percent per flight (weight)
	FRandomStream Flight(int32(GetTypeHash(Identity->Callsign.ToString())) ^ 0x7A4E);
	const EWakeCategory Wake = AircraftType ? AircraftType->WakeCategory : EWakeCategory::Medium;
	TakeoffAccelCms2 = (Wake == EWakeCategory::Super ? 170.f : Wake == EWakeCategory::Heavy ? 190.f : 230.f) * Flight.FRandRange(0.9f, 1.1f);
	RotateSpeedKt = (AircraftType && AircraftType->ApproachSpeedKt > 0.f ? AircraftType->ApproachSpeedKt : 140.f) + 10.f + Flight.FRandRange(-4.f, 6.f);
	bTakeoffOnLine = false;
	if (const ATaxiRoute* Route = TaxiController->Route)
	{
		const float Length = Route->GetLength();
		if (!Route->TakeoffDirection.IsNearlyZero())
		{
			// The painted runway centre line, stored on the route by ATC.CheckRoutes
			TakeoffLineDir = Route->TakeoffDirection.GetSafeNormal2D();
			TakeoffLineStart = FVector(Route->TakeoffLinePoint.X, Route->TakeoffLinePoint.Y, GetActorLocation().Z);
			bTakeoffOnLine = (TakeoffLineDir | GetActorForwardVector().GetSafeNormal2D()) > 0.98f;
		}
		else if (Length > 7000.f)
		{
			TakeoffLineStart = Route->GetLocationAt(Length);
			// The chord of the last two spline points: both lie on the centre line, the curve between them does not quite
			// (its start tangent is the bisector of the turn onto the runway: 0.46 degrees off on 24, 4 m off after 600 m)
			const int32 Points = Route->Spline->GetNumberOfSplinePoints();
			const FVector Before = Points >= 2 ? Route->Spline->GetLocationAtSplinePoint(Points - 2, ESplineCoordinateSpace::World) : FVector::ZeroVector;
			TakeoffLineDir = Points >= 2 && FVector::Dist2D(Before, TakeoffLineStart) > 3000.f ? (TakeoffLineStart - Before).GetSafeNormal2D()
				: (TakeoffLineStart - Route->GetLocationAt(Length - 6000.f)).GetSafeNormal2D();
			bTakeoffOnLine = !TakeoffLineDir.IsNearlyZero() && (TakeoffLineDir | GetActorForwardVector().GetSafeNormal2D()) > 0.98f;
		}
	}
	// The departure (SID): from the Kaagbaan (24) straight on or a left turn, from the Polderbaan (36L) straight, left or
	// right (user 18-11-2026); 25 to 70 degrees, flown from 150 m up
	const FString RouteTag = TaxiController->Route ? TaxiController->Route->RouteTag.ToString() : FString();
	const FString Runway = RouteTag.Contains(TEXT(".Rwy")) ? RouteTag.RightChop(RouteTag.Find(TEXT(".Rwy"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 4) : FString();
	const TArray<float> Turns = Runway == TEXT("24") ? TArray<float>{ 0.f, -1.f } : Runway == TEXT("36L") ? TArray<float>{ 0.f, -1.f, 1.f } : TArray<float>{ 0.f };
	DepartureTurnLeftDeg = Turns[Flight.RandHelper(Turns.Num())] * Flight.FRandRange(25.f, 70.f);
	UE_LOG(LogATC, Display, TEXT("%s takeoff roll %s: line heading %.2f, nose %.2f, Vr %.0f kt, %.1f m/s2, then %s"), *Identity->Callsign.ToString(),
		bTakeoffOnLine ? TEXT("on the centre line") : TEXT("along the nose"), TakeoffLineDir.Rotation().Yaw, GetActorRotation().Yaw, RotateSpeedKt, TakeoffAccelCms2 / 100.f,
		DepartureTurnLeftDeg == 0.f ? TEXT("straight on") : *FString::Printf(TEXT("a %.0f degree %s turn"), FMath::Abs(DepartureTurnLeftDeg), DepartureTurnLeftDeg > 0.f ? TEXT("right") : TEXT("left")));
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

bool AATCAircraft::PlanSidestep(const ATaxiRoute* ApproachRoute)
{
	// The two finals: the one it flies first (the approach route's runway) and the landing one, through the touchdown point
	const FVector2D FirstStart(ApproachRoute->Spline->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World));
	FirstFinal = (FVector2D(ApproachRoute->Spline->GetLocationAtDistanceAlongSpline(FMath::Min(2000.f, ApproachRoute->GetLength()), ESplineCoordinateSpace::World))
		- FirstStart).GetSafeNormal();
	const FVector2D Touchdown(TouchdownPoint);
	const FVector2D Landing = FVector2D(LandingDirection).GetSafeNormal();
	const float Cross = FirstFinal.X * Landing.Y - FirstFinal.Y * Landing.X;
	SidestepTurnRad = FMath::Acos(FMath::Clamp(FirstFinal | Landing, -1.f, 1.f));
	if (FirstFinal.IsZero() || SidestepTurnRad < FMath::DegreesToRadians(10.f) || SidestepTurnRad > FMath::DegreesToRadians(90.f))
	{
		return false;
	}
	SidestepTurnSign = Cross > 0.f ? 1.f : -1.f;   // towards +Y (right, seen from above with X forward)

	// Where the finals cross: Touchdown - Landing * Back = FirstStart - FirstFinal * Along
	const FVector2D Delta = Touchdown - FirstStart;
	const float Det = -Landing.X * FirstFinal.Y + Landing.Y * FirstFinal.X;
	if (FMath::Abs(Det) < KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float Back = (FirstFinal.X * Delta.Y - FirstFinal.Y * Delta.X) / Det;   // touchdown back to the crossing
	// A visual sidestep: about a 1.2 km turn (some 22 degrees of bank), at least 300 m straight before touchdown
	const float Tangent = FMath::Tan(0.5f * SidestepTurnRad);
	SidestepRadius = FMath::Min(120000.f, (Back - 30000.f) / Tangent);
	if (SidestepRadius < 40000.f)
	{
		UE_LOG(LogATC, Display, TEXT("%s: no sidestep, the finals cross %.0f m before touchdown (too close for a turn)"), *Identity->Callsign.ToString(), Back / 100.f);
		return false;
	}
	const FVector2D Crossing = Touchdown - Landing * Back;
	SidestepArcStart = Crossing - FirstFinal * (SidestepRadius * Tangent);
	SidestepCenter = SidestepArcStart + SidestepTurnSign * FVector2D(-FirstFinal.Y, FirstFinal.X) * SidestepRadius;
	BackStraight = Back - SidestepRadius * Tangent;
	BackArc = SidestepRadius * SidestepTurnRad;
	return true;
}

void AATCAircraft::SidestepAt(float ToGoCm, FVector2D& OutPoint, FVector2D& OutHeading, float& OutCurvature) const
{
	const FVector2D Landing = FVector2D(LandingDirection).GetSafeNormal();
	OutCurvature = 0.f;
	if (ToGoCm <= BackStraight)
	{
		OutHeading = Landing;
		OutPoint = FVector2D(TouchdownPoint) - Landing * ToGoCm;
	}
	else if (ToGoCm <= BackStraight + BackArc)
	{
		// Fraction of the turn done; heading and radius turn together
		const float Angle = SidestepTurnSign * SidestepTurnRad * (1.f - (ToGoCm - BackStraight) / BackArc);
		const FVector2D Radial = SidestepArcStart - SidestepCenter;
		const float C = FMath::Cos(Angle), S = FMath::Sin(Angle);
		OutPoint = SidestepCenter + FVector2D(Radial.X * C - Radial.Y * S, Radial.X * S + Radial.Y * C);
		OutHeading = FVector2D(FirstFinal.X * C - FirstFinal.Y * S, FirstFinal.X * S + FirstFinal.Y * C);
		OutCurvature = SidestepTurnSign / SidestepRadius;
	}
	else
	{
		OutHeading = FirstFinal;
		OutPoint = SidestepArcStart - FirstFinal * (ToGoCm - BackStraight - BackArc);
	}
}

void AATCAircraft::StartLanding(ATaxiRoute* Route, float StartCm, float EndCm, float RolloutEndSpeedKt, const ATaxiRoute* ApproachRoute)
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

	// Touchdown on the aiming point markings (the white blocks; else 450 m past the runway end); with a short runway behind
	// the start the braking is harder
	const float TouchdownFromEnd = Route->TouchdownFromEndCm > 0.f ? Route->TouchdownFromEndCm : 45000.f;
	RolloutCm = Route->RunwayBeforeStartCm > 0.f ? FMath::Clamp(Route->RunwayBeforeStartCm - TouchdownFromEnd, 40000.f, 250000.f) : 90000.f;
	TouchdownPoint = Start - LandingDirection * RolloutCm;
	// Every flight its own: the type's approach speed a few knots either side, now and then a crosswind (crabbed, gusty: half
	// the gust is flown on top), and the way the pilot hand-flies the final. From the callsign, so a run repeats
	const bool bHeavy = AircraftType && AircraftType->WakeCategory >= EWakeCategory::Heavy;
	// Which landings get wind: one series for the whole session (from the callsigns the share came out 1 in 8, not 1 in 4)
	static FRandomStream WindDraws(2026);
	const bool bWindy = WindDraws.FRand() < CVarCrosswindShare.GetValueOnGameThread();
	FRandomStream Flight(int32((GetTypeHash(Identity->Callsign.ToString()) ^ 0x5EEDu) * 2654435761u >> 1));
	CrosswindKt = bWindy ? Flight.FRandRange(10.f, 25.f) * (Flight.FRand() < 0.5f ? -1.f : 1.f) : Flight.FRandRange(-4.f, 4.f);
	const float GustKt = bWindy ? Flight.FRandRange(4.f, 10.f) : 0.f;
	const float TypeKt = AircraftType && AircraftType->ApproachSpeedKt > 0.f ? AircraftType->ApproachSpeedKt : (bHeavy ? 145.f : 135.f);
	ApproachSpeedCms = (TypeKt + Flight.FRandRange(-5.f, 5.f) + 0.5f * GustKt) * ATCUnits::KnotsToCms;
	CrabDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(CrosswindKt * ATCUnits::KnotsToCms / ApproachSpeedCms, -0.5f, 0.5f)));
	WanderLateralCm = FMath::Min(Flight.FRandRange(500.f, 1300.f) * (1.f + FMath::Abs(CrosswindKt) / 15.f), 2500.f);
	WanderVerticalCm = Flight.FRandRange(150.f, 450.f) * (1.f + FMath::Abs(CrosswindKt) / 25.f);
	for (float& Phase : WanderPhase)
	{
		Phase = Flight.FRandRange(0.f, 2.f * PI);
	}
	FinalSeconds = 0.f;
	// Not every touchdown on the same spot: a little short or a long float, within the touchdown zone of the white blocks
	const float Float = Flight.FRandRange(-2500.f, 6000.f);
	TouchdownPoint += LandingDirection * Float;
	RolloutCm = FMath::Max(RolloutCm - Float, 30000.f);
	TouchdownSpeedCms = ApproachSpeedCms - 5.f * ATCUnits::KnotsToCms;
	RolloutEndSpeedCms = FMath::Max(RolloutEndSpeedKt, 5.f) * ATCUnits::KnotsToCms;
	RolloutDecelCms2 = FMath::Clamp((FMath::Square(TouchdownSpeedCms) - FMath::Square(RolloutEndSpeedCms)) / (2.f * RolloutCm), 100.f, 350.f);
	LandingToGoCm = ApproachDistanceM * ATCUnits::MetersToCm;
	LandingSpeedCms = ApproachSpeedCms;
	PitchDeg = 2.5f;
	RollDeg = 0.f;
	SinceTouchdown = -1.f;
	bSidestep = ApproachRoute && PlanSidestep(ApproachRoute);
	if (bSidestep)
	{
		// At least 1.5 km on the first final before the turn, so it is seen lined up for that runway
		LandingToGoCm = FMath::Max(LandingToGoCm, BackStraight + BackArc + 150000.f);
		UE_LOG(LogATC, Display, TEXT("%s on final for %s, sidesteps to %s: %.0f degree turn of %.0f m radius, %.0f m before touchdown"),
			*Identity->Callsign.ToString(), *ApproachRoute->RouteTag.ToString(), *Route->RouteTag.ToString(), FMath::RadiansToDegrees(SidestepTurnRad),
			SidestepRadius / 100.f, (BackStraight + BackArc) / 100.f);
	}

	// In the air nobody taxis behind it: no path until it joins the route. Its runway exit stays reserved (spawn manager)
	Movement->ClearPath();
	bLanding = true;
	SetActorTickEnabled(true);
	TickLanding(0.f);
	UpdateEngineSound();
	UE_LOG(LogATC, Display, TEXT("%s on final for %s, touchdown %.0f m before the exit route, %.0f kt, %s"), *Identity->Callsign.ToString(),
		*Route->RouteTag.ToString(), RolloutCm / 100.f, ApproachSpeedCms / ATCUnits::KnotsToCms, FMath::Abs(CrosswindKt) >= 9.f
		? *FString::Printf(TEXT("crosswind %.0f kt from the %s, %.0f degree crab, gusts %.0f kt"), FMath::Abs(CrosswindKt), CrosswindKt > 0.f ? TEXT("right") : TEXT("left"),
			FMath::Abs(CrabDeg), GustKt) : TEXT("calm"));
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
	// ground); still not free 1.5 km before touchdown, it goes around. Unless the one holding the exit is the landing
	// before it, rolling out or taxiing off: that one is gone by the time this one gets there (its own rollout takes
	// another half a minute), so it decides just before the threshold (150 m), like a pilot who sees the runway being vacated
	if (!bExitReserved)
	{
		bExitReserved = ReserveRunwayExit();
		const AATCAircraft* Ahead = Cast<AATCAircraft>(ExitBlocker.Get());
		// (on the rollout the landing moves itself, not through Movement; in the turn off the runway it is down to ~6 kt)
		// One that is moving off the runway (taxiing in, or a departure passing behind the exit) only holds the taxiway behind
		// the exit, half a minute of rollout away, like traffic a controller clears a landing behind: decide 50 m before
		// touchdown (17-11-2026: two wide-bodies went around behind one taxiing in at 12 kt and one passing at 7 kt)
		const bool bMovingOff = Ahead && !Ahead->IsLanding() && Ahead->Movement->Speed > 50.f;
		const float DecideCm = Ahead && Ahead->IsLanding() ? 15000.f : bMovingOff ? 5000.f : 150000.f;
		if (!bExitReserved && LandingToGoCm < DecideCm)
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
	// Touchdown: the reversers roar
	if (LandingToGoCm < 0.f && EngineSound && EngineSound->GetThrust() < 0.6f)
	{
		UpdateEngineSound();
	}

	// 3 degree glide path, nose 2.5 degrees up; from 12 m the flare: the sink rate drops to a third at touchdown
	// (smooth cubic) while the nose comes up to 6 degrees. On the main gear first: the nose stays up for a second and a
	// half, then comes down at 1.5 degrees a second (the nose wheel touches about four seconds after the mains)
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
		PitchDeg = FMath::Lerp(6.f, 2.5f, FMath::SmoothStep(0.f, 1.f, U));
	}
	else
	{
		SinceTouchdown = SinceTouchdown < 0.f ? 0.f : SinceTouchdown + DeltaSeconds;
		if (SinceTouchdown > 1.5f)
		{
			PitchDeg = FMath::FInterpConstantTo(PitchDeg, 0.f, DeltaSeconds, 1.5f);
		}
	}

	// On the final (or the sidestep turn); banked in a turn as an aircraft is: tan(bank) = v^2 * curvature / g
	FVector2D Ground = FVector2D(TouchdownPoint - LandingDirection * LandingToGoCm);
	FVector2D Heading = FVector2D(LandingDirection);
	float Curvature = 0.f;
	if (bSidestep && LandingToGoCm > 0.f)
	{
		SidestepAt(LandingToGoCm, Ground, Heading, Curvature);
	}

	// Hand flown: a slow wander around the centre line (with the bank and heading that make it) and the glide path, more
	// far out, gone at the flare; in a crosswind more, gusts rock the wings, the nose points into the wind (crab) until the
	// pilot kicks it straight in the flare, upwind wing a little low
	FinalSeconds += DeltaSeconds;
	float LateralCm = 0.f, LateralAccel = 0.f, LateralRate = 0.f, YawOffset = 0.f, WindRoll = 0.f, PitchWobble = 0.f;
	if (LandingToGoCm > 0.f)
	{
		const float T = FinalSeconds;
		const float Far = FMath::Clamp(LandingToGoCm / 600000.f, 0.f, 1.f);
		const float BeforeFlare = FMath::Clamp((LandingToGoCm - FlareCm) / 100000.f, 0.f, 1.f);
		const float Size = WanderLateralCm * (0.15f + 0.85f * Far) * BeforeFlare;
		const float W1 = 0.13f, W2 = 0.31f;
		LateralCm = Size * (0.65f * FMath::Sin(W1 * T + WanderPhase[0]) + 0.35f * FMath::Sin(W2 * T + WanderPhase[1]));
		LateralRate = Size * (0.65f * W1 * FMath::Cos(W1 * T + WanderPhase[0]) + 0.35f * W2 * FMath::Cos(W2 * T + WanderPhase[1]));
		LateralAccel = -Size * (0.65f * W1 * W1 * FMath::Sin(W1 * T + WanderPhase[0]) + 0.35f * W2 * W2 * FMath::Sin(W2 * T + WanderPhase[1]));
		Height += WanderVerticalCm * BeforeFlare * (0.6f * FMath::Sin(0.16f * T + WanderPhase[2]) + 0.4f * FMath::Sin(0.47f * T + WanderPhase[3]));
		const float Turbulence = FMath::Abs(CrosswindKt) / 20.f;
		const float Decrab = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(LandingToGoCm / (0.6f * FlareCm), 0.f, 1.f));
		YawOffset = CrabDeg * Decrab;
		WindRoll = Turbulence * BeforeFlare * 3.f * (0.6f * FMath::Sin(1.1f * T + WanderPhase[4]) + 0.4f * FMath::Sin(2.3f * T + WanderPhase[5]))
			+ FMath::Clamp(CrosswindKt / 10.f, -1.f, 1.f) * 2.f * (1.f - Decrab);
		PitchWobble = (0.3f + 0.5f * Turbulence) * BeforeFlare * FMath::Sin(0.9f * T + WanderPhase[5]);
	}
	const FVector2D Side(-Heading.Y, Heading.X);
	Ground += Side * LateralCm;
	const float TrackYaw = FVector(Heading, 0.f).Rotation().Yaw + FMath::RadiansToDegrees(FMath::Atan2(LateralRate, FMath::Max(LandingSpeedCms, 1.f)));
	const float TargetRoll = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan((FMath::Square(LandingSpeedCms) * Curvature + LateralAccel) / 981.f)) + WindRoll, -30.f, 30.f);
	RollDeg = FMath::FInterpConstantTo(RollDeg, TargetRoll, DeltaSeconds, 8.f);
	const FVector Location(Ground.X, Ground.Y, TouchdownPoint.Z + Height);
	SetActorLocationAndRotation(Location, FRotator(PitchDeg + PitchWobble, TrackYaw + YawOffset, RollDeg));

	// At the start of the arrival route: from here the taxi controller drives (exit, taxi in, stand)
	if (-LandingToGoCm >= RolloutCm)
	{
		bLanding = false;
		SetActorTickEnabled(false);
		// On the lane its exit is reserved on
		ATaxiRoute* Lane = ExitLane.IsValid() ? ExitLane.Get() : Route;
		Movement->SetPath(Route->Spline, ETravelDirection::Forward, true, LandingStartCm, true);
		// Off the runway first: until it is on its stand it goes before ground traffic, or it blocks its runway's exits
		TaxiController->bPriority = true;
		TaxiController->StartTaxiOnLane(Route, Lane, LandingStartCm, LandingEndCm);
		Movement->Speed = LandingSpeedCms;
		UE_LOG(LogATC, Display, TEXT("%s landed, rolling out at %.0f kt onto %s"), *Identity->Callsign.ToString(),
			LandingSpeedCms / ATCUnits::KnotsToCms, *Lane->RouteTag.ToString());
		Report(TAG_ATC_Event_Traffic_Landed, Lane->RouteTag.ToString());
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
	const AATCAircraft* Ahead = Cast<AATCAircraft>(Blocker);
	const FString State = Ahead ? FString::Printf(TEXT(", %s, %.0f kt, %.0f m to go"), Ahead->IsLanding() ? TEXT("landing") : Ahead->TaxiController->bPriority
		? TEXT("taxiing in") : *UEnum::GetValueAsString(Ahead->TaxiController->GetState()), Ahead->Movement->Speed / ATCUnits::KnotsToCms, LandingToGoCm / 100.f) : FString();
	UE_LOG(LogATC, Display, TEXT("%s goes around: runway exit of %s not free (%s%s)"), *Identity->Callsign.ToString(),
		LandingRoute.IsValid() ? *LandingRoute->RouteTag.ToString() : TEXT("?"), Blocker && Blocker->Implements<UAircraftInfo>()
		? *FString::Printf(TEXT("held by %s"), *IAircraftInfo::Execute_GetCallsign(Blocker).ToString()) : TEXT("held"), *State);
	LeaveTaxiNetwork();
	bLanding = false;
	bGoingAround = true;
	bTakeoffOnLine = false;
	bTakingOff = true;
	TakeoffSpeedCms = LandingSpeedCms;
	TakeoffSeconds = 0.f;
	// Still in the air, coming down the glide path: the nose comes up and the descent turns into a climb
	bLiftedOff = true;
	FlightPathDeg = -3.f;
	UpdateEngineSound();
}

void AATCAircraft::TickTakeoff(float DeltaSeconds)
{
	// Takeoff roll at about 2 m/s2. At VR the nose comes up at 2.5 degrees a second to 13 degrees, the aircraft still
	// rolling on its main gear (rotating about it); it lifts off at about 8 degrees, and the climb angle of its path
	// builds up smoothly to the pitch minus 6 degrees (the wing's angle of attack): 7 degrees at full pitch
	TakeoffSeconds += DeltaSeconds;
	const float Rotate = RotateSpeedKt * ATCUnits::KnotsToCms;
	TakeoffSpeedCms += (TakeoffSpeedCms < Rotate ? TakeoffAccelCms2 : 0.4f * TakeoffAccelCms2) * DeltaSeconds;
	if (TakeoffSpeedCms >= Rotate)
	{
		PitchDeg = FMath::Min(PitchDeg + 2.5f * DeltaSeconds, 13.f);
	}
	bLiftedOff |= PitchDeg >= 8.f;
	if (bLiftedOff)
	{
		FlightPathDeg = FMath::FInterpConstantTo(FlightPathDeg, FMath::Max(PitchDeg - 6.f, 0.f), DeltaSeconds, 2.f);
	}
	FRotator Rotation = GetActorRotation();
	Rotation.Pitch = PitchDeg;
	// The departure turn from 150 m up, at the standard rate (3 degrees a second) with the bank that flies it; until then on
	// the centre line, wings level (a go-around out of the sidestep turn climbs straight ahead)
	const bool bTurning = !bGoingAround && DepartureTurnLeftDeg != 0.f && GetActorLocation().Z - TakeoffLineStart.Z > 15000.f;
	if (bTakeoffOnLine && !bTurning && FMath::Abs(DepartureTurnLeftDeg) > 0.f && GetActorLocation().Z - TakeoffLineStart.Z > 15000.f)
	{
		bTakeoffOnLine = false;
	}
	if (bTakeoffOnLine)
	{
		Rotation.Yaw = TakeoffLineDir.Rotation().Yaw;
	}
	float TargetRoll = 0.f;
	if (bTurning)
	{
		bTakeoffOnLine = false;
		// Standard rate (3 degrees a second), but airliners bank at most 25 degrees: slower when fast
		const float RateDeg = FMath::Min(3.f, FMath::RadiansToDegrees(981.f * FMath::Tan(FMath::DegreesToRadians(25.f)) / FMath::Max(TakeoffSpeedCms, 1.f)));
		const float Step = FMath::Min(RateDeg * DeltaSeconds, FMath::Abs(DepartureTurnLeftDeg));
		Rotation.Yaw += FMath::Sign(DepartureTurnLeftDeg) * Step;
		DepartureTurnLeftDeg -= FMath::Sign(DepartureTurnLeftDeg) * Step;
		TargetRoll = FMath::Sign(DepartureTurnLeftDeg) * FMath::RadiansToDegrees(FMath::Atan(TakeoffSpeedCms * FMath::DegreesToRadians(RateDeg) / 981.f));
		TargetRoll = FMath::Abs(DepartureTurnLeftDeg) < 8.f ? TargetRoll * FMath::Abs(DepartureTurnLeftDeg) / 8.f : TargetRoll;   // rolling out
	}
	RollDeg = FMath::FInterpConstantTo(RollDeg, TargetRoll, DeltaSeconds, 6.f);
	Rotation.Roll = RollDeg;
	const float ClimbDeg = FlightPathDeg;
	const FVector Flat = FRotator(0.f, Rotation.Yaw, 0.f).Vector();
	const FVector Velocity = (Flat * FMath::Cos(FMath::DegreesToRadians(ClimbDeg)) + FVector::UpVector * FMath::Sin(FMath::DegreesToRadians(ClimbDeg))) * TakeoffSpeedCms;
	FVector Location = GetActorLocation() + Velocity * DeltaSeconds;
	if (bTakeoffOnLine)
	{
		// Any sideways offset from the centre line (a few centimetres at the lineup) goes away within a few seconds
		const FVector Side = FVector::CrossProduct(FVector::UpVector, TakeoffLineDir);
		const float Offset = FVector::DotProduct(Location - TakeoffLineStart, Side);
		Location -= Side * (Offset - FMath::FInterpTo(Offset, 0.f, DeltaSeconds, 1.5f));
	}
	SetActorLocationAndRotation(Location, Rotation);

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
		return FText::FromString(FString::Printf(TEXT("%s %.0f kt%s"), LandingToGoCm > 0.f ? TEXT("Final approach") : TEXT("Landing rollout"),
			LandingSpeedCms / ATCUnits::KnotsToCms, FMath::Abs(CrosswindKt) >= 9.f && LandingToGoCm > 0.f
			? *FString::Printf(TEXT(", crosswind %.0f kt from the %s"), FMath::Abs(CrosswindKt), CrosswindKt > 0.f ? TEXT("right") : TEXT("left")) : TEXT("")));
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
