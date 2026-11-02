#include "TrafficDirector.h"
#include "AircraftIdentityComponent.h"
#include "AircraftSpawnPoint.h"
#include "EngineUtils.h"
#include "AircraftSpawnSubsystem.h"
#include "AircraftTypeData.h"
#include "ATCAircraft.h"
#include "ATCGameplayTags.h"
#include "ATCUnits.h"
#include "Stand.h"
#include "PushbackControllerComponent.h"
#include "TaxiControllerComponent.h"
#include "TaxiNetworkSubsystem.h"
#include "TaxiRoute.h"
#include "TrafficMixData.h"
#include "TrafficSubsystem.h"
#include "AircraftMovementComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

ATrafficDirector::ATrafficDirector()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ATrafficDirector::BeginPlay()
{
	Super::BeginPlay();
	if (bStartOnBeginPlay)
	{
		// Next frame: stands and routes register themselves in their own BeginPlay
		GetWorldTimerManager().SetTimerForNextTick(this, &ATrafficDirector::StartTraffic);
	}
}

void ATrafficDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopTraffic();
	Super::EndPlay(EndPlayReason);
}

float ATrafficDirector::Now() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void ATrafficDirector::StartTraffic()
{
	UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	UAircraftSpawnSubsystem* Spawner = GetWorld()->GetSubsystem<UAircraftSpawnSubsystem>();
	if (bRunning || !Traffic || !Spawner || !TrafficMix || Gates.Num() == 0)
	{
		UE_LOG(LogATC, Warning, TEXT("%s: traffic not started (already running, no gates or no traffic mix)."), *GetName());
		return;
	}
	bRunning = true;
	Traffic->OnAircraftEvent.AddUniqueDynamic(this, &ATrafficDirector::HandleAircraftEvent);
	Spawner->OnAircraftSpawned.AddUniqueDynamic(this, &ATrafficDirector::HandleSpawned);

	// A normal day is already going: part of the gates have an aircraft in its turnaround, ready at different times.
	// A gate with a parked model of the map always has one: it takes the model's place and leaves later in the day
	int32 Parked = 0, StandIns = 0;
	for (int32 Index = 0; Index < Gates.Num(); ++Index)
	{
		const AStand* Stand = Traffic->FindStand(Gates[Index].Stand);
		UAircraftTypeData* ModelType = Gates[Index].ParkedModels.Num() > 0 ? Gates[Index].ParkedType.LoadSynchronous() : nullptr;
		if (!Stand || Stand->GetOccupant() || (!ModelType && Random.FRand() > InitialOccupancy))
		{
			continue;
		}
		UAircraftTypeData* Type = ModelType;
		TArray<FString> Airlines = { Gates[Index].ParkedAirline };
		if (!ModelType && !PickType(Gates[Index].bWideBody, Type, Airlines))
		{
			continue;
		}
		FAircraftSpawnRequest Request;
		Request.Type = Type;
		Request.Callsign = Spawner->MakeCallsign(Airlines);
		Request.AircraftClass = TrafficMix->AircraftClass.LoadSynchronous();
		Request.StandTag = Gates[Index].Stand;
		Request.RouteTag = Gates[Index].DepartureRoute;
		Request.bRemoveAtRouteEnd = true;
		Request.RemoveDelaySeconds = LineUpSeconds;
		GateOf.Add(Request.Callsign, Index);
		if (ModelType)
		{
			ParkedModelStandIns.Add(Request.Callsign);
		}
		if (Spawner->RequestSpawn(Request).IsNone())
		{
			GateOf.Remove(Request.Callsign);
			ParkedModelStandIns.Remove(Request.Callsign);
			continue;
		}
		const FName Callsign = Request.Callsign;
		const FVector2D Wait = ModelType ? ParkedModelDepartureSeconds : FVector2D(10.f, TurnaroundSeconds.Y);
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Callsign]() { Depart(Callsign); }),
			Random.FRandRange(Wait.X, FMath::Max(Wait.Y, Wait.X + 1.f)), false);
		++Parked;
		StandIns += ModelType ? 1 : 0;
	}

	// Departures already in the queue for the runway: the first takeoffs within minutes
	int32 Departing = 0;
	for (TActorIterator<AAircraftSpawnPoint> It(GetWorld()); It && bDeparturesAtStart; ++It)
	{
		if (!It->Route)
		{
			continue;
		}
		FAircraftSpawnRequest Request = It->MakeRequest();
		Request.TrafficMix = Request.TrafficMix ? Request.TrafficMix.Get() : TrafficMix.Get();
		for (const FTaxiStop& Stop : It->Route->GetAllStops())
		{
			if (Stop.bRequiresClearance)
			{
				Request.StartDistanceM = FMath::Max(Request.StartDistanceM, Stop.Distance / ATCUnits::MetersToCm - StartDepartureBeforeHoldM);
				break;
			}
		}
		Request.bRemoveAtRouteEnd = true;
		Request.RemoveDelaySeconds = LineUpSeconds;
		Departing += Spawner->RequestSpawn(Request).IsNone() ? 0 : 1;
	}

	ScheduleArrival();
	UE_LOG(LogATC, Display, TEXT("%s: traffic started, %d gates, %d aircraft parked (%d in place of the map's parked models), %d departures queued, %.0f arrivals an hour, clearances %s"),
		*GetName(), Gates.Num(), Parked, StandIns, Departing, ArrivalsPerHour, bAutoClearances ? TEXT("automatic") : TEXT("by the trainee"));
}

