#include "AircraftSpawnSubsystem.h"
#include "AircraftIdentityComponent.h"
#include "AircraftMovementComponent.h"
#include "AircraftSpawnPoint.h"
#include "AircraftTypeData.h"
#include "ATCAircraft.h"
#include "ATCGameplayTags.h"
#include "ATCSimSettings.h"
#include "ATCUnits.h"
#include "Stand.h"
#include "TaxiControllerComponent.h"
#include "TaxiNetworkSubsystem.h"
#include "TaxiRoute.h"
#include "TrafficMixData.h"
#include "TrafficSubsystem.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	/** Landing direction: along the first 20 m of the arrival route, which starts on the runway (like AATCAircraft::StartLanding). */
	FVector RunwayDirection(const ATaxiRoute* Route, float StartCm)
	{
		return (Route->GetLocationAt(FMath::Min(StartCm + 2000.f, Route->GetLength())) - Route->GetLocationAt(StartCm)).GetSafeNormal2D();
	}
}

bool UAircraftSpawnSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAircraftSpawnSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(QueueTimer);
	}
	Super::Deinitialize();
}

void UAircraftSpawnSubsystem::RegisterSpawnPoint(AAircraftSpawnPoint* SpawnPoint) { SpawnPoints.AddUnique(SpawnPoint); }
void UAircraftSpawnSubsystem::UnregisterSpawnPoint(AAircraftSpawnPoint* SpawnPoint) { SpawnPoints.Remove(SpawnPoint); }

AAircraftSpawnPoint* UAircraftSpawnSubsystem::FindSpawnPoint(FGameplayTag SpawnPointTag) const
{
	for (const TWeakObjectPtr<AAircraftSpawnPoint>& Entry : SpawnPoints)
	{
		if (Entry.IsValid() && Entry->SpawnPointTag == SpawnPointTag)
		{
			return Entry.Get();
		}
	}
	return nullptr;
}

bool UAircraftSpawnSubsystem::IsCallsignInUse(FName Callsign) const
{
	const UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	if (Traffic && Traffic->FindAircraft(Callsign))
	{
		return true;
	}
	return Queue.ContainsByPredicate([Callsign](const FAircraftSpawnRequest& Waiting) { return Waiting.Callsign == Callsign; });
}

FName UAircraftSpawnSubsystem::MakeCallsign(const TArray<FString>& Airlines)
{
	for (int32 Attempt = 0; Attempt < 100; ++Attempt)
	{
		const FString Airline = Airlines.Num() > 0 ? Airlines[Random.RandHelper(Airlines.Num())] : FString(TEXT("ATC"));
		const FName Callsign(*FString::Printf(TEXT("%s%d"), *Airline, Random.RandRange(100, 1999)));
		if (!IsCallsignInUse(Callsign))
		{
			return Callsign;
		}
	}
	return NAME_None;
}

