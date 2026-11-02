#include "Aircraft.h"
#include "FlightPath.h"
#include "Camera/CameraComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAircraft, Log, All);

namespace
{
	/** 0 before RampStart, then linear to 1 at the end of the spline. */
	float RampAlpha(float Progress, float RampStart)
	{
		return FMath::Clamp((Progress - RampStart) / FMath::Max(1.f - RampStart, 0.001f), 0.f, 1.f);
	}
}

AAircraft::AAircraft()
{
	PrimaryActorTick.bCanEverTick = true;

	// Always loaded in World Partition (Open World) maps, otherwise it is streamed out once it flies away (an editor-only setting)
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif

	// The aircraft itself is a static mesh: pick your model in Details > Static Mesh
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Mesh;

	// Placeholder body (30 m long) so the aircraft is visible without assets. Set Scale to 1 when you use your own mesh.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (PlaceholderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(PlaceholderMesh.Object);
		Mesh->SetRelativeScale3D(FVector(30.0, 3.0, 3.0));
	}

	// Chase camera behind and above the aircraft (ignores the mesh scale)
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->SetUsingAbsoluteScale(true);
	SpringArm->SetRelativeRotation(FRotator(-15.0, 0.0, 0.0));
	SpringArm->TargetArmLength = 6000.f;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.f;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 5.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);

	auto Phase = [this](EAircraftState State, float StartSpeed, float EndSpeed) -> FAircraftPhaseSettings&
	{
		FAircraftPhaseSettings& Settings = PhaseSettings.FindOrAdd(State);
		Settings.StartSpeed = StartSpeed;
		Settings.EndSpeed = EndSpeed;
		return Settings;
	};

	// Average widebody (A350, 777, 787, A330, 747) speeds in cm/s
	const float PushbackSpeed = 140.f;    // ~5 km/h, walking pace behind the tug
	const float TaxiSpeed = 1030.f;       // 20 kt
	const float V2 = 8490.f;              // 165 kt, takeoff safety speed
	const float ClimbSpeed = 12860.f;     // 250 kt, speed limit below 10.000 ft
	const float CruiseSpeed = 24800.f;    // Mach 0.84, ~890 km/h
	const float ApproachSpeed = 7460.f;   // 145 kt, final approach

	Phase(EAircraftState::Pushback, PushbackSpeed, PushbackSpeed).bReverse = true;

	Phase(EAircraftState::Taxi, TaxiSpeed, TaxiSpeed);

	// Takeoff roll at ~2 m/s2, reaches V2 near the end of the runway
	FAircraftPhaseSettings& Takeoff = Phase(EAircraftState::Takeoff, V2, V2);
	Takeoff.Acceleration = 200.f;
	Takeoff.EndPitch = 8.f;
	Takeoff.PitchRampStart = 0.65f;

	Phase(EAircraftState::Departure, V2, ClimbSpeed).StartPitch = 8.f;

	Phase(EAircraftState::Cruise, CruiseSpeed, CruiseSpeed);

	Phase(EAircraftState::Arrival, CruiseSpeed, ApproachSpeed);

	// Rollout after touchdown brakes down to taxi speed
	Phase(EAircraftState::Landing, ApproachSpeed, TaxiSpeed).SpeedRampStart = 0.82f;

	Phase(EAircraftState::TaxiToGate, TaxiSpeed, TaxiSpeed).bStopAtEnd = true;
}

void AAircraft::BeginPlay()
{
	Super::BeginPlay();

	if (!FlightPath)
	{
		FlightPath = Cast<AFlightPath>(UGameplayStatics::GetActorOfClass(this, AFlightPath::StaticClass()));
	}

	if (!FlightPath)
	{
		UE_LOG(LogAircraft, Error, TEXT("%s: no FlightPath in the level."), *GetName());
		SetActorTickEnabled(false);
		return;
	}

	ValidateRoute();

	// The mesh is the root and gets MeshRotation: turn the camera back so it stays behind the nose
	SpringArm->SetRelativeRotation(MeshRotation.Quaternion().Inverse() * FRotator(-15.0, 0.0, 0.0).Quaternion());

	EnterState(EAircraftState::Pushback, 0.f);
	if (CurrentSpline)
	{
		UpdateTransform(PhaseSettings.FindRef(CurrentState), 0.f, true);
	}

	// Next tick, so the player controller has possessed its pawn first and does not take the view back
	if (bFollowCamera)
	{
		GetWorldTimerManager().SetTimerForNextTick([WeakThis = TWeakObjectPtr<AAircraft>(this)]()
		{
			if (AAircraft* Aircraft = WeakThis.Get())
			{
				if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(Aircraft, 0))
				{
					PlayerController->SetViewTarget(Aircraft);
				}
			}
		});
	}
}

