// Fill out your copyright notice in the Description page of Project Settings.

#include "AircraftSplineActor.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	constexpr float KnotsToCms = 51.4444f;
}

AAircraftSplineActor::AAircraftSplineActor()
{
	// Without this the actor never ticks and the aircraft stands still at the start of its first spline
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;

	// Create root scene component
	USceneComponent* RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = RootScene;

	// Create and attach aircraft mesh		
	AircraftMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AircraftMesh"));
	AircraftMesh->SetupAttachment(RootComponent);
	AircraftMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	// Create and attach spline components
	PushbackSpline = CreateDefaultSubobject<USplineComponent>(TEXT("PushbackSpline"));
	PushbackSpline->SetupAttachment(RootComponent);

	TaxiSpline = CreateDefaultSubobject<USplineComponent>(TEXT("TaxiSpline"));
	TaxiSpline->SetupAttachment(RootComponent);

	TakeoffSpline = CreateDefaultSubobject<USplineComponent>(TEXT("TakeoffSpline"));
	TakeoffSpline->SetupAttachment(RootComponent);

	LandingSpline = CreateDefaultSubobject<USplineComponent>(TEXT("LandingSpline"));
	LandingSpline->SetupAttachment(RootComponent);

	// Initialize state
	CurrentPhase = EFlightPhase::Pushback;
	CurrentSpline = nullptr;
	DistanceAlongSpline = 0.0f;
	CurrentSpeed = 0.0f;
	TargetSpeed = 0.0f;
	CurrentAccelerationRate = 0.0f;
}

void AAircraftSplineActor::BeginPlay()
{
	AActor::BeginPlay();

	// Start with Pushback phase
	InitializePhase(EFlightPhase::Pushback);
}

void AAircraftSplineActor::Tick(float DeltaTime)
{
	AActor::Tick(DeltaTime);

	if (!CurrentSpline || CurrentSpline->GetNumberOfSplinePoints() < 2)
	{
		return;
	}

	// Update speed (acceleration/deceleration)
	UpdateSpeed(DeltaTime);

	// Update aircraft position and rotation
	UpdateMovement(DeltaTime);

	// Check if we've reached the end of the spline
	if (HasReachedSplineEnd())
	{
		AdvanceToNextPhase();
	}
}

void AAircraftSplineActor::UpdateSpeed(float DeltaTime)
{
	// A steady acceleration or braking (knots per second) towards the target speed
	CurrentSpeed = FMath::FInterpConstantTo(CurrentSpeed, TargetSpeed, DeltaTime, CurrentAccelerationRate);

	// The last phase stops at the end of its spline instead of running past it: v^2 = 2 a d
	if (CurrentPhase == EFlightPhase::Landing && CurrentSpline)
	{
		const float RemainingCm = FMath::Max(CurrentSpline->GetSplineLength() - DistanceAlongSpline, 0.f);
		const float StopKt = FMath::Sqrt(2.f * FMath::Max(LandingDeceleration, 0.1f) * KnotsToCms * RemainingCm) / KnotsToCms;
		// At least a crawl, so it really gets to the end (and the phase finishes)
		CurrentSpeed = FMath::Min(CurrentSpeed, FMath::Max(StopKt, 0.5f));
	}
}

void AAircraftSplineActor::UpdateMovement(float DeltaTime)
{
	if (!CurrentSpline)
	{
		return;
	}

	// Calculate distance traveled this frame (speeds are knots, the spline is in cm)
	float DistanceTraveled = CurrentSpeed * KnotsToCms * DeltaTime;
	DistanceAlongSpline += DistanceTraveled;

	// Get spline length
	float SplineLength = CurrentSpline->GetSplineLength();

	// Get location at distance along spline (Clamp for safety)
	FVector NewLocation = CurrentSpline->GetLocationAtDistanceAlongSpline(
		FMath::Clamp(DistanceAlongSpline, 0.0f, SplineLength),
		ESplineCoordinateSpace::World
	);

	// Get rotation at distance along spline; pushed back, the nose points the other way (back to the gate)
	FRotator NewRotation = CurrentSpline->GetRotationAtDistanceAlongSpline(
		FMath::Clamp(DistanceAlongSpline, 0.0f, SplineLength),
		ESplineCoordinateSpace::World
	);
	if (CurrentPhase == EFlightPhase::Pushback && bPushbackTailFirst)
	{
		NewRotation = FRotator(-NewRotation.Pitch, NewRotation.Yaw + 180.f, -NewRotation.Roll);
	}

	// Apply new location and rotation to the aircraft
	AircraftMesh->SetWorldLocationAndRotation(NewLocation, NewRotation);
}

bool AAircraftSplineActor::HasReachedSplineEnd() const
{
	if (!CurrentSpline)
	{
		return false;
	}

	float SplineLength = CurrentSpline->GetSplineLength();

	// For normal forward movement, check if we've exceeded spline length
	if (CurrentSpeed >= 0.0f && DistanceAlongSpline >= SplineLength)
	{
		return true;
	}

	// For backward movement (pushback), check if we've gone below zero
	if (CurrentSpeed < 0.0f && DistanceAlongSpline <= 0.0f)
	{
		return true;
	}

	return false;
}

void AAircraftSplineActor::AdvanceToNextPhase()
{
	switch (CurrentPhase)
	{
	case EFlightPhase::Pushback:
		InitializePhase(EFlightPhase::Taxi);
		break;

	case EFlightPhase::Taxi:
		InitializePhase(EFlightPhase::Takeoff);
		break;

	case EFlightPhase::Takeoff:
		InitializePhase(EFlightPhase::Landing);
		break;

	case EFlightPhase::Landing:
		CurrentSpeed = 0.0f;
		TargetSpeed = 0.0f;
		CurrentSpline = nullptr;   // done: stands at the end of the landing spline, and stops ticking
		SetActorTickEnabled(false);
		UE_LOG(LogTemp, Warning, TEXT("Aircraft has completed landing sequence"));
		break;

	default:
		break;
	}
}

void AAircraftSplineActor::InitializePhase(EFlightPhase NewPhase)
{
	CurrentPhase = NewPhase;
	DistanceAlongSpline = 0.0f;

	switch (NewPhase)
	{
	case EFlightPhase::Pushback:
		CurrentSpline = PushbackSpline;
		CurrentSpeed = 0.0f;
		TargetSpeed = PushbackSpeed;
		CurrentAccelerationRate = 1.0f;
		UE_LOG(LogTemp, Warning, TEXT("Aircraft entering PUSHBACK phase"));
		break;

	case EFlightPhase::Taxi:
		CurrentSpline = TaxiSpline;
		CurrentSpeed = 0.0f;
		TargetSpeed = TaxiSpeed;
		CurrentAccelerationRate = TaxiAcceleration;
		UE_LOG(LogTemp, Warning, TEXT("Aircraft entering TAXI phase"));
		break;

	case EFlightPhase::Takeoff:
		CurrentSpline = TakeoffSpline;
		CurrentSpeed = TaxiSpeed;
		TargetSpeed = TakeoffMaxSpeed;
		CurrentAccelerationRate = TakeoffAcceleration;
		UE_LOG(LogTemp, Warning, TEXT("Aircraft entering TAKEOFF phase"));
		break;

	case EFlightPhase::Landing:
		CurrentSpline = LandingSpline;
		CurrentSpeed = LandingStartSpeed;
		TargetSpeed = TaxiSpeed;
		CurrentAccelerationRate = LandingDeceleration;
		UE_LOG(LogTemp, Warning, TEXT("Aircraft entering LANDING phase"));
		break;

	default:
		break;
	}
}