#include "TrafficSubsystem.h"
#include "AircraftCommands.h"
#include "AircraftIdentityComponent.h"
#include "AircraftMovementComponent.h"
#include "AircraftTypeData.h"
#include "ATCAircraft.h"
#include "ATCGameplayTags.h"
#include "ATCUnits.h"
#include "Stand.h"
#include "TaxiControllerComponent.h"
#include "TaxiNetworkSubsystem.h"
#include "TaxiRoute.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"

bool UTrafficSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTrafficSubsystem::RegisterAircraft(AActor* InAircraft) { Aircraft.AddUnique(InAircraft); }
void UTrafficSubsystem::UnregisterAircraft(AActor* InAircraft) { Aircraft.Remove(InAircraft); }
void UTrafficSubsystem::RegisterStand(AStand* Stand) { Stands.AddUnique(Stand); }
void UTrafficSubsystem::UnregisterStand(AStand* Stand) { Stands.Remove(Stand); }
void UTrafficSubsystem::RegisterRoute(ATaxiRoute* Route)
{
	Routes.AddUnique(Route);
	if (UTaxiNetworkSubsystem* Network = GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>())
	{
		Network->MarkDirty();
	}
}

void UTrafficSubsystem::UnregisterRoute(ATaxiRoute* Route)
{
	Routes.Remove(Route);
	if (UTaxiNetworkSubsystem* Network = GetWorld() ? GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>() : nullptr)
	{
		Network->MarkDirty();
	}
}

TArray<ATaxiRoute*> UTrafficSubsystem::GetAllRoutes() const
{
	TArray<ATaxiRoute*> Result;
	for (const TWeakObjectPtr<ATaxiRoute>& Entry : Routes)
	{
		if (Entry.IsValid())
		{
			Result.Add(Entry.Get());
		}
	}
	return Result;
}

AActor* UTrafficSubsystem::FindAircraft(FName Callsign) const
{
	for (const TWeakObjectPtr<AActor>& Entry : Aircraft)
	{
		AActor* Actor = Entry.Get();
		if (Actor && Actor->Implements<UAircraftInfo>() && IAircraftInfo::Execute_GetCallsign(Actor) == Callsign)
		{
			return Actor;
		}
	}
	return nullptr;
}

AStand* UTrafficSubsystem::FindStand(FGameplayTag StandTag) const
{
	for (const TWeakObjectPtr<AStand>& Entry : Stands)
	{
		if (Entry.IsValid() && Entry->StandTag == StandTag)
		{
			return Entry.Get();
		}
	}
	return nullptr;
}

ATaxiRoute* UTrafficSubsystem::FindRoute(FGameplayTag RouteTag) const
{
	for (const TWeakObjectPtr<ATaxiRoute>& Entry : Routes)
	{
		if (Entry.IsValid() && Entry->RouteTag == RouteTag)
		{
			return Entry.Get();
		}
	}
	return nullptr;
}

TArray<AActor*> UTrafficSubsystem::GetAllAircraft() const
{
	TArray<AActor*> Result;
	for (const TWeakObjectPtr<AActor>& Entry : Aircraft)
	{
		if (Entry.IsValid())
		{
			Result.Add(Entry.Get());
		}
	}
	return Result;
}

