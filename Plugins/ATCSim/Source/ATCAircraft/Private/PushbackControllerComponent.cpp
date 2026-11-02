#include "PushbackControllerComponent.h"
#include "AircraftCommands.h"
#include "AircraftMovementComponent.h"
#include "AircraftTypeData.h"
#include "ATCUnits.h"
#include "Stand.h"
#include "TaxiControllerComponent.h"
#include "TaxiNetworkSubsystem.h"
#include "TaxiRoute.h"
#include "Engine/World.h"
#include "TimerManager.h"

UPushbackControllerComponent::UPushbackControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UPushbackControllerComponent::BeginPlay()
{
	Super::BeginPlay();

	Movement = GetOwner()->FindComponentByClass<UAircraftMovementComponent>();
	if (Movement)
	{
		Movement->AddTickPrerequisiteComponent(this);
		Movement->OnPathEndReached.AddDynamic(this, &UPushbackControllerComponent::HandlePathEnd);
	}
}

void UPushbackControllerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Also called when the world is already gone (garbage collection after a level change): then there is no timer to clear
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(Timer);
	}
	if (IsValid(Tug))
	{
		Tug->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

float UPushbackControllerComponent::GetPushSpeedCms() const
{
	return AircraftType ? AircraftType->GetPushbackSpeedCms() : 3.f * ATCUnits::KnotsToCms;
}

void UPushbackControllerComponent::SetState(EPushbackState NewState)
{
	if (NewState != State)
	{
		const EPushbackState OldState = State;
		State = NewState;
		OnPushbackStateChanged.Broadcast(OldState, NewState);
	}
}

bool UPushbackControllerComponent::RequestPushback(const FPushbackRequest& Request, AStand* FromStand)
{
	if (State != EPushbackState::Parked && State != EPushbackState::Completed)
	{
		return false;
	}

	ATaxiRoute* Route = FromStand ? FromStand->FindPushbackRoute(Request.OptionTag) : nullptr;
	if (!Route || !Movement)
	{
		UE_LOG(LogATC, Warning, TEXT("%s: no pushback route %s at stand %s."), *GetOwner()->GetName(),
			*Request.OptionTag.ToString(), FromStand ? *FromStand->StandTag.ToString() : TEXT("none"));
		return false;
	}

	ActiveRequest = Request;
	Stand = FromStand;
	PushRoute = Route;
	State = EPushbackState::Parked;
	SetState(EPushbackState::Requested);

	if (!Request.bRequiresApproval)
	{
		StartTugConnect();
	}
	return true;
}

void UPushbackControllerComponent::ApprovePushback(bool bApproved)
{
	if (State != EPushbackState::Requested)
	{
		return;
	}
	if (bApproved)
	{
		StartTugConnect();
	}
	else
	{
		SetState(EPushbackState::Parked);
	}
}

void UPushbackControllerComponent::StartTugConnect()
{
	SetState(EPushbackState::TugConnecting);

	if (TugClass)
	{
		USceneComponent* AttachTo = GetOwner()->GetRootComponent();
		TInlineComponentArray<USceneComponent*> Components(GetOwner());
		for (USceneComponent* Component : Components)
		{
			if (Component->GetFName() == TEXT("NoseGear"))
			{
				AttachTo = Component;
			}
		}
		Tug = GetWorld()->SpawnActor<AActor>(TugClass, AttachTo->GetComponentTransform());
		if (Tug)
		{
			Tug->AttachToComponent(AttachTo, FAttachmentTransformRules::KeepWorldTransform);
		}
	}

	GetWorld()->GetTimerManager().SetTimer(Timer, this, &UPushbackControllerComponent::StartPushing, FMath::Max(TugConnectTime, 0.01f));
}

void UPushbackControllerComponent::StartPushing()
{
	ATaxiRoute* Route = PushRoute.Get();
	if (!Route)
	{
		SetState(EPushbackState::Parked);
		return;
	}

	// The route starts at the stand: the aircraft drives it forward along the spline, nose pointing back to the gate
	const float StartDistance = UAircraftMovementComponent::GetDistanceClosestTo(Route->Spline, GetOwner()->GetActorLocation());

	// The whole path must be free, from the nose at the gate to the tail at the end of the push, and nobody may come
	// towards the aircraft on the taxi route it takes afterwards: otherwise the tug stays connected and asks again every second
	if (UTaxiNetworkSubsystem* Network = GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>())
	{
		FTaxiClaimRequest Request;
		Request.Route = Route;
		Request.LengthCm = AircraftType ? AircraftType->GetLengthCm() : 3950.f;
		Request.NoseOffsetCm = AircraftType ? AircraftType->GetNoseOffsetCm() : 2034.f;
		Request.HalfSpanCm = AircraftType ? AircraftType->GetHalfSpanCm() : 1790.f;
		Request.bTailFirst = true;
		Request.RearCm = StartDistance - Request.NoseOffsetCm - 1000.f;
		Request.FrontCm = Route->GetLength() + Request.LengthCm - Request.NoseOffsetCm + 1000.f;
		FTaxiClaimRequest After = Request;
		After.Route = ActiveRequest.TaxiRouteAfter;
		After.bTailFirst = false;
		if (After.Route)
		{
			const float TaxiStart = UAircraftMovementComponent::GetDistanceClosestTo(After.Route->Spline, Route->GetLocationAt(Route->GetLength()));
			After.RearCm = TaxiStart - After.LengthCm;
			After.FrontCm = TaxiStart + After.NoseOffsetCm + FMath::Max(ReserveAheadM * ATCUnits::MetersToCm, 5000.f);
		}
		AActor* Blocker = nullptr;
		const bool bAfterFree = !After.Route || Network->ReserveTwoWay(GetOwner(), After, Blocker);
		const bool bFree = bAfterFree && Network->ClaimStretch(GetOwner(), Request, Blocker);
		if (!bFree)
		{
			Network->CancelReservations(GetOwner());
			if (Blocker && Blocker != WaitingFor.Get())
			{
				const FString Self = GetOwner()->Implements<UAircraftInfo>() ? IAircraftInfo::Execute_GetCallsign(GetOwner()).ToString() : GetOwner()->GetName();
				const FString Other = Blocker->Implements<UAircraftInfo>() ? IAircraftInfo::Execute_GetCallsign(Blocker).ToString() : Blocker->GetName();
				UE_LOG(LogATC, Display, TEXT("%s: pushback waits for %s"), *Self, *Other);
			}
			WaitingFor = Blocker;
			GetWorld()->GetTimerManager().SetTimer(Timer, this, &UPushbackControllerComponent::StartPushing, 1.f);
			return;
		}
	}
	WaitingFor = nullptr;
	Movement->SetPath(Route->Spline, ETravelDirection::Forward, false, StartDistance);
	Movement->SetTargetSpeed(GetPushSpeedCms());
	SetState(EPushbackState::Pushing);

	if (AStand* FromStand = Stand.Get())
	{
		FromStand->Vacate();
	}
}

void UPushbackControllerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (State != EPushbackState::Pushing && State != EPushbackState::Stopping)
	{
		return;
	}

	const float Decel = AircraftType ? AircraftType->GetDecelerationCms2() * 0.5f : 50.f;  // gentle with a tug
	const float Remaining = Movement->GetRemainingDistance();
	float Target = FMath::Min(GetPushSpeedCms(), FMath::Sqrt(2.f * Decel * Remaining));
	Target = FMath::Max(Target, FMath::Min(15.f, Remaining / FMath::Max(DeltaTime, 0.001f)));
	Movement->SetTargetSpeed(Target);

	if (State == EPushbackState::Pushing && Remaining <= Movement->GetBrakingDistance() + 300.f)
	{
		SetState(EPushbackState::Stopping);
	}
}

void UPushbackControllerComponent::HandlePathEnd()
{
	if (State != EPushbackState::Pushing && State != EPushbackState::Stopping)
	{
		return;
	}

	Movement->StopImmediately();
	SetState(EPushbackState::TugDisconnecting);
	GetWorld()->GetTimerManager().SetTimer(Timer, this, &UPushbackControllerComponent::Finish, FMath::Max(TugDisconnectTime, 0.01f));
}

void UPushbackControllerComponent::Finish()
{
	if (Tug)
	{
		Tug->Destroy();
		Tug = nullptr;
	}
	SetState(EPushbackState::Completed);

	if (ActiveRequest.bAutoTaxi && ActiveRequest.TaxiRouteAfter)
	{
		if (UTaxiControllerComponent* Taxi = GetOwner()->FindComponentByClass<UTaxiControllerComponent>())
		{
			Taxi->StartTaxi(ActiveRequest.TaxiRouteAfter);
		}
	}
}