bool UAircraftSpawnSubsystem::Resolve(FAircraftSpawnRequest& Request)
{
	UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	if (!Request.Route && Traffic && Request.RouteTag.IsValid())
	{
		Request.Route = Traffic->FindRoute(Request.RouteTag);
	}
	if (!Request.Route && !Request.StandTag.IsValid())
	{
		UE_LOG(LogATC, Warning, TEXT("Spawn %s: no route %s and no stand."), *Request.Callsign.ToString(), *Request.RouteTag.ToString());
		return false;
	}

	// Type and airline from the mix when the request does not name them
	TArray<FString> Airlines;
	if (Request.TrafficMix)
	{
		if (const FTrafficMixEntry* Entry = Request.TrafficMix->Pick(Random))
		{
			Airlines = Entry->Airlines;
			if (!Request.Type)
			{
				Request.Type = Entry->Type.LoadSynchronous();
			}
		}
		if (!Request.AircraftClass)
		{
			Request.AircraftClass = Request.TrafficMix->AircraftClass.LoadSynchronous();
		}
	}
	if (!Request.Type)
	{
		UE_LOG(LogATC, Warning, TEXT("Spawn %s: no aircraft type and no traffic mix."), *Request.Callsign.ToString());
		return false;
	}
	if (!Request.AircraftClass)
	{
		Request.AircraftClass = LoadClass<AATCAircraft>(nullptr, TEXT("/ATCSim/Blueprints/BP_Aircraft_Base.BP_Aircraft_Base_C"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!Request.AircraftClass)
	{
		Request.AircraftClass = AATCAircraft::StaticClass();
	}
	if (Request.Callsign.IsNone())
	{
		Request.Callsign = MakeCallsign(Airlines);
	}
	return !Request.Callsign.IsNone();
}

FName UAircraftSpawnSubsystem::RequestSpawn(const FAircraftSpawnRequest& InRequest)
{
	// Resolved once, so a queued aircraft keeps its callsign and type while it waits
	FAircraftSpawnRequest Request = InRequest;
	if (!Resolve(Request))
	{
		return NAME_None;
	}
	if (IsCallsignInUse(Request.Callsign))
	{
		UE_LOG(LogATC, Warning, TEXT("Spawn %s: callsign already in use."), *Request.Callsign.ToString());
		return NAME_None;
	}

	AActor* Blocker = nullptr;
	if (TrySpawn(Request, Blocker))
	{
		return Request.Callsign;
	}
	if (!Request.Route && !Blocker)
	{
		return NAME_None;   // stand not found: waiting does not help
	}

	Queue.Add(Request);
	UE_LOG(LogATC, Display, TEXT("%s %s %s"), *Request.Callsign.ToString(), *TAG_ATC_Event_Traffic_Queued.GetTag().ToString(),
		Blocker ? *FString::Printf(TEXT("(behind %s)"), *Blocker->GetName()) : TEXT(""));
	if (UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>())
	{
		Traffic->BroadcastAircraftEvent(nullptr, TAG_ATC_Event_Traffic_Queued, Request.Callsign.ToString());
	}
	if (!GetWorld()->GetTimerManager().IsTimerActive(QueueTimer))
	{
		GetWorld()->GetTimerManager().SetTimer(QueueTimer, this, &UAircraftSpawnSubsystem::ProcessQueue, 0.5f, true);
	}
	return Request.Callsign;
}

AATCAircraft* UAircraftSpawnSubsystem::TrySpawn(const FAircraftSpawnRequest& InRequest, AActor*& OutBlocker)
{
	OutBlocker = nullptr;
	FAircraftSpawnRequest Request = InRequest;
	if ((!Request.Route && !Request.StandTag.IsValid()) || !Request.Type || !Request.AircraftClass || Request.Callsign.IsNone())
	{
		if (!Resolve(Request))
		{
			return nullptr;
		}
	}
	UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	if (!Traffic)
	{
		return nullptr;
	}

	// On a stand: the ordinary scenario spawn, with the route as taxi route after pushback
	if (Request.StandTag.IsValid())
	{
		AStand* Stand = Traffic->FindStand(Request.StandTag);
		if (Stand && Stand->GetOccupant())
		{
			OutBlocker = Stand->GetOccupant();
			return nullptr;
		}
		AATCAircraft* Parked = Traffic->SpawnAircraft(Request.AircraftClass, Request.Type, Request.Callsign, Request.StandTag,
			Request.Route ? Request.Route->RouteTag : Request.RouteTag, true);
		if (Parked)
		{
			Parked->Identity->bRemoveAtRouteEnd = Request.bRemoveAtRouteEnd;
			Parked->Identity->RemoveDelaySeconds = Request.RemoveDelaySeconds;
			OnAircraftSpawned.Broadcast(Parked, Request.Callsign);
		}
		return Parked;
	}

	// The footprint the new aircraft would take, checked against every other aircraft
	ATaxiRoute* Route = Request.Route;
	const float Length = Route->GetLength();
	const float Start = FMath::Clamp(Request.StartDistanceM * ATCUnits::MetersToCm, 0.f, Length);
	const float Wheelbase = Request.Type->GetWheelbaseCm();
	const FVector MainGear = Route->Spline->GetLocationAtDistanceAlongSpline(Start, ESplineCoordinateSpace::World);
	FVector Heading = (Route->Spline->GetLocationAtDistanceAlongSpline(FMath::Min(Start + Wheelbase, Length), ESplineCoordinateSpace::World) - MainGear).GetSafeNormal();
	if (Heading.IsNearlyZero())
	{
		Heading = Route->GetDirectionAt(Start);
	}
	const float NoseOffset = Request.Type->GetNoseOffsetCm();
	const FVector Nose = MainGear + Heading * NoseOffset;
	const FVector Tail = Nose - Heading * Request.Type->GetLengthCm();
	// On the ground: room around the place and nobody's reservation on it. A landing holds in the stack until it can
	// come; on final it asks for its runway exit itself, and goes around when it does not get it
	const float Clearance = GetDefault<UATCSimSettings>()->SpawnClearanceM * ATCUnits::MetersToCm;
	UTaxiNetworkSubsystem* Network = GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>();
	if (!Request.bLand && (!Traffic->IsSpaceClear(Tail, Nose, Request.Type->GetHalfSpanCm(), Clearance, OutBlocker)
		|| (Network && !Network->IsStretchFree(Route, Start + NoseOffset - Request.Type->GetLengthCm() - Clearance, Start + NoseOffset + Clearance,
			Request.Type->GetHalfSpanCm(), OutBlocker))))
	{
		return nullptr;
	}
	if (Request.bLand && HoldLanding(Request, Route, Start, OutBlocker))
	{
		return nullptr;
	}

	const FTransform Transform(Heading.Rotation(), MainGear);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	AATCAircraft* Aircraft = GetWorld()->SpawnActor<AATCAircraft>(Request.AircraftClass, Transform, Params);
	if (!Aircraft)
	{
		return nullptr;
	}
	Aircraft->AircraftType = Request.Type;
	Aircraft->Identity->Callsign = Request.Callsign;
	Aircraft->Identity->AssignedTaxiRoute = Route->RouteTag;
	Aircraft->Identity->bRemoveAtRouteEnd = Request.bRemoveAtRouteEnd;
	Aircraft->Identity->RemoveDelaySeconds = Request.RemoveDelaySeconds;
	Aircraft->FinishSpawning(Transform);

	// On the route from the first frame, also when it waits for Cmd.Taxi: other traffic then sees it there
	Aircraft->Movement->SetPath(Route->Spline, ETravelDirection::Forward, true, Start, true);
	if (Network && !Request.bLand)
	{
		FTaxiClaimRequest Body;
		Body.Route = Route;
		Body.RearCm = Start + NoseOffset - Request.Type->GetLengthCm();
		Body.FrontCm = Start + NoseOffset;
		Body.HalfSpanCm = Request.Type->GetHalfSpanCm();
		Body.LengthCm = Request.Type->GetLengthCm();
		Body.NoseOffsetCm = NoseOffset;
		Network->ClaimBody(Aircraft, Body);
	}
	float End = Request.EndDistanceM >= 0.f ? Request.EndDistanceM * ATCUnits::MetersToCm : -1.f;
	if (!Request.EndStop.IsNone())
	{
		End = Route->FindNamedDistance(Request.EndStop);
		if (End < 0.f)
		{
			UE_LOG(LogATC, Warning, TEXT("Spawn %s: no stop or waypoint \"%s\" on %s, taxiing to the end."), *Request.Callsign.ToString(),
				*Request.EndStop.ToString(), *Route->RouteTag.ToString());
		}
	}
	if (Request.bLand)
	{
		Aircraft->StartLanding(Route, Start, End, Request.InitialSpeedKt);
		RecentLandings.Add({ Route->GetLocationAt(Start), RunwayDirection(Route, Start), static_cast<float>(GetWorld()->GetTimeSeconds()), Aircraft });
	}
	else if (Request.bStartTaxi)
	{
		Aircraft->TaxiController->StartTaxiBetween(Route, Start, End);
		Aircraft->Movement->Speed = FMath::Max(Request.InitialSpeedKt, 0.f) * ATCUnits::KnotsToCms;
	}
	ReportSpawned(Aircraft, Route, Start);
	OnAircraftSpawned.Broadcast(Aircraft, Request.Callsign);
	return Aircraft;
}

bool UAircraftSpawnSubsystem::HoldLanding(const FAircraftSpawnRequest& Request, ATaxiRoute* Route, float StartCm, AActor*& OutBlocker)
{
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Start = Route->GetLocationAt(StartCm);
	const FVector Direction = RunwayDirection(Route, StartCm);

	// Landings on the same runway (the same direction, joining it on one centre line) come at least the spacing apart
	RecentLandings.RemoveAll([Now, Settings](const FRecentLanding& Recent) { return Now - Recent.Time >= Settings->LandingSpacingSeconds; });
	for (const FRecentLanding& Recent : RecentLandings)
	{
		if ((Direction | Recent.Direction) > 0.98f && FMath::Abs(FVector::CrossProduct(Direction, Recent.Start - Start).Z) < 10000.f)
		{
			OutBlocker = Recent.Aircraft.Get();
			return true;
		}
	}

	// One of its runway exits free: nobody on it, and nobody who has waited for it longer than a landing's head start
	UTaxiNetworkSubsystem* Network = GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>();
	const TArray<FTaxiClaimRequest> Exits = UTaxiNetworkSubsystem::RunwayExitRequests(Route, StartCm, Request.Type);
	bool bExitFree = !Network || Exits.Num() == 0;
	for (const FTaxiClaimRequest& Exit : Exits)
	{
		bExitFree = bExitFree || Network->ClaimStretch(nullptr, Exit, OutBlocker, true);
	}
	const float Waited = Now - HoldingSince.FindOrAdd(Request.Callsign, Now);
	if (!bExitFree && Waited < Settings->MaxHoldingSeconds)
	{
		return true;
	}
	if (!bExitFree)
	{
		UE_LOG(LogATC, Display, TEXT("%s leaves the holding stack after %.0f s, its runway exit still taken"), *Request.Callsign.ToString(), Waited);
	}
	HoldingSince.Remove(Request.Callsign);
	OutBlocker = nullptr;
	return false;
}

void UAircraftSpawnSubsystem::ReportSpawned(AATCAircraft* Aircraft, ATaxiRoute* Route, float StartCm)
{
	const FString Detail = FString::Printf(TEXT("%s at %.0f m (%s)"), *Route->RouteTag.ToString(), StartCm / 100.f,
		Aircraft->AircraftType ? *Aircraft->AircraftType->GetName() : TEXT("no type"));
	UE_LOG(LogATC, Display, TEXT("%s %s %s"), *Aircraft->Identity->Callsign.ToString(), *TAG_ATC_Event_Traffic_Spawned.GetTag().ToString(), *Detail);
	if (UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>())
	{
		Traffic->BroadcastAircraftEvent(Aircraft, TAG_ATC_Event_Traffic_Spawned, Detail);
	}
}

void UAircraftSpawnSubsystem::ProcessQueue()
{
	// First come, first served per place: a later request for the same place waits for the earlier one
	TArray<FString> Blocked;
	for (int32 Index = 0; Index < Queue.Num();)
	{
		const FAircraftSpawnRequest& Request = Queue[Index];
		const FString Place = Request.Route ? FString::Printf(TEXT("%s@%d"), *Request.Route->GetName(), FMath::RoundToInt(Request.StartDistanceM))
			: Request.StandTag.ToString();
		AActor* Blocker = nullptr;
		if (!Blocked.Contains(Place) && TrySpawn(Request, Blocker))
		{
			Queue.RemoveAt(Index);
			continue;
		}
		Blocked.AddUnique(Place);
		++Index;
	}
	if (Queue.Num() == 0)
	{
		GetWorld()->GetTimerManager().ClearTimer(QueueTimer);
	}
}

bool UAircraftSpawnSubsystem::Despawn(FName Callsign)
{
	const int32 Removed = Queue.RemoveAll([Callsign](const FAircraftSpawnRequest& Waiting) { return Waiting.Callsign == Callsign; });
	UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	AActor* Aircraft = Traffic ? Traffic->FindAircraft(Callsign) : nullptr;
	if (!Aircraft)
	{
		return Removed > 0;
	}
	if (const UAircraftIdentityComponent* Identity = Aircraft->FindComponentByClass<UAircraftIdentityComponent>())
	{
		if (Identity->Stand && Identity->Stand->GetOccupant() == Aircraft)
		{
			Identity->Stand->Vacate();
		}
	}
	Traffic->BroadcastAircraftEvent(Aircraft, TAG_ATC_Event_Traffic_Removed, TEXT("despawned"));
	UE_LOG(LogATC, Display, TEXT("%s %s despawned"), *Callsign.ToString(), *TAG_ATC_Event_Traffic_Removed.GetTag().ToString());
	Aircraft->Destroy();
	return true;
}

TArray<FName> UAircraftSpawnSubsystem::GetQueuedCallsigns() const
{
	TArray<FName> Result;
	for (const FAircraftSpawnRequest& Request : Queue)
	{
		Result.Add(Request.Callsign);
	}
	return Result;
}