AATCAircraft* UTrafficSubsystem::SpawnAircraft(TSubclassOf<AATCAircraft> AircraftClass, UAircraftTypeData* Type, FName Callsign,
	FGameplayTag StandTag, FGameplayTag TaxiRoute, bool bAutoTaxiAfterPushback)
{
	AStand* Stand = FindStand(StandTag);
	if (!Stand)
	{
		UE_LOG(LogATC, Error, TEXT("SpawnAircraft %s: stand %s not found."), *Callsign.ToString(), *StandTag.ToString());
		return nullptr;
	}
	if (Stand->GetOccupant())
	{
		UE_LOG(LogATC, Warning, TEXT("SpawnAircraft %s: stand %s is occupied."), *Callsign.ToString(), *StandTag.ToString());
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	UClass* Class = AircraftClass ? AircraftClass.Get() : AATCAircraft::StaticClass();
	AATCAircraft* NewAircraft = GetWorld()->SpawnActor<AATCAircraft>(Class, Stand->GetParkingTransform(), Params);
	if (!NewAircraft)
	{
		return nullptr;
	}

	NewAircraft->AircraftType = Type;
	NewAircraft->Identity->Callsign = Callsign;
	NewAircraft->Identity->AssignedTaxiRoute = TaxiRoute;
	NewAircraft->Identity->bAutoTaxiAfterPushback = bAutoTaxiAfterPushback;
	NewAircraft->Identity->Stand = Stand;
	NewAircraft->FinishSpawning(Stand->GetParkingTransform());
	Stand->SetOccupant(NewAircraft);

	UE_LOG(LogATC, Display, TEXT("%s spawned at %s (%s)"), *Callsign.ToString(), *StandTag.ToString(), Type ? *Type->GetName() : TEXT("no type"));
	return NewAircraft;
}

namespace
{
	const float PathStepCm = 500.f;

	/** Points every 5 m from the nose along the stretch of route the aircraft is about to cover; empty when not taxiing. */
	TArray<FVector> PathAhead(const AATCAircraft* Aircraft, float LookAheadCm)
	{
		TArray<FVector> Points;
		const ETaxiState State = Aircraft->TaxiController->GetState();
		const USplineComponent* Path = Aircraft->Movement->Path;
		if (!Path || (State != ETaxiState::Taxiing && State != ETaxiState::Braking) || !Aircraft->TaxiController->bKeepSeparation)
		{
			return Points;
		}
		const float Nose = Aircraft->Movement->Distance + Aircraft->GetNoseOffsetCm();
		const float End = FMath::Min(Aircraft->TaxiController->GetEndDistance() + Aircraft->GetNoseOffsetCm(), Nose + LookAheadCm);
		for (float D = Nose; D <= End; D += PathStepCm)
		{
			// Past the end of the spline the nose goes straight on (the line-up on a runway)
			const float OnSpline = FMath::Min(D, Path->GetSplineLength());
			const FVector Direction = Path->GetDirectionAtDistanceAlongSpline(OnSpline, ESplineCoordinateSpace::World);
			Points.Add(Path->GetLocationAtDistanceAlongSpline(OnSpline, ESplineCoordinateSpace::World) + Direction * (D - OnSpline));
		}
		return Points;
	}

	/**
	 * Distance along the path points to the first one inside the body: alongside the fuselage (between tail and nose)
	 * and within Clearance of its axis (the wings). Measured this way a follower keeps its distance to the tail itself.
	 * -1 = clear.
	 */
	float FirstConflict(const TArray<FVector>& Points, const FVector& Tail, const FVector& Nose, float Clearance)
	{
		// The space just in front of the nose counts too, so a crossing aircraft is seen before it is on the path
		const FVector Axis = Nose - Tail;
		const float AxisSquared = FMath::Max(Axis.SizeSquared(), 1.f);
		const float Front = 1.f + Clearance / FMath::Sqrt(AxisSquared);
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const float Along = FVector::DotProduct(Points[Index] - Tail, Axis) / AxisSquared;
			if (Along >= 0.f && Along <= Front && FVector::Dist(Points[Index], Tail + Axis * Along) < Clearance)
			{
				// The body starts somewhere between this point and the one before: take the safe side
				return FMath::Max(Index - 1, 0) * PathStepCm;
			}
		}
		return -1.f;
	}
}

AActor* UTrafficSubsystem::FindTrafficAhead(const AActor* SelfActor, float LookAheadCm, float& OutGapCm) const
{
	OutGapCm = BIG_NUMBER;
	const AATCAircraft* Self = Cast<AATCAircraft>(SelfActor);
	const TArray<FVector> Ahead = Self ? PathAhead(Self, LookAheadCm) : TArray<FVector>();
	if (Ahead.Num() == 0)
	{
		return nullptr;
	}
	FVector SelfTail, SelfNose;
	float SelfHalfSpan = 0.f;
	Self->GetFootprint(SelfTail, SelfNose, SelfHalfSpan);
	const UTaxiNetworkSubsystem* Network = GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>();

	AActor* Blocker = nullptr;
	for (const TWeakObjectPtr<AActor>& Entry : Aircraft)
	{
		AActor* OtherActor = Entry.Get();
		const AATCAircraft* Other = Cast<AATCAircraft>(OtherActor);
		if (!Other || Other == Self || !Other->Movement->HasPath())
		{
			continue;
		}
		FVector Tail, Nose;
		float HalfSpan = 0.f;
		Other->GetFootprint(Tail, Nose, HalfSpan);
		const float Clearance = FMath::Max(SelfHalfSpan, HalfSpan);
		if (FVector::Dist(SelfNose, (Tail + Nose) * 0.5f) > LookAheadCm + FVector::Dist(Tail, Nose) + Clearance)
		{
			continue;
		}
		const float Gap = FirstConflict(Ahead, Tail, Nose, Clearance);
		if (Gap < 0.f || Gap >= OutGapCm)
		{
			continue;
		}
		// Crossing, beside or head-on traffic with a reservation is kept apart by the reservations; here only what goes
		// the same way is followed (and whatever has no reservation, like a parked aircraft, is stopped for)
		const int32 At = FMath::Clamp(FMath::RoundToInt(Gap / PathStepCm), 0, FMath::Max(Ahead.Num() - 2, 0));
		const FVector Along = Ahead.Num() > 1 ? (Ahead[At + 1] - Ahead[At]).GetSafeNormal2D() : Self->GetActorForwardVector();
		const FVector OtherTravel = Other->GetActorForwardVector() * (Other->Movement->bNoseAlongTravel ? 1.f : -1.f);
		if (Network && Network->HasClaim(Other) && (OtherTravel.GetSafeNormal2D() | Along) < 0.5f)
		{
			continue;
		}
		// Both heading for the same spot (a crossing or a merge): the one closer to it goes first. An aircraft
		// following another never has it on its own path the other way round, so it always waits
		const float OtherGap = FirstConflict(PathAhead(Other, LookAheadCm), SelfTail, SelfNose, Clearance);
		if (OtherGap >= 0.f && (Gap < OtherGap || (Gap == OtherGap && Self->GetName() < Other->GetName())))
		{
			continue;
		}
		OutGapCm = Gap;
		Blocker = OtherActor;
	}
	return Blocker;
}


