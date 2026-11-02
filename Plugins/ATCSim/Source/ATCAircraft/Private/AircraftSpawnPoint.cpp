#include "AircraftSpawnPoint.h"
#include "AircraftMovementComponent.h"
#include "ATCUnits.h"
#include "TaxiRoute.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

AAircraftSpawnPoint::AAircraftSpawnPoint()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(RootComponent);
	Arrow->ArrowSize = 20.f;
	Arrow->ArrowLength = 200.f;
	Arrow->ArrowColor = FColor(80, 200, 255);
	Arrow->SetRelativeLocation(FVector(0.f, 0.f, 300.f));

#if WITH_EDITORONLY_DATA
	Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(RootComponent);
		Sprite->SetRelativeLocation(FVector(0.f, 0.f, 500.f));
	}
#endif
}

void AAircraftSpawnPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Snap onto the route, pointing along it
	if (Route)
	{
		const float Distance = UAircraftMovementComponent::GetDistanceClosestTo(Route->Spline, GetActorLocation());
		StartDistanceM = Distance / ATCUnits::MetersToCm;
		SetActorLocationAndRotation(Route->Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World),
			Route->GetDirectionAt(Distance).Rotation());
	}
}

void AAircraftSpawnPoint::BeginPlay()
{
	Super::BeginPlay();
	if (UAircraftSpawnSubsystem* Spawner = GetWorld()->GetSubsystem<UAircraftSpawnSubsystem>())
	{
		Spawner->RegisterSpawnPoint(this);
	}
	if (Route)
	{
		StartDistanceM = UAircraftMovementComponent::GetDistanceClosestTo(Route->Spline, GetActorLocation()) / ATCUnits::MetersToCm;
	}
	if (bSpawnOnBeginPlay)
	{
		// Next frame: by then the route has resolved its stops in its own BeginPlay
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { StartStream(Count, IntervalSeconds); }));
	}
}

void AAircraftSpawnPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StreamTimer);
		if (UAircraftSpawnSubsystem* Spawner = World->GetSubsystem<UAircraftSpawnSubsystem>())
		{
			Spawner->UnregisterSpawnPoint(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

FAircraftSpawnRequest AAircraftSpawnPoint::MakeRequest() const
{
	FAircraftSpawnRequest Request;
	Request.Type = Type;
	Request.TrafficMix = TrafficMix;
	Request.AircraftClass = AircraftClass;
	Request.Route = Route;
	Request.StartDistanceM = StartDistanceM;
	Request.EndStop = EndStop;
	Request.EndDistanceM = EndDistanceM;
	Request.bStartTaxi = bStartTaxi;
	Request.bRemoveAtRouteEnd = bRemoveAtRouteEnd;
	return Request;
}

FName AAircraftSpawnPoint::SpawnOne(FName Callsign, UAircraftTypeData* TypeOverride)
{
	UAircraftSpawnSubsystem* Spawner = GetWorld()->GetSubsystem<UAircraftSpawnSubsystem>();
	if (!Spawner)
	{
		return NAME_None;
	}
	FAircraftSpawnRequest Request = MakeRequest();
	Request.Callsign = Callsign;
	if (TypeOverride)
	{
		Request.Type = TypeOverride;
	}
	return Spawner->RequestSpawn(Request);
}

void AAircraftSpawnPoint::StartStream(int32 InCount, float InIntervalSeconds)
{
	StreamLeft = InCount > 0 ? InCount : -1;
	StreamTick();
	if (StreamLeft != 0)
	{
		GetWorldTimerManager().SetTimer(StreamTimer, this, &AAircraftSpawnPoint::StreamTick, FMath::Max(InIntervalSeconds, 1.f), true);
	}
}

void AAircraftSpawnPoint::StopStream()
{
	GetWorldTimerManager().ClearTimer(StreamTimer);
	StreamLeft = 0;
}

void AAircraftSpawnPoint::StreamTick()
{
	if (StreamLeft == 0)
	{
		StopStream();
		return;
	}
	SpawnOne();
	if (StreamLeft > 0 && --StreamLeft == 0)
	{
		GetWorldTimerManager().ClearTimer(StreamTimer);
	}
}
