#include "Stand.h"
#include "TaxiRoute.h"
#include "TrafficSubsystem.h"
#include "Components/ArrowComponent.h"
#include "Engine/World.h"

AStand::AStand()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	ParkingPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ParkingPoint"));
	ParkingPoint->SetupAttachment(RootComponent);

	NoseDirection = CreateDefaultSubobject<UArrowComponent>(TEXT("NoseDirection"));
	NoseDirection->SetupAttachment(ParkingPoint);
	NoseDirection->ArrowSize = 10.f;
	NoseDirection->ArrowColor = FColor::Yellow;
}

void AStand::BeginPlay()
{
	Super::BeginPlay();
	if (UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>())
	{
		Traffic->RegisterStand(this);
	}
	// Taxiway lights are for taxiing; a pushback route has none
	for (const FPushbackOption& Option : PushbackOptions)
	{
		if (Option.Route)
		{
			Option.Route->SetShowLights(false);
		}
	}
}

void AStand::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UTrafficSubsystem>() : nullptr)
	{
		Traffic->UnregisterStand(this);
	}
	Super::EndPlay(EndPlayReason);
}

ATaxiRoute* AStand::FindPushbackRoute(FGameplayTag OptionTag) const
{
	for (const FPushbackOption& Option : PushbackOptions)
	{
		if (Option.OptionTag == OptionTag || !OptionTag.IsValid())
		{
			return Option.Route;
		}
	}
	return nullptr;
}

void AStand::Vacate()
{
	if (Occupant.IsValid())
	{
		Occupant = nullptr;
		OnStandVacated.Broadcast(this);
	}
}
