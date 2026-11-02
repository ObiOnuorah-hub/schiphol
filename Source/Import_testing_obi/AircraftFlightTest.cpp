#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Aircraft.h"
#include "FlightPath.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAircraftFullCycleTest, "Import_testing_obi.AircraftFlight.FullCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Flies the full cycle and checks every frame: on the spline, no jumps, nose along the spline, never below ground. */
bool FAircraftFullCycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AFlightPath* Path = World->SpawnActor<AFlightPath>();
	AAircraft* Plane = World->SpawnActor<AAircraft>();
	Plane->FlightPath = Path;
	if (!Path->HasActorBegunPlay())
	{
		Path->DispatchBeginPlay();
	}
	if (!Plane->HasActorBegunPlay())
	{
		Plane->DispatchBeginPlay();
	}

	const float DeltaTime = 1.f / 30.f;
	const float MaxTime = 3000.f;

	TArray<EAircraftState> Visited = { Plane->CurrentState };
	TMap<EAircraftState, float> PhaseTime;
	float Time = 0.f;
	float MaxOffSpline = 0.f;
	float MaxStepError = -BIG_NUMBER;
	float MaxYawError = 0.f;
	float MinZ = BIG_NUMBER;
	float MaxPitch = -BIG_NUMBER;
	FVector PrevLocation = Plane->GetActorLocation();
	float PrevSpeed = Plane->CurrentSpeed;

	while (Plane->CurrentState != EAircraftState::Parked && Time < MaxTime)
	{
		const EAircraftState State = Plane->CurrentState;
		const USplineComponent* Spline = Path->GetSpline(State);

		Plane->Tick(DeltaTime);
		Time += DeltaTime;
		PhaseTime.FindOrAdd(State) += DeltaTime;

		const FVector Location = Plane->GetActorLocation();
		const FRotator Rotation = Plane->GetActorRotation();

		const FVector Closest = Spline->FindLocationClosestToWorldLocation(Location, ESplineCoordinateSpace::World);
		MaxOffSpline = FMath::Max(MaxOffSpline, static_cast<float>(FVector::Dist(Location, Closest)));

		// A step can never be longer than this frame's distance plus the overflow carried from the last frame
		const float Step = FVector::Dist(Location, PrevLocation);
		MaxStepError = FMath::Max(MaxStepError, Step - (PrevSpeed + Plane->CurrentSpeed) * DeltaTime);

		const FVector Direction = Spline->FindDirectionClosestToWorldLocation(Location, ESplineCoordinateSpace::World);
		MaxYawError = FMath::Max(MaxYawError, FMath::Abs(FMath::FindDeltaAngleDegrees(Rotation.Yaw, Direction.Rotation().Yaw)));

		MinZ = FMath::Min(MinZ, static_cast<float>(Location.Z));
		MaxPitch = FMath::Max(MaxPitch, static_cast<float>(Rotation.Pitch));

		if (Plane->CurrentState != State)
		{
			Visited.Add(Plane->CurrentState);
		}

		PrevLocation = Location;
		PrevSpeed = Plane->CurrentSpeed;
	}

	const TArray<EAircraftState> Expected = {
		EAircraftState::Pushback, EAircraftState::Taxi, EAircraftState::Takeoff, EAircraftState::Departure,
		EAircraftState::Cruise, EAircraftState::Arrival, EAircraftState::Landing, EAircraftState::TaxiToGate,
		EAircraftState::Parked
	};

	for (const EAircraftState State : Expected)
	{
		AddInfo(FString::Printf(TEXT("%s: %.1f s"), *UEnum::GetValueAsString(State), PhaseTime.FindRef(State)));
	}
	AddInfo(FString::Printf(TEXT("Total %.1f s | off spline %.2f cm | step error %.2f cm | yaw error %.2f deg | min Z %.2f cm | max pitch %.1f deg"),
		Time, MaxOffSpline, MaxStepError, MaxYawError, MinZ, MaxPitch));

	TestTrue(TEXT("All phases in order"), Visited == Expected);
	TestTrue(TEXT("Stopped"), Plane->CurrentSpeed == 0.f);
	TestTrue(TEXT("Parked at the gate"), FVector::Dist(Plane->GetActorLocation(), Path->GetActorLocation()) < 1.0);
	TestTrue(TEXT("Stays on the spline"), MaxOffSpline < 10.f);
	TestTrue(TEXT("No teleporting"), MaxStepError < 1.f);
	TestTrue(TEXT("Nose follows the spline"), MaxYawError < 5.f);
	TestTrue(TEXT("Never below the ground"), MinZ > -1.f);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