void ATrafficDirector::StopTraffic()
{
	if (!bRunning)
	{
		return;
	}
	bRunning = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
		if (UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>())
		{
			Traffic->OnAircraftEvent.RemoveDynamic(this, &ATrafficDirector::HandleAircraftEvent);
		}
		if (UAircraftSpawnSubsystem* Spawner = World->GetSubsystem<UAircraftSpawnSubsystem>())
		{
			Spawner->OnAircraftSpawned.RemoveDynamic(this, &ATrafficDirector::HandleSpawned);
		}
	}
	UE_LOG(LogATC, Display, TEXT("%s: traffic stopped (aircraft already moving finish their way)"), *GetName());
}

bool ATrafficDirector::PickType(bool bWideBody, UAircraftTypeData*& OutType, TArray<FString>& OutAirlines)
{
	// Weighted pick among the entries that fit the gate
	float Total = 0.f;
	TArray<const FTrafficMixEntry*> Fitting;
	for (const FTrafficMixEntry& Entry : TrafficMix->Entries)
	{
		const UAircraftTypeData* Type = Entry.Type.LoadSynchronous();
		if (Type && (Type->AerodromeCode >= EAerodromeCode::E) == bWideBody)
		{
			Fitting.Add(&Entry);
			Total += FMath::Max(Entry.Weight, 0.f);
		}
	}
	if (Fitting.Num() == 0)
	{
		return false;
	}
	float Roll = Random.FRandRange(0.f, Total);
	const FTrafficMixEntry* Chosen = Fitting.Last();
	for (const FTrafficMixEntry* Entry : Fitting)
	{
		Roll -= FMath::Max(Entry->Weight, 0.f);
		if (Roll <= 0.f)
		{
			Chosen = Entry;
			break;
		}
	}
	OutType = Chosen->Type.LoadSynchronous();
	OutAirlines = Chosen->Airlines;
	return OutType != nullptr;
}

int32 ATrafficDirector::PickFreeGate(bool bNeedArrivalRoute, const TSet<int32>& Skip)
{
	const UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	TArray<int32> Free;
	for (int32 Index = 0; Index < Gates.Num(); ++Index)
	{
		const AStand* Stand = Traffic ? Traffic->FindStand(Gates[Index].Stand) : nullptr;
		bool bTaken = !Stand || Stand->GetOccupant() || (bNeedArrivalRoute && !Gates[Index].ArrivalRoute.IsValid()) || Skip.Contains(Index);
		for (const TPair<FName, int32>& Pair : GateOf)
		{
			bTaken |= Pair.Value == Index;
		}
		if (!bTaken)
		{
			Free.Add(Index);
		}
	}
	return Free.Num() > 0 ? Free[Random.RandHelper(Free.Num())] : INDEX_NONE;
}