void AAircraft::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!CurrentSpline)
	{
		return;
	}

	const FAircraftPhaseSettings Phase = PhaseSettings.FindRef(CurrentState);
	const float Length = CurrentSpline->GetSplineLength();
	const float Progress = Length > 0.f ? CurrentDistance / Length : 1.f;

	// Speed
	float TargetSpeed = FMath::Lerp(Phase.StartSpeed, Phase.EndSpeed, RampAlpha(Progress, Phase.SpeedRampStart));
	if (Phase.bStopAtEnd)
	{
		const float Remaining = FMath::Max(Length - CurrentDistance, 0.f);
		TargetSpeed = FMath::Min(TargetSpeed, FMath::Sqrt(2.f * StopDeceleration * Remaining));
	}

	const float Rate = TargetSpeed > CurrentSpeed ? Phase.Acceleration : Deceleration;
	CurrentSpeed = FMath::FInterpConstantTo(CurrentSpeed, TargetSpeed, DeltaTime, Rate);
	CurrentSpeed = FMath::Max(CurrentSpeed, MinMoveSpeed);

	// Move along the spline
	CurrentDistance += CurrentSpeed * DeltaTime;

	float Overflow = 0.f;
	const bool bFinished = CurrentDistance >= Length;
	if (bFinished)
	{
		Overflow = CurrentDistance - Length;
		CurrentDistance = Length;
	}

	UpdateTransform(Phase, DeltaTime, false);

	// State machine: enum order is the flight order
	if (bFinished)
	{
		const EAircraftState NextState = static_cast<EAircraftState>(static_cast<uint8>(CurrentState) + 1);
		EnterState(NextState, Overflow);
	}
}

void AAircraft::EnterState(EAircraftState NewState, float StartDistance)
{
	CurrentState = NewState;
	CurrentSpline = FlightPath->GetSpline(NewState);
	CurrentDistance = 0.f;

	if (CurrentSpline)
	{
		CurrentDistance = FMath::Min(StartDistance, CurrentSpline->GetSplineLength());
	}
	else
	{
		if (NewState != EAircraftState::Parked)
		{
			UE_LOG(LogAircraft, Error, TEXT("%s: no spline for %s."), *GetName(), *UEnum::GetValueAsString(NewState));
		}

		CurrentSpeed = 0.f;
		SetActorTickEnabled(false);
	}

	const FString StateName = StaticEnum<EAircraftState>()->GetNameStringByValue(static_cast<int64>(NewState));
	UE_LOG(LogAircraft, Display, TEXT("%s: %s"), *GetName(), *StateName);
	if (bShowPhaseOnScreen && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, FString::Printf(TEXT("Aircraft: %s"), *StateName));
	}

	OnStateChanged.Broadcast(NewState);
}

void AAircraft::UpdateTransform(const FAircraftPhaseSettings& Phase, float DeltaTime, bool bSnapRotation)
{
	const float Length = CurrentSpline->GetSplineLength();
	const float SplineDistance = Phase.bReverse ? Length - CurrentDistance : CurrentDistance;

	const FVector Location = CurrentSpline->GetLocationAtDistanceAlongSpline(SplineDistance, ESplineCoordinateSpace::World);
	const FQuat SplineRotation = CurrentSpline->GetQuaternionAtDistanceAlongSpline(SplineDistance, ESplineCoordinateSpace::World);

	// Extra nose-up pitch on top of the spline direction (takeoff rotation, climb attitude)
	const float Progress = Length > 0.f ? CurrentDistance / Length : 1.f;
	const float PitchAlpha = FMath::SmoothStep(0.f, 1.f, RampAlpha(Progress, Phase.PitchRampStart));
	const float Pitch = FMath::Lerp(Phase.StartPitch, Phase.EndPitch, PitchAlpha);
	const FQuat TargetRotation = SplineRotation * FRotator(Pitch, 0.f, 0.f).Quaternion();

	if (bSnapRotation || RotationInterpSpeed <= 0.f)
	{
		CurrentRotation = TargetRotation;
	}
	else
	{
		CurrentRotation = FMath::QInterpTo(CurrentRotation, TargetRotation, DeltaTime, RotationInterpSpeed);
	}

	SetActorLocationAndRotation(Location, CurrentRotation * MeshRotation.Quaternion());
}

void AAircraft::ValidateRoute() const
{
	for (uint8 Index = 0; Index < static_cast<uint8>(EAircraftState::TaxiToGate); ++Index)
	{
		const EAircraftState From = static_cast<EAircraftState>(Index);
		const EAircraftState To = static_cast<EAircraftState>(Index + 1);

		const double Gap = FVector::Dist(GetPhasePoint(From, true), GetPhasePoint(To, false));
		if (Gap > RouteGapTolerance)
		{
			UE_LOG(LogAircraft, Warning, TEXT("%s: gap of %.0f cm between %s and %s. Connect the splines."),
				*GetName(), Gap, *UEnum::GetValueAsString(From), *UEnum::GetValueAsString(To));
		}
	}
}

FVector AAircraft::GetPhasePoint(EAircraftState State, bool bExit) const
{
	const USplineComponent* Spline = FlightPath->GetSpline(State);
	if (!Spline)
	{
		return FVector::ZeroVector;
	}

	// Exit point is the spline end, entry point the spline start. Reversed splines swap them.
	const bool bAtEnd = bExit != PhaseSettings.FindRef(State).bReverse;
	return Spline->GetLocationAtDistanceAlongSpline(bAtEnd ? Spline->GetSplineLength() : 0.f, ESplineCoordinateSpace::World);
}