bool UTrafficSubsystem::IsSpaceClear(const FVector& Tail, const FVector& Nose, float HalfSpanCm, float ClearanceCm, AActor*& OutBlocker) const
{
	OutBlocker = nullptr;
	const FVector Direction = (Nose - Tail).GetSafeNormal();
	const FVector Back = Tail - Direction * ClearanceCm;
	const FVector Front = Nose + Direction * ClearanceCm;
	for (const TWeakObjectPtr<AActor>& Entry : Aircraft)
	{
		const AATCAircraft* Other = Cast<AATCAircraft>(Entry.Get());
		if (!Other)
		{
			continue;
		}
		FVector OtherTail, OtherNose, OnSelf, OnOther;
		float OtherHalfSpan = 0.f;
		Other->GetFootprint(OtherTail, OtherNose, OtherHalfSpan);
		FMath::SegmentDistToSegmentSafe(Back, Front, OtherTail, OtherNose, OnSelf, OnOther);
		if (FVector::Dist(OnSelf, OnOther) < FMath::Max(HalfSpanCm, OtherHalfSpan))
		{
			OutBlocker = Entry.Get();
			return false;
		}
	}
	return true;
}

bool UTrafficSubsystem::SendCommand(FName Callsign, FGameplayTag Command, FGameplayTag Parameter)
{
	AActor* Target = FindAircraft(Callsign);
	if (!Target || !Target->Implements<UAircraftCommands>())
	{
		UE_LOG(LogATC, Warning, TEXT("Command %s: aircraft %s not found."), *Command.ToString(), *Callsign.ToString());
		return false;
	}

	const UAircraftIdentityComponent* Identity = Target->FindComponentByClass<UAircraftIdentityComponent>();
	ATaxiRoute* AssignedRoute = Identity ? FindRoute(Identity->AssignedTaxiRoute) : nullptr;

	bool bAccepted = false;
	if (Command == TAG_ATC_Cmd_Pushback)
	{
		FPushbackRequest Request;
		Request.OptionTag = Parameter;
		Request.TaxiRouteAfter = AssignedRoute;
		Request.bAutoTaxi = Identity ? Identity->bAutoTaxiAfterPushback : true;
		bAccepted = IAircraftCommands::Execute_RequestPushback(Target, Request);
	}
	else if (Command == TAG_ATC_Cmd_Taxi)
	{
		ATaxiRoute* Route = Parameter.IsValid() ? FindRoute(Parameter) : AssignedRoute;
		bAccepted = IAircraftCommands::Execute_StartTaxi(Target, Route);
	}
	else if (Command == TAG_ATC_Cmd_HoldPosition)
	{
		bAccepted = IAircraftCommands::Execute_HoldPosition(Target);
	}
	else if (Command == TAG_ATC_Cmd_Continue)
	{
		bAccepted = IAircraftCommands::Execute_ContinueTaxi(Target);
	}

	UE_LOG(LogATC, Display, TEXT("%s %s %s: %s"), *Callsign.ToString(), *Command.ToString(), *Parameter.ToString(), bAccepted ? TEXT("accepted") : TEXT("rejected"));
	return bAccepted;
}

void UTrafficSubsystem::BroadcastAircraftEvent(AActor* InAircraft, const FGameplayTag& EventTag, const FString& Detail)
{
	const FName Callsign = InAircraft && InAircraft->Implements<UAircraftInfo>() ? IAircraftInfo::Execute_GetCallsign(InAircraft) : NAME_None;
	OnAircraftEvent.Broadcast(InAircraft, Callsign, EventTag, Detail);
}