void ATrafficDirector::AddPoses(const ATaxiRoute* Route, float FromCm, float ToCm, const UAircraftTypeData* Type, bool bTailFirst, TArray<FTransform>& OutPoses)
{
	if (!Route)
	{
		return;
	}
	const float Length = Route->GetLength();
	const float Wheelbase = Type ? Type->GetWheelbaseCm() : 1560.f;
	// Past the ends the line goes on straight, so the nose gear has a place at the start and the end
	auto PointAt = [Route, Length](float Distance)
	{
		const float OnLine = FMath::Clamp(Distance, 0.f, Length);
		return Route->GetLocationAt(OnLine) + Route->GetDirectionAt(OnLine) * (Distance - OnLine);
	};
	const float From = FromCm < 0.f ? FMath::Max(Length + FromCm, 0.f) : FromCm;
	const float To = ToCm < 0.f ? Length + ToCm : FMath::Min(ToCm, Length);
	// Where the nose gear would be off the line (the tangent at an end point is zero) the heading stays as it was
	FVector Heading = FVector::ZeroVector;
	for (float Distance = From; Distance <= To + 1.f; Distance = Distance < To ? FMath::Min(Distance + 200.f, To) : To + 200.f)
	{
		const FVector Gear = PointAt(Distance);
		const FVector ToNose = PointAt(Distance + (bTailFirst ? -Wheelbase : Wheelbase)) - Gear;
		Heading = ToNose.Size2D() > 100.f ? ToNose : Heading;
		if (!Heading.IsNearlyZero())
		{
			OutPoses.Add(FTransform(Heading.Rotation(), Gear));
		}
	}
	for (const ATaxiRoute* Lane : Route->Alternatives)
	{
		AddPoses(Lane, FromCm, ToCm, Type, bTailFirst, OutPoses);
	}
}

const AATCAircraft* ATrafficDirector::FindInTheWay(const AActor* Self, const UAircraftTypeData* Type, const TArray<FTransform>& Poses) const
{
	const UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	const UTaxiNetworkSubsystem* Network = GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>();
	for (AActor* Actor : Traffic ? Traffic->GetAllAircraft() : TArray<AActor*>())
	{
		// Only what stands on a stand: aircraft on the taxi network hold reservations, which keep them apart already
		const AATCAircraft* Other = Cast<AATCAircraft>(Actor);
		if (!Other || Other == Self || Other->IsTakingOff() || Other->IsLanding() || (Network && Network->HasClaim(Other)))
		{
			continue;
		}
		// 2 m to spare: the aircraft turns a little later than the line does, and stops a little short of the end
		for (const FTransform& Pose : Poses)
		{
			if (UTaxiNetworkSubsystem::Overlap(Type, Pose, Other, 200.f))
			{
				return Other;
			}
		}
	}
	return nullptr;
}

void ATrafficDirector::ScheduleArrival()
{
	if (!bRunning || ArrivalsPerHour <= 0.f)
	{
		return;
	}
	// Spacing varies like real approaches: +-30 % around the mean
	const float Mean = 3600.f / ArrivalsPerHour;
	GetWorldTimerManager().SetTimer(ArrivalTimer, this, &ATrafficDirector::SpawnArrival, Random.FRandRange(Mean * 0.7f, Mean * 1.3f), false);
}

void ATrafficDirector::SpawnArrival()
{
	ScheduleArrival();
	UAircraftSpawnSubsystem* Spawner = GetWorld()->GetSubsystem<UAircraftSpawnSubsystem>();
	UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	UAircraftTypeData* Type = nullptr;
	TArray<FString> Airlines;
	// A free gate whose last 600 m (or the whole apron part) does not run through an aircraft standing there
	int32 Gate = INDEX_NONE;
	TSet<int32> Blocked;
	while (Spawner && Traffic && Blocked.Num() < 8 && Gate == INDEX_NONE)
	{
		const int32 Candidate = PickFreeGate(true, Blocked);
		if (Candidate == INDEX_NONE || !PickType(Gates[Candidate].bWideBody, Type, Airlines))
		{
			break;
		}
		const ATaxiRoute* Route = Traffic->FindRoute(Gates[Candidate].ArrivalRoute);
		const AStand* Stand = Traffic->FindStand(Gates[Candidate].Stand);
		TArray<FTransform> Poses;
		AddPoses(Route, -60000.f, BIG_NUMBER, Type, false, Poses);
		if (Route && Route->SpeedZones.Num() > 0)
		{
			AddPoses(Route, Route->SpeedZones[0].StartDistance, BIG_NUMBER, Type, false, Poses);
		}
		Poses.Add(Stand ? Stand->GetParkingTransform() : FTransform::Identity);
		const AATCAircraft* InTheWay = FindInTheWay(nullptr, Type, Poses);
		Gate = Stand && !InTheWay ? Candidate : INDEX_NONE;
		UE_LOG(LogATC, Display, TEXT("%s: arrival to %s %s (%d places checked)"), *GetName(), *Gates[Candidate].Stand.ToString(),
			InTheWay ? *FString::Printf(TEXT("blocked by %s"), *InTheWay->Identity->Callsign.ToString()) : TEXT("free"), Poses.Num());
		Blocked.Add(Candidate);
	}
	if (Gate == INDEX_NONE)
	{
		UE_LOG(LogATC, Display, TEXT("%s: arrival skipped, no free gate"), *GetName());
		return;
	}
	FAircraftSpawnRequest Request;
	Request.Type = Type;
	Request.Callsign = Spawner->MakeCallsign(Airlines);
	Request.AircraftClass = TrafficMix->AircraftClass.LoadSynchronous();
	Request.RouteTag = Gates[Gate].ArrivalRoute;
	Request.StartDistanceM = 0.f;
	Request.InitialSpeedKt = ArrivalSpeedKt;
	Request.bLand = bVisibleLandings;
	Request.bRemoveAtRouteEnd = false;
	GateOf.Add(Request.Callsign, Gate);
	if (Spawner->RequestSpawn(Request).IsNone())
	{
		GateOf.Remove(Request.Callsign);
		return;
	}
	// The landing runway (Route.EHAM.G7.Arr36C -> 36C) stays closed for crossings until the rollout is done
	const FString RouteName = Gates[Gate].ArrivalRoute.ToString();
	const int32 Arr = RouteName.Find(TEXT(".Arr"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (bVisibleLandings && Arr != INDEX_NONE)
	{
		const FString Runway = RouteName.Mid(Arr + 4);
		RunwayFreeAt.Add(Runway, FMath::Max(RunwayFreeAt.FindRef(Runway), Now() + LandingRunwaySeconds));
	}
	if (Traffic)
	{
		Traffic->BroadcastAircraftEvent(nullptr, TAG_ATC_Event_Traffic_GateAssigned, FString::Printf(TEXT("%s %s"), *Request.Callsign.ToString(),
			*Gates[Gate].Stand.ToString()));
	}
}

void ATrafficDirector::HandleSpawned(AATCAircraft* Aircraft, FName Callsign)
{
	const int32* Gate = GateOf.Find(Callsign);
	UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	AStand* Stand = Gate && Traffic ? Traffic->FindStand(Gates[*Gate].Stand) : nullptr;
	if (!Aircraft || !Stand)
	{
		return;
	}
	// The stand is this aircraft's from now on, also while it is still on its way: nobody else is sent there
	Aircraft->Identity->Stand = Stand;
	Aircraft->Identity->AssignedTaxiRoute = Gates[*Gate].DepartureRoute;
	Aircraft->Identity->RemoveDelaySeconds = LineUpSeconds;
	Stand->SetOccupant(Aircraft);
	if (ParkedModelStandIns.Remove(Callsign) > 0)
	{
		Aircraft->StandInFor(Gates[*Gate].ParkedModels);
	}
}

void ATrafficDirector::Depart(FName Callsign)
{
	UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	AATCAircraft* Aircraft = Traffic ? Cast<AATCAircraft>(Traffic->FindAircraft(Callsign)) : nullptr;
	const int32* Gate = GateOf.Find(Callsign);
	if (!bRunning || !Aircraft || !Gate)
	{
		return;
	}
	// No pushback into passing traffic: anything moving within 40 m of the pushback path waits the approval
	bool bPathClear = true;
	const ATaxiRoute* Push = Aircraft->Identity->Stand ? Aircraft->Identity->Stand->FindPushbackRoute(Gates[*Gate].PushbackOption) : nullptr;
	for (AActor* OtherActor : Traffic->GetAllAircraft())
	{
		const AATCAircraft* Other = Cast<AATCAircraft>(OtherActor);
		if (!Push || !Other || Other == Aircraft || !Other->Movement->HasPath())
		{
			continue;
		}
		FVector Tail, Nose;
		float HalfSpan = 0.f;
		Other->GetFootprint(Tail, Nose, HalfSpan);
		for (float Distance = 0.f; Distance <= Push->GetLength() && bPathClear; Distance += 1000.f)
		{
			bPathClear = FMath::PointDistToSegment(Push->Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World), Tail, Nose) > HalfSpan + 4000.f;
		}
	}
	// Nor through an aircraft that stands on the way: along the pushback and the first 600 m (or the whole apron part) of the taxi route
	const ATaxiRoute* Taxi = Traffic->FindRoute(Gates[*Gate].DepartureRoute);
	TArray<FTransform> Way;
	AddPoses(Push, 0.f, BIG_NUMBER, Aircraft->AircraftType, true, Way);
	AddPoses(Taxi, 0.f, FMath::Max(60000.f, Taxi && Taxi->SpeedZones.Num() > 0 ? Taxi->SpeedZones[0].EndDistance : 0.f), Aircraft->AircraftType, false, Way);
	const AATCAircraft* InTheWay = FindInTheWay(Aircraft, Aircraft->AircraftType, Way);
	if (InTheWay)
	{
		UE_LOG(LogATC, Display, TEXT("%s: pushback waits, %s stands in the way (%d places checked)"), *Callsign.ToString(),
			*InTheWay->Identity->Callsign.ToString(), Way.Num());
	}
	bPathClear = bPathClear && !InTheWay;

	// Departure management: only a few departures on their way to one runway at a time, the next one waits at its gate
	const FString RouteName = Gates[*Gate].DepartureRoute.ToString();
	const FString Runway = TEXT(".Rwy") + RouteName.Mid(RouteName.Find(TEXT(".Rwy"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 4);
	int32 Taxiing = 0;
	for (AActor* OtherActor : Traffic->GetAllAircraft())
	{
		const AATCAircraft* Other = Cast<AATCAircraft>(OtherActor);
		const EPushbackState PushState = Other ? Other->PushbackController->GetState() : EPushbackState::Parked;
		const bool bPushing = PushState != EPushbackState::Parked && PushState != EPushbackState::Completed;
		const bool bTaxiingOut = Other && Other->TaxiController->PlannedRoute && Other->TaxiController->PlannedRoute->RouteTag.ToString().EndsWith(Runway);
		Taxiing += Other && Other != Aircraft && !Other->IsTakingOff() && ((bPushing && Other->Identity->AssignedTaxiRoute.ToString().EndsWith(Runway))
			|| (bTaxiingOut && Other->TaxiController->GetState() != ETaxiState::Arrived)) ? 1 : 0;
	}
	bPathClear = bPathClear && (MaxDeparturesTaxiing <= 0 || Taxiing < MaxDeparturesTaxiing);

	Aircraft->Identity->bRemoveAtRouteEnd = true;
	Aircraft->Identity->RemoveDelaySeconds = LineUpSeconds;
	if (!bPathClear || !Traffic->SendCommand(Callsign, TAG_ATC_Cmd_Pushback, Gates[*Gate].PushbackOption))
	{
		// Something behind the stand (the pushback path is taken): try again a little later
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Callsign]() { Depart(Callsign); }), 30.f, false);
	}
}

void ATrafficDirector::HandleAircraftEvent(AActor* Actor, FName Callsign, FGameplayTag EventTag, const FString& Detail)
{
	const int32* Gate = GateOf.Find(Callsign);
	AATCAircraft* Aircraft = Cast<AATCAircraft>(Actor);

	// Arrived at the end of the arrival route (or the parallel lane it was given): on the stand, the turnaround starts
	if (EventTag == TAG_ATC_Event_Taxi_Arrived && Gate && Aircraft && Aircraft->TaxiController->PlannedRoute
		&& Aircraft->TaxiController->PlannedRoute->RouteTag == Gates[*Gate].ArrivalRoute)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Callsign]() { Depart(Callsign); }),
			Random.FRandRange(TurnaroundSeconds.X, FMath::Max(TurnaroundSeconds.Y, TurnaroundSeconds.X)), false);
	}
	// Pushed back and on its own: the gate is free for the next arrival
	else if (EventTag == TAG_ATC_Event_Pushback_Completed && Gate)
	{
		GateOf.Remove(Callsign);
	}
	// Waiting at a stop bar: the simulated colleague clears it after a moment
	else if (EventTag == TAG_ATC_Event_Taxi_StopReached && bAutoClearances && Detail.Contains(TEXT("waiting for clearance")))
	{
		const FString StopName = Detail.Replace(TEXT(" (waiting for clearance)"), TEXT(""));
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Callsign, StopName]() { TryClear(Callsign, StopName); }),
			Random.FRandRange(ClearanceDelaySeconds.X, FMath::Max(ClearanceDelaySeconds.Y, ClearanceDelaySeconds.X)), false);
	}
	else if (EventTag == TAG_ATC_Event_Traffic_Removed)
	{
		GateOf.Remove(Callsign);
	}
	// A landing appears on final (it may have waited for its runway exit): its runway stays closed for crossings from now
	else if (EventTag == TAG_ATC_Event_Traffic_Spawned && bVisibleLandings && Detail.Contains(TEXT(".Arr")))
	{
		FString Runway = Detail.Mid(Detail.Find(TEXT(".Arr"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 4);
		Runway.Split(TEXT(" "), &Runway, nullptr);
		RunwayFreeAt.Add(Runway, FMath::Max(RunwayFreeAt.FindRef(Runway), Now() + LandingRunwaySeconds));
	}
}

void ATrafficDirector::TryClear(FName Callsign, FString StopName)
{
	UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	const AATCAircraft* Aircraft = Traffic ? Cast<AATCAircraft>(Traffic->FindAircraft(Callsign)) : nullptr;
	if (!bRunning || !Aircraft || Aircraft->TaxiController->GetState() != ETaxiState::Holding)
	{
		return;   // gone, or the trainee already gave the clearance
	}

	// "Holding point 36L" uses runway 36L; "Runway crossing 18R/36L" both of its directions. One user at a time
	FString Designators = StopName;
	Designators.RemoveFromStart(TEXT("Holding point "));
	Designators.RemoveFromStart(TEXT("Runway crossing "));
	TArray<FString> Runways;
	Designators.ParseIntoArray(Runways, TEXT("/"));
	float FreeAt = 0.f;
	for (const FString& Runway : Runways)
	{
		FreeAt = FMath::Max(FreeAt, RunwayFreeAt.FindRef(Runway));
	}
	if (FreeAt > Now())
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Callsign, StopName]() { TryClear(Callsign, StopName); }),
			FreeAt - Now() + 1.f, false);
		return;
	}
	const bool bDeparture = StopName.StartsWith(TEXT("Holding point"));
	for (const FString& Runway : Runways)
	{
		RunwayFreeAt.Add(Runway, Now() + (bDeparture ? DepartureIntervalSeconds : 45.f));
	}
	Traffic->SendCommand(Callsign, TAG_ATC_Cmd_Continue, FGameplayTag());
}

FString ATrafficDirector::Describe() const
{
	const UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	FString Text = FString::Printf(TEXT("%s: %s, %d gates\n"), *GetName(), bRunning ? TEXT("running") : TEXT("stopped"), Gates.Num());
	for (int32 Index = 0; Index < Gates.Num(); ++Index)
	{
		const AStand* Stand = Traffic ? Traffic->FindStand(Gates[Index].Stand) : nullptr;
		const AActor* Occupant = Stand ? Stand->GetOccupant() : nullptr;
		FName Assigned = NAME_None;
		for (const TPair<FName, int32>& Pair : GateOf)
		{
			Assigned = Pair.Value == Index ? Pair.Key : Assigned;
		}
		Text += FString::Printf(TEXT("  %-16s %-10s %s\n"), *Gates[Index].Stand.ToString(), *Assigned.ToString(),
			Occupant ? TEXT("occupied") : TEXT("free"));
	}
	return Text;
}
