// Automation tests for the ATC building blocks. Run: Session Frontend > Automation > ATCSim,
// or headless: -ExecCmds="Automation RunTests ATCSim" -TestExit="Automation Test Queue Empty"

#include "AircraftIdentityComponent.h"
#include "AircraftSpawnSubsystem.h"
#include "AirportData.h"
#include "ATCSimSettings.h"
#include "TrafficSubsystem.h"
#include "ATCMapSurvey.h"
#include "ATCSchematic.h"
#include "AircraftMovementComponent.h"
#include "AircraftTypeData.h"
#include "ATCAircraft.h"
#include "ATCUnits.h"
#include "EnvironmentTypes.h"
#include "GMICamera.h"
#include "GMICameraSubsystem.h"
#include "LensSetData.h"
#include "PTZControllerComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "LightingRigComponent.h"
#include "PushbackControllerComponent.h"
#include "Stand.h"
#include "TaxiControllerComponent.h"
#include "TaxiNetworkSubsystem.h"
#include "TaxiRoute.h"
#include "TimeOfDaySubsystem.h"
#include "TrafficDirector.h"
#include "TrafficMixData.h"
#include "Algo/Reverse.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCLensTest, "ATCSim.Camera.FocalLengthToFOV", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FATCLensTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("24 mm"), ULensLibrary::FocalLengthToFOV(24.f), 73.74f, 0.05f);
	TestEqual(TEXT("50 mm"), ULensLibrary::FocalLengthToFOV(50.f), 39.60f, 0.05f);
	TestEqual(TEXT("100 mm"), ULensLibrary::FocalLengthToFOV(100.f), 20.41f, 0.05f);
	TestEqual(TEXT("400 mm"), ULensLibrary::FocalLengthToFOV(400.f), 5.15f, 0.05f);
	TestEqual(TEXT("round trip"), ULensLibrary::FOVToFocalLength(ULensLibrary::FocalLengthToFOV(135.f)), 135.f, 0.01f);
	// Logarithmic zoom: half way from 25 to 100 mm is 50 mm, so 25 -> 50 takes as long as 50 -> 100
	TestEqual(TEXT("log zoom half way"), ULensLibrary::InterpolateFocalLength(25.f, 100.f, 0.5f), 50.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCPTZTest, "ATCSim.Camera.PTZ", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A GMI camera on a mast: a preset arrives with pan, tilt and zoom together (the zoom logarithmic), every input is
 * clamped, a turn takes the short way only when that stays within the limits, the joystick is slower when zoomed in,
 * a dome camera turns all the way round, and a lens change zooms to that lens.
 */
bool FATCPTZTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ATCSimCameraWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->HasBegunPlay())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}
	auto Run = [World](float Seconds, TFunction<void()> EachFrame = nullptr)
	{
		const float Step = 1.f / 30.f;
		for (int32 Frame = 0; Frame < FMath::RoundToInt(Seconds / Step); ++Frame)
		{
			if (EachFrame)
			{
				EachFrame();
			}
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Step);
		}
	};

	ULensSetData* Lenses = NewObject<ULensSetData>();
	FLensPreset Wide;
	Wide.Name = FText::FromString(TEXT("Wide"));
	Wide.FocalLengthMm = 24.f;
	FLensPreset Tele;
	Tele.Name = FText::FromString(TEXT("Tele"));
	Tele.FocalLengthMm = 100.f;
	Lenses->Lenses = { Wide, Tele };
	FCameraPreset Gate;
	Gate.PanDeg = 10.f;
	Gate.TiltDeg = -5.f;
	Gate.FocalLengthMm = 24.f;
	FCameraPreset Runway;
	Runway.PanDeg = 120.f;
	Runway.TiltDeg = -2.f;
	Runway.FocalLengthMm = 400.f;

	AGMICamera* Camera = World->SpawnActorDeferred<AGMICamera>(AGMICamera::StaticClass(), FTransform::Identity);
	Camera->PTZ->LensSet = Lenses;
	Camera->PTZ->Presets = { Gate, Runway };
	Camera->FinishSpawning(FTransform::Identity);
	UPTZControllerComponent* PTZ = Camera->PTZ;
	TestEqual(TEXT("starts at the first preset"), PTZ->PanDeg, 10.f, 0.01f);
	TestEqual(TEXT("the capture gets the field of view of 24 mm"), Camera->Capture->FOVAngle, 73.74f, 0.05f);

	// Half way in time the zoom is at the geometric middle and the pan half way; then everything arrives together
	PTZ->TransitionTime = 2.f;
	PTZ->GoToPresetIndex(1);
	Run(1.f);
	TestEqual(TEXT("zoom half way: geometric middle"), PTZ->FocalLengthMm, FMath::Sqrt(24.f * 400.f), 1.f);
	TestEqual(TEXT("pan half way"), PTZ->PanDeg, 65.f, 0.5f);
	Run(1.f);
	TestEqual(TEXT("pan at the preset"), PTZ->PanDeg, 120.f, 0.01f);
	TestEqual(TEXT("tilt at the preset"), PTZ->TiltDeg, -2.f, 0.01f);
	TestEqual(TEXT("zoom at the preset"), PTZ->FocalLengthMm, 400.f, 0.01f);
	TestEqual(TEXT("the capture gets the field of view of 400 mm"), Camera->Capture->FOVAngle, 5.15f, 0.01f);

	// Full joystick for one second at 400 mm: 40 deg/s scaled with the field of view
	const float Before = PTZ->PanDeg;
	Run(1.f, [PTZ]() { PTZ->AddPanInput(1.f); });
	TestEqual(TEXT("slower when zoomed in"), PTZ->PanDeg - Before, 40.f * ULensLibrary::FocalLengthToFOV(400.f) / ULensLibrary::FocalLengthToFOV(24.f), 0.05f);

	PTZ->SetPanTilt(200.f, 30.f, false);
	TestEqual(TEXT("pan clamped"), PTZ->PanDeg, 170.f, 0.01f);
	TestEqual(TEXT("tilt clamped"), PTZ->TiltDeg, 15.f, 0.01f);
	PTZ->SetFocalLength(1000.f, false);
	TestEqual(TEXT("zoom clamped"), PTZ->FocalLengthMm, 400.f, 0.01f);

	// From 160 to -160 the short way (+40) passes the cable stop at 170: the camera goes round through 0
	PTZ->SetPanTilt(160.f, 0.f, false);
	PTZ->SetPanTilt(-160.f, 0.f);
	Run(0.5f);
	TestTrue(TEXT("the long way round"), PTZ->PanDeg < 159.f && PTZ->PanDeg > -159.f);
	Run(2.f);
	TestEqual(TEXT("arrives"), PTZ->PanDeg, -160.f, 0.01f);

	// A dome camera has no stop: the short way through 180
	PTZ->PanLimitsDeg = FVector2D(-180.f, 180.f);
	PTZ->SetPanTilt(160.f, 0.f, false);
	PTZ->SetPanTilt(-160.f, 0.f);
	Run(0.5f);
	TestTrue(TEXT("dome takes the short way"), PTZ->PanDeg > 160.f);
	Run(2.f);
	TestEqual(TEXT("dome arrives"), PTZ->PanDeg, -160.f, 0.01f);

	PTZ->ZoomDuration = 0.5f;
	PTZ->SelectLens(1);
	Run(0.6f);
	TestEqual(TEXT("tele lens"), PTZ->FocalLengthMm, 100.f, 0.01f);
	TestEqual(TEXT("lens name"), PTZ->GetLensName().ToString(), FString(TEXT("Tele")));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCCaptureBudgetTest, "ATCSim.Camera.CaptureBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Five cameras, four of them on a screen that is seen: the two that matter most render at their own 20 Hz, the other
 * two at 5 Hz, the one nobody sees not at all, and never more than two pictures in one frame.
 */
bool FATCCaptureBudgetTest::RunTest(const FString& Parameters)
{
	TArray<FGMICaptureState> States;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		FGMICaptureState& State = States.AddDefaulted_GetRef();
		State.bViewed = Index < 4;
		State.Priority = 4.f - Index;
		State.LiveRateHz = 20.f;
	}
	TArray<int32> Count;
	Count.Init(0, States.Num());
	int32 MostInOneFrame = 0;
	TArray<int32> Capture;
	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		UGMICameraSubsystem::PlanCaptures(States, 1.f / 60.f, 2, 5.f, 2, Capture);
		MostInOneFrame = FMath::Max(MostInOneFrame, Capture.Num());
		for (const int32 Index : Capture)
		{
			++Count[Index];
		}
	}
	TestTrue(TEXT("the two that matter most are live"), States[0].bLive && States[1].bLive && !States[2].bLive && !States[3].bLive);
	TestEqual(TEXT("live camera at 20 Hz"), float(Count[0]), 200.f, 2.f);
	TestEqual(TEXT("second live camera at 20 Hz"), float(Count[1]), 200.f, 2.f);
	TestEqual(TEXT("seen camera at 5 Hz"), float(Count[2]), 50.f, 2.f);
	TestEqual(TEXT("another seen camera at 5 Hz"), float(Count[3]), 50.f, 2.f);
	TestEqual(TEXT("unseen camera renders nothing"), Count[4], 0);
	TestTrue(TEXT("at most two pictures per frame"), MostInOneFrame <= 2);

	// Six live cameras that each want every frame: still two pictures per frame
	TArray<FGMICaptureState> Busy;
	Busy.Init(FGMICaptureState(), 6);
	for (FGMICaptureState& State : Busy)
	{
		State.bViewed = true;
		State.LiveRateHz = 60.f;
	}
	int32 Total = 0;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		UGMICameraSubsystem::PlanCaptures(Busy, 1.f / 60.f, 6, 5.f, 2, Capture);
		Total += Capture.Num();
	}
	TestEqual(TEXT("two pictures every frame"), Total, 120);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCCurveSpeedTest, "ATCSim.Aircraft.CurveSpeed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FATCCurveSpeedTest::RunTest(const FString& Parameters)
{
	// 0.5 m/s2 in a 50 m radius turn: v = sqrt(0.5 * 50) = 5 m/s = 9.7 kt
	const float Speed = UTaxiControllerComponent::CurveSpeed(1.f / 5000.f, 50.f);
	TestEqual(TEXT("50 m radius"), Speed / ATCUnits::KnotsToCms, 9.72f, 0.05f);
	TestTrue(TEXT("straight = no limit"), UTaxiControllerComponent::CurveSpeed(0.f, 50.f) > 100000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCSunTest, "ATCSim.Environment.SunPosition", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FATCSunTest::RunTest(const FString& Parameters)
{
	float Elevation = 0.f, Azimuth = 0.f;

	// Schiphol, 21 June around solar noon (11:43 UTC): ~61 degrees, due south
	UTimeOfDaySubsystem::ComputeSun(52.3086, 4.7639, FDateTime(2026, 6, 21, 11, 43, 0), Elevation, Azimuth);
	TestEqual(TEXT("summer elevation"), Elevation, 61.1f, 1.f);
	TestEqual(TEXT("summer azimuth"), Azimuth, 180.f, 3.f);

	// 21 December: ~14 degrees
	UTimeOfDaySubsystem::ComputeSun(52.3086, 4.7639, FDateTime(2026, 12, 21, 11, 45, 0), Elevation, Azimuth);
	TestEqual(TEXT("winter elevation"), Elevation, 14.3f, 1.f);

	// Midnight: below the horizon, night
	UTimeOfDaySubsystem::ComputeSun(52.3086, 4.7639, FDateTime(2026, 12, 21, 23, 0, 0), Elevation, Azimuth);
	TestTrue(TEXT("night phase"), UTimeOfDaySubsystem::PhaseForElevation(Elevation) == EDayPhase::Night);

	// The light travels away from the sun: sun in the south at 30 degrees -> light points north and down
	const FVector Light = UTimeOfDaySubsystem::SunLightRotation(30.f, 180.f, 0.f).Vector();
	TestTrue(TEXT("light points north"), Light.X > 0.8f);
	TestTrue(TEXT("light points down"), Light.Z < -0.4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCWeatherTest, "ATCSim.Environment.Weather", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FATCWeatherTest::RunTest(const FString& Parameters)
{
	FWeatherState Clear;
	Clear.VisibilityM = 10000.f;
	FWeatherState Mist;
	Mist.VisibilityM = 100.f;
	Mist.CloudCoverage = 1.f;
	Mist.Precipitation = EPrecipitationType::None;
	FWeatherState Rain;
	Rain.Precipitation = EPrecipitationType::Rain;
	Rain.PrecipitationRate = 1.f;
	FWeatherState Snow;
	Snow.Precipitation = EPrecipitationType::Snow;
	Snow.PrecipitationRate = 1.f;

	const FWeatherState Half = FWeatherState::Lerp(Clear, Mist, 0.5f);
	TestEqual(TEXT("visibility blends in log space"), Half.VisibilityM, 1000.f, 1.f);
	TestEqual(TEXT("coverage blends linearly"), Half.CloudCoverage, 0.55f, 0.001f);

	const FWeatherState Early = FWeatherState::Lerp(Rain, Snow, 0.25f);
	const FWeatherState Late = FWeatherState::Lerp(Rain, Snow, 0.75f);
	TestTrue(TEXT("rain fades out first"), Early.Precipitation == EPrecipitationType::Rain);
	TestEqual(TEXT("rain at half"), Early.PrecipitationRate, 0.5f, 0.001f);
	TestTrue(TEXT("then snow fades in"), Late.Precipitation == EPrecipitationType::Snow);

	// Koschmieder through the engine fog formula: 300 m mist and the engine default 0.02 (~4 km)
	TestEqual(TEXT("fog density at 300 m"), ULightingRigComponent::VisibilityToFogDensity(300.f), 0.2714f, 0.001f);
	TestEqual(TEXT("fog density at 4 km"), ULightingRigComponent::VisibilityToFogDensity(4000.f), 0.0204f, 0.001f);
	return true;
}

namespace ATCTest
{
	ATaxiRoute* MakeRoute(UWorld* World, const TArray<FVector>& Points)
	{
		ATaxiRoute* Route = World->SpawnActor<ATaxiRoute>(Points[0], FRotator::ZeroRotator);
		Route->bShowCenterline = false;
		Route->Spline->ClearSplinePoints(false);
		for (const FVector& Point : Points)
		{
			Route->Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
		}
		Route->Spline->UpdateSpline();
		Route->Rebuild();
		return Route;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCPushbackTaxiTest, "ATCSim.Aircraft.PushbackAndTaxi", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A B738 pushes back, taxis on its own, waits for clearance at the holding point and stops on the end of the route. */
bool FATCPushbackTaxiTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ATCSimTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->HasBegunPlay())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}

	ATaxiRoute* Push = ATCTest::MakeRoute(World, { FVector(0, 0, 0), FVector(-2000, 0, 0), FVector(-4475, -1025, 0), FVector(-5500, -3500, 0), FVector(-5500, -4500, 0) });
	ATaxiRoute* Taxi = ATCTest::MakeRoute(World, { FVector(-5500, -4500, 0), FVector(-5500, 12000, 0), FVector(-4036, 15536, 0), FVector(-500, 17000, 0), FVector(20000, 17000, 0) });

	const FVector HoldingPointLocation(10000.f, 17000.f, 0.f);
	FTaxiStop HoldingPoint;
	HoldingPoint.Name = TEXT("HP");
	HoldingPoint.Distance = UAircraftMovementComponent::GetDistanceClosestTo(Taxi->Spline, HoldingPointLocation);
	HoldingPoint.bRequiresClearance = true;
	Taxi->Stops = { HoldingPoint };
	FSpeedZone Apron;
	Apron.EndDistance = UAircraftMovementComponent::GetDistanceClosestTo(Taxi->Spline, FVector(-5500, 12000, 0));
	Apron.MaxSpeedKt = 10.f;
	Taxi->SpeedZones = { Apron };
	Taxi->Rebuild();

	AStand* Stand = World->SpawnActor<AStand>(FVector::ZeroVector, FRotator::ZeroRotator);
	FPushbackOption Option;
	Option.Route = Push;
	Stand->PushbackOptions = { Option };

	UAircraftTypeData* Type = NewObject<UAircraftTypeData>();
	Type->WingspanM = 35.8f;
	Type->LengthM = 39.5f;
	Type->WheelbaseM = 15.6f;

	AATCAircraft* Aircraft = World->SpawnActor<AATCAircraft>(FVector::ZeroVector, FRotator::ZeroRotator);
	Aircraft->SetAircraftType(Type);
	Aircraft->Identity->Callsign = TEXT("TEST01");
	Aircraft->Identity->Stand = Stand;
	Aircraft->PushbackController->TugConnectTime = 2.f;
	Aircraft->PushbackController->TugDisconnectTime = 2.f;
	Stand->SetOccupant(Aircraft);

	FPushbackRequest Request;
	Request.TaxiRouteAfter = Taxi;
	TestTrue(TEXT("pushback accepted"), IAircraftCommands::Execute_RequestPushback(Aircraft, Request));
	// No approval needed: Requested passes straight on to TugConnecting
	TestTrue(TEXT("tug connecting right after the request"), Aircraft->PushbackController->GetState() == EPushbackState::TugConnecting);

	TSet<EPushbackState> PushStates;
	TSet<ETaxiState> TaxiStates;
	float MaxApronSpeedKt = 0.f;
	float HoldingError = -1.f;
	FVector PushEndLocation = FVector::ZeroVector;
	FVector PushEndForward = FVector::ZeroVector;
	float HoldingTime = 0.f;
	bool bCleared = false;
	const float Step = 1.f / 30.f;
	float Time = 0.f;

	for (; Time < 900.f; Time += Step)
	{
		// Timers tick once per engine frame: advance the frame counter like the engine loop would
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);

		const EPushbackState PushState = Aircraft->PushbackController->GetState();
		const ETaxiState TaxiState = Aircraft->TaxiController->GetState();
		if (!PushStates.Contains(EPushbackState::TugDisconnecting) && PushState == EPushbackState::TugDisconnecting)
		{
			PushEndLocation = Aircraft->GetActorLocation();
			PushEndForward = Aircraft->GetActorForwardVector();
		}
		PushStates.Add(PushState);
		TaxiStates.Add(TaxiState);

		if (TaxiState == ETaxiState::Taxiing || TaxiState == ETaxiState::Braking)
		{
			if (Aircraft->Movement->Distance < Apron.EndDistance)
			{
				MaxApronSpeedKt = FMath::Max(MaxApronSpeedKt, Aircraft->Movement->Speed / ATCUnits::KnotsToCms);
			}
		}

		if (TaxiState == ETaxiState::Holding && !bCleared)
		{
			if (HoldingError < 0.f)
			{
				HoldingError = FVector::Dist(Aircraft->GetActorLocation(), Taxi->Spline->GetLocationAtDistanceAlongSpline(HoldingPoint.Distance, ESplineCoordinateSpace::World));
			}
			// The controller gives clearance after 10 s
			HoldingTime += Step;
			if (HoldingTime > 10.f)
			{
				TestTrue(TEXT("clearance accepted"), IAircraftCommands::Execute_ContinueTaxi(Aircraft));
				bCleared = true;
			}
		}
		if (TaxiState == ETaxiState::Arrived)
		{
			break;
		}
	}

	for (EPushbackState Expected : { EPushbackState::TugConnecting, EPushbackState::Pushing, EPushbackState::Stopping,
		EPushbackState::TugDisconnecting, EPushbackState::Completed })
	{
		TestTrue(FString::Printf(TEXT("pushback went through %s"), *UEnum::GetValueAsString(Expected)), PushStates.Contains(Expected));
	}
	TestTrue(TEXT("pushback ends within 0.5 m of its end point"), FVector::Dist(PushEndLocation, FVector(-5500, -4500, 0)) < 50.f);
	TestTrue(TEXT("after pushback the nose faces east (+Y)"), PushEndForward.Y > 0.95f);
	TestTrue(TEXT("taxi started on its own after pushback"), TaxiStates.Contains(ETaxiState::Taxiing));
	TestTrue(TEXT("stopped at the holding point"), HoldingError >= 0.f && HoldingError < 50.f);
	TestTrue(TEXT("apron speed limit 10 kt"), MaxApronSpeedKt <= 10.5f);
	TestTrue(TEXT("arrived"), Aircraft->TaxiController->GetState() == ETaxiState::Arrived);
	TestTrue(TEXT("stopped within 0.5 m of the route end"), FVector::Dist(Aircraft->GetActorLocation(), FVector(20000, 17000, 0)) < 50.f);
	TestTrue(TEXT("stand vacated"), Stand->GetOccupant() == nullptr);
	AddInfo(FString::Printf(TEXT("Cycle took %.0f s, max apron speed %.1f kt, holding error %.1f cm"), Time, MaxApronSpeedKt, HoldingError));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCTaxiLightsTest, "ATCSim.Aircraft.TaxiLights", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Follow the greens on a straight 1 km route with a stop bar on the waypoint "HP" at 530 m. */
bool FATCTaxiLightsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ATCSimLightsWorld"));
	ATaxiRoute* Route = ATCTest::MakeRoute(World, { FVector(0, 0, 0), FVector(100000, 0, 0) });
	FTaxiStop Stop;
	Stop.Name = TEXT("HP");
	Stop.Distance = 50000.f;
	Stop.bRequiresClearance = true;
	Route->Stops = { Stop };
	FTaxiWaypoint Line;
	Line.Name = TEXT("HP");
	Line.Distance = 53000.f;
	Route->Waypoints = { Line };
	Route->Rebuild();

	const TArray<float>& Data = Route->Lights->PerInstanceSMCustomData;
	const int32 Count = Route->Lights->GetInstanceCount();
	if (!TestTrue(TEXT("lights built (ATC.CreateTaxiLights has run)"), Count > 0))
	{
		World->DestroyWorld(false);
		return false;
	}
	// Centerline lights are green (custom data 2 = G), the stop bar lights red and last
	auto LitGreens = [&Data, Count]()
	{
		int32 Lit = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Lit += Data[Index * 4] > 0.5f && Data[Index * 4 + 2] > 0.5f ? 1 : 0;
		}
		return Lit;
	};
	auto BarOn = [&Data, Count]() { return Data[(Count - 1) * 4] > 0.5f; };

	TestEqual(TEXT("67 centerline lights every 15 m plus a 9-light bar"), Count, 67 + 9);
	TestEqual(TEXT("idle: no greens"), LitGreens(), 0);
	TestTrue(TEXT("idle: bar on"), BarOn());

	// Two aircraft on the route: the first is "Route" (any object works as a key), the second "World"
	Route->ShowGuidance(Route, 10000.f, 0);
	TestEqual(TEXT("taxiing: greens 400 m ahead"), LitGreens(), 26);
	Route->ShowGuidance(Route, 49000.f, 0);
	TestEqual(TEXT("holding: greens up to the bar only"), LitGreens(), 2);
	TestTrue(TEXT("holding: bar on"), BarOn());
	Route->ShowGuidance(World, 1000.f, 0);
	TestEqual(TEXT("second aircraft behind: both stretches lit"), LitGreens(), 2 + 26);
	Route->ShowGuidance(Route, 49000.f, 1);
	TestFalse(TEXT("cleared: bar off"), BarOn());
	TestEqual(TEXT("cleared: greens past the bar, plus the second aircraft's"), LitGreens(), 26 + 26);
	Route->ShowGuidance(Route, 60000.f, 1);
	TestTrue(TEXT("bar back on once the aircraft is past it"), BarOn());
	Route->ClearGuidance(Route);
	TestEqual(TEXT("first aircraft gone: only the second one's greens"), LitGreens(), 26);
	Route->ClearGuidance(World);
	TestEqual(TEXT("empty route: no greens"), LitGreens(), 0);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCSpawnTrafficTest, "ATCSim.Aircraft.SpawnAndSeparation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two aircraft asked for the same place on a 2 km route: the second waits in the queue until the first has left,
 * then follows it, stops the separation distance behind it at the holding point, and takes its place after the
 * first is cleared and removed at the route end.
 */
bool FATCSpawnTrafficTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ATCSimSpawnWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->HasBegunPlay())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}
	auto Cleanup = [World]()
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	ATaxiRoute* Route = ATCTest::MakeRoute(World, { FVector(0, 0, 0), FVector(200000, 0, 0) });
	FTaxiStop HoldingPoint;
	HoldingPoint.Name = TEXT("HP");
	HoldingPoint.Distance = 150000.f;
	HoldingPoint.bRequiresClearance = true;
	Route->Stops = { HoldingPoint };
	Route->Rebuild();

	UAircraftTypeData* Type = NewObject<UAircraftTypeData>();
	Type->WingspanM = 35.8f;
	Type->LengthM = 39.5f;
	Type->WheelbaseM = 15.6f;
	Type->MaxTaxiSpeedKt = 20.f;

	UAircraftSpawnSubsystem* Spawner = World->GetSubsystem<UAircraftSpawnSubsystem>();
	UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>();
	if (!TestNotNull(TEXT("spawn manager"), Spawner) || !TestNotNull(TEXT("traffic subsystem"), Traffic))
	{
		Cleanup();
		return false;
	}

	FAircraftSpawnRequest Request;
	Request.Route = Route;
	Request.Type = Type;
	Request.AircraftClass = AATCAircraft::StaticClass();
	Request.StartDistanceM = 100.f;
	Request.RemoveDelaySeconds = 2.f;
	Request.Callsign = TEXT("LEAD1");
	TestEqual(TEXT("first spawns"), Spawner->RequestSpawn(Request), FName(TEXT("LEAD1")));
	Request.Callsign = TEXT("FOLL2");
	TestEqual(TEXT("second accepted"), Spawner->RequestSpawn(Request), FName(TEXT("FOLL2")));
	TestEqual(TEXT("second waits: the place is taken"), Spawner->GetQueuedCallsigns().Num(), 1);

	const float Separation = GetDefault<UATCSimSettings>()->TaxiSeparationM * 100.f;
	float MinGap = BIG_NUMBER;
	float QueueGap = -1.f;
	float HeldTime = 0.f;
	float FollowerHoldError = -1.f;
	bool bLeaderCleared = false;
	bool bLeaderRemoved = false;
	const float Step = 1.f / 30.f;
	for (float Time = 0.f; Time < 900.f; Time += Step)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);

		AATCAircraft* Leader = Cast<AATCAircraft>(Traffic->FindAircraft(TEXT("LEAD1")));
		AATCAircraft* Follower = Cast<AATCAircraft>(Traffic->FindAircraft(TEXT("FOLL2")));
		if (bLeaderCleared && !Leader)
		{
			bLeaderRemoved = true;
		}
		if (Leader && Follower)
		{
			FVector LeaderTail, LeaderNose, FollowerTail, FollowerNose;
			float HalfSpan = 0.f;
			Leader->GetFootprint(LeaderTail, LeaderNose, HalfSpan);
			Follower->GetFootprint(FollowerTail, FollowerNose, HalfSpan);
			const float Gap = LeaderTail.X - FollowerNose.X;
			MinGap = FMath::Min(MinGap, Gap);

			// Leader waiting for clearance and the follower standing still behind it: the queue
			if (!bLeaderCleared && Leader->TaxiController->GetState() == ETaxiState::Holding && Follower->Movement->Speed < 1.f
				&& Follower->TaxiController->GetTrafficAhead() == Leader)
			{
				HeldTime += Step;
				if (HeldTime > 5.f)
				{
					QueueGap = Gap;
					TestTrue(TEXT("leader cleared"), Leader->TaxiController->ContinueTaxi());
					bLeaderCleared = true;
				}
			}
		}
		if (Follower && bLeaderRemoved && Follower->TaxiController->GetState() == ETaxiState::Holding)
		{
			FollowerHoldError = FMath::Abs(Follower->Movement->Distance - HoldingPoint.Distance);
			break;
		}
	}

	TestEqual(TEXT("queue empty: the second aircraft spawned"), Spawner->GetQueuedCallsigns().Num(), 0);
	TestTrue(TEXT("never closer than the separation (5 m margin)"), MinGap > Separation - 500.f);
	TestTrue(TEXT("queued at the separation distance behind the leader"), QueueGap > Separation - 500.f && QueueGap < Separation + 1000.f);
	TestTrue(TEXT("leader removed after arriving at the route end"), bLeaderRemoved);
	TestTrue(TEXT("follower moved up and holds at the holding point"), FollowerHoldError >= 0.f && FollowerHoldError < 50.f);
	AddInfo(FString::Printf(TEXT("Minimum gap %.1f m, queue gap %.1f m, follower holding error %.1f cm"), MinGap / 100.f, QueueGap / 100.f, FollowerHoldError));

	Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCParkedModelTest, "ATCSim.Aircraft.ParkedModelStandIn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A gate with a parked model of the map: when the traffic starts, an aircraft of the model's type and airline stands on
 * the gate and the model is hidden. Removed before its pushback, the model is back; after its pushback it stays hidden.
 */
bool FATCParkedModelTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ATCSimParkedWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->HasBegunPlay())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}

	ATaxiRoute* Push = ATCTest::MakeRoute(World, { FVector(0, 0, 0), FVector(-6000, 0, 0) });
	ATaxiRoute* Taxi = ATCTest::MakeRoute(World, { FVector(-6000, 0, 0), FVector(-6000, 50000, 0) });
	Taxi->RouteTag = FGameplayTag::RequestGameplayTag(TEXT("Route.Showcase.S1.Runway"));
	AStand* Stand = World->SpawnActor<AStand>(FVector::ZeroVector, FRotator::ZeroRotator);
	Stand->StandTag = FGameplayTag::RequestGameplayTag(TEXT("Stand.Showcase.S1"));
	FPushbackOption Option;
	Option.Route = Push;
	Stand->PushbackOptions = { Option };
	AActor* Model = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);

	UAircraftTypeData* Type = NewObject<UAircraftTypeData>();
	Type->WingspanM = 35.8f;
	Type->LengthM = 39.5f;
	Type->WheelbaseM = 15.6f;
	UTrafficMixData* Mix = NewObject<UTrafficMixData>();
	Mix->AircraftClass = AATCAircraft::StaticClass();

	ATrafficDirector* Director = World->SpawnActor<ATrafficDirector>();
	Director->TrafficMix = Mix;
	Director->bDeparturesAtStart = false;
	Director->ArrivalsPerHour = 0.f;
	FTrafficGate& Gate = Director->Gates.AddDefaulted_GetRef();
	Gate.Stand = Stand->StandTag;
	Gate.DepartureRoute = Taxi->RouteTag;
	Gate.ParkedModels = { Model };
	Gate.ParkedType = Type;
	Gate.ParkedAirline = TEXT("KLM");
	Director->StartTraffic();

	const AATCAircraft* StandIn = Cast<AATCAircraft>(Stand->GetOccupant());
	const FName Callsign = StandIn ? StandIn->Identity->Callsign : NAME_None;
	TestTrue(TEXT("an aircraft stands on the gate"), StandIn != nullptr);
	TestTrue(TEXT("of the model's type"), StandIn && StandIn->AircraftType == Type);
	TestTrue(TEXT("with a callsign of the model's airline"), Callsign.ToString().StartsWith(TEXT("KLM")));
	TestTrue(TEXT("the model is hidden"), Model->IsHidden());

	// A scenario clears the airport before it pushed back: the model stands there again
	World->GetSubsystem<UAircraftSpawnSubsystem>()->Despawn(Callsign);
	TestFalse(TEXT("removed while parked: the model is back"), Model->IsHidden());

	// One that pushed back has left the stand: the model stays hidden after it is gone
	AATCAircraft* Leaving = World->SpawnActor<AATCAircraft>(FVector::ZeroVector, FRotator::ZeroRotator);
	Leaving->SetAircraftType(Type);
	Leaving->Identity->Callsign = TEXT("KLM2");
	Leaving->Identity->Stand = Stand;
	Stand->SetOccupant(Leaving);
	Leaving->StandInFor({ Model });
	TestTrue(TEXT("hidden again by the next stand-in"), Model->IsHidden());
	FPushbackRequest Request;
	Request.TaxiRouteAfter = Taxi;
	TestTrue(TEXT("pushback accepted"), IAircraftCommands::Execute_RequestPushback(Leaving, Request));
	Leaving->Destroy();
	TestTrue(TEXT("removed after its pushback: the model stays hidden"), Model->IsHidden());

	Director->StopTraffic();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCGeoTest, "ATCSim.Airports.GeoToMap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FATCGeoTest::RunTest(const FString& Parameters)
{
	// Schiphol Tower Centrum (map origin, north along -Y) to Tower West (OSM): 2690.8 m north, 3039.0 m west (WGS84)
	const double Lat0 = 52.3075170, Lon0 = 4.7624393;
	const FVector West = UAirportData::ProjectGeo(Lat0, Lon0, -90.f, FVector::ZeroVector, 52.3316991, 4.7178680);
	TestEqual(TEXT("west of the centre tower"), West.X, -303904.0, 100.0);
	TestEqual(TEXT("north of the centre tower"), West.Y, -269083.0, 100.0);

	double Lat = 0.0, Lon = 0.0;
	UAirportData::UnprojectGeo(Lat0, Lon0, -90.f, FVector::ZeroVector, West, Lat, Lon);
	TestEqual(TEXT("round trip latitude"), Lat, 52.3316991, 1e-7);
	TestEqual(TEXT("round trip longitude"), Lon, 4.7178680, 1e-7);

	// North along +X and an origin elsewhere: the same distance
	const FVector Turned = UAirportData::ProjectGeo(Lat0, Lon0, 0.f, FVector(1000.0, 2000.0, 0.0), 52.3316991, 4.7178680);
	TestEqual(TEXT("north is +X"), Turned.X - 1000.0, 269083.0, 100.0);
	TestEqual(TEXT("west is -Y"), Turned.Y - 2000.0, -303904.0, 100.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCNetworkTest, "ATCSim.Editor.RouteNetwork", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FATCNetworkTest::RunTest(const FString& Parameters)
{
	auto Line = [](TArray<FVector>& Points, const FVector& A, const FVector& B)
	{
		const int32 Steps = FMath::CeilToInt(FVector::Dist(A, B) / 100.0);
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			Points.Add(FMath::Lerp(A, B, double(Step) / Steps));
		}
	};

	// A taxiway: centerline with edge lines 11 m either side
	TArray<FVector> Taxiway;
	Line(Taxiway, FVector(0, 0, 0), FVector(10000, 0, 0));
	Line(Taxiway, FVector(0, 1100, 0), FVector(10000, 1100, 0));
	Line(Taxiway, FVector(0, -1100, 0), FVector(10000, -1100, 0));
	ATCMapSurvey::FNetwork Edges;
	Edges.Build(Taxiway, 75.f, 220.f);
	TestEqual(TEXT("centerline costs 1"), Edges.LineCost(Edges.Nearest(FVector(2500, 0, 0))), 1.f);
	TestTrue(TEXT("edge line costs more"), Edges.LineCost(Edges.Nearest(FVector(2500, 1100, 0))) > 1.f);

	// A centerline and a stand lead-in that stops 41 m short of it
	TArray<FVector> Points;
	Line(Points, FVector(0, 0, 0), FVector(10000, 0, 0));
	Line(Points, FVector(5000, -6000, 0), FVector(5000, -4100, 0));
	ATCMapSurvey::FNetwork Network;
	Network.Build(Points, 75.f, 220.f);

	// From the stand to the taxiway: along the lead-in, over the gap (bridged in the line's own direction) and on
	const TArray<FVector> Route = Network.Route(FVector(5000, -6000, 0), FVector(9000, 0, 0));
	double Length = 0.0;
	for (int32 Index = 1; Index < Route.Num(); ++Index)
	{
		Length += FVector::Dist2D(Route[Index - 1], Route[Index]);
	}
	TestTrue(TEXT("route found"), Route.Num() > 2);
	TestTrue(TEXT("route is lead-in + gap + taxiway (about 100 m)"), Length > 9500.0 && Length < 11000.0);
	TestTrue(TEXT("route ends on the centerline"), FMath::Abs(Route.Last().Y) < 100.0);

	// Corners get arcs that stay tangent to both legs
	const TArray<FVector> Round = ATCMapSurvey::RoundCorners({ FVector(0, 0, 0), FVector(10000, 0, 0), FVector(10000, 10000, 0) }, 3000.f);
	TestTrue(TEXT("corner rounded"), Round.Num() > 3);
	TestTrue(TEXT("arc starts 30 m before the corner"), Round.ContainsByPredicate([](const FVector& P) { return P.Equals(FVector(7000, 0, 0), 1.0); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCTriangulateTest, "ATCSim.Editor.Triangulate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FATCTriangulateTest::RunTest(const FString& Parameters)
{
	// An L-shaped footprint (concave), clockwise, closed like an OSM way
	const TArray<FVector2D> Outline = { FVector2D(0, 0), FVector2D(0, 2), FVector2D(1, 2), FVector2D(1, 1), FVector2D(2, 1), FVector2D(2, 0), FVector2D(0, 0) };
	TArray<int32> Indices;
	TestTrue(TEXT("triangulated"), ATCSchematic::Triangulate(Outline, Indices));
	TestEqual(TEXT("n - 2 triangles"), Indices.Num(), 12);
	double Area = 0.0;
	for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
	{
		const FVector2D A = Outline[Indices[Index]], B = Outline[Indices[Index + 1]], C = Outline[Indices[Index + 2]];
		Area += FMath::Abs((B - A) ^ (C - A)) * 0.5;
	}
	TestEqual(TEXT("area of the L"), Area, 3.0, 1e-6);
	return true;
}

namespace ATCTest
{
	UWorld* MakePlayWorld(const TCHAR* Name)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		if (!World->HasBegunPlay())
		{
			World->GetWorldSettings()->NotifyBeginPlay();
		}
		return World;
	}

	void DestroyPlayWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** A taxiway like the Schiphol routes: straight lines with 30 m arcs in the corners, so shared stretches really coincide. */
	ATaxiRoute* MakeTaxiway(UWorld* World, const TArray<FVector>& Corners)
	{
		const TArray<FVector> Points = ATCMapSurvey::RoundCorners(Corners, 3000.f);
		ATaxiRoute* Route = World->SpawnActor<ATaxiRoute>(Points[0], FRotator::ZeroRotator);
		Route->bShowCenterline = false;
		Route->Spline->ClearSplinePoints(false);
		for (const FVector& Point : Points)
		{
			Route->Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
		}
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			Route->Spline->SetSplinePointType(Index, ESplinePointType::CurveClamped, false);
		}
		Route->Spline->UpdateSpline();
		Route->Rebuild();
		return Route;
	}

	/** An A320-sized type that taxis at 20 kt. */
	UAircraftTypeData* MakeNarrowBody()
	{
		UAircraftTypeData* Type = NewObject<UAircraftTypeData>();
		Type->WingspanM = 35.8f;
		Type->LengthM = 37.6f;
		Type->WheelbaseM = 12.6f;
		Type->MaxTaxiSpeedKt = 20.f;
		return Type;
	}

	/** An aircraft on a route with its nose along it, taxiing from there to EndCm (-1 = the end of the route). */
	AATCAircraft* StartOnRoute(UWorld* World, UAircraftTypeData* Type, const TCHAR* Callsign, ATaxiRoute* Route, float StartCm, float EndCm = -1.f)
	{
		AATCAircraft* Aircraft = World->SpawnActor<AATCAircraft>(FVector::ZeroVector, FRotator::ZeroRotator);
		Aircraft->Identity->Callsign = Callsign;
		Aircraft->SetAircraftType(Type);
		Aircraft->PlaceOnRoute(Route, StartCm);
		Aircraft->TaxiController->StartTaxiBetween(Route, StartCm, EndCm);
		return Aircraft;
	}

	/** How far the main gear is from the line it follows (0 = exactly on it). */
	float OffLine(const AATCAircraft* Aircraft)
	{
		const USplineComponent* Path = Aircraft->Movement->Path;
		if (!Path || Aircraft->Movement->Distance < 0.f || Aircraft->Movement->Distance > Path->GetSplineLength())
		{
			return 0.f;
		}
		return FVector::Dist(Aircraft->GetActorLocation(), Path->GetLocationAtDistanceAlongSpline(Aircraft->Movement->Distance, ESplineCoordinateSpace::World));
	}

	/** What a run of the world saw: overlaps, distance off the line, who stood still how long, when it was over. */
	struct FWatch
	{
		int32 Overlaps = 0;
		float MaxOffLineCm = 0.f;
		TMap<FName, float> StoodSeconds;
		float Seconds = 0.f;
		bool bAllArrived = false;

		/** One step of 1/30 s; true when every aircraft has arrived. */
		bool Step(UWorld* World, const TArray<AATCAircraft*>& Aircraft)
		{
			const float Delta = 1.f / 30.f;
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Delta);
			Seconds += Delta;
			bAllArrived = true;
			for (int32 Index = 0; Index < Aircraft.Num(); ++Index)
			{
				const AATCAircraft* One = Aircraft[Index];
				const ETaxiState State = One->TaxiController->GetState();
				bAllArrived &= State == ETaxiState::Arrived;
				MaxOffLineCm = FMath::Max(MaxOffLineCm, OffLine(One));
				if ((State == ETaxiState::Taxiing || State == ETaxiState::Braking) && One->Movement->Speed < 50.f)
				{
					StoodSeconds.FindOrAdd(One->Identity->Callsign) += Delta;
				}
				for (int32 Other = Index + 1; Other < Aircraft.Num(); ++Other)
				{
					Overlaps += UTaxiNetworkSubsystem::Overlap(One, Aircraft[Other]) ? 1 : 0;
				}
			}
			return bAllArrived;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCHeadOnTest, "ATCSim.Aircraft.HeadOnOneTaxiway", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two aircraft head for the same 1 km taxiway from opposite ends at the same time; a third follows the first. Nobody
 * leaves the line, nobody passes through another: one direction waits before the shared taxiway until it is empty.
 */
bool FATCHeadOnTest::RunTest(const FString& Parameters)
{
	UWorld* World = ATCTest::MakePlayWorld(TEXT("ATCSimHeadOnWorld"));
	ATaxiRoute* East = ATCTest::MakeTaxiway(World, { FVector(-50000, 40000, 0), FVector(-20000, 10000, 0), FVector(0, 0, 0), FVector(100000, 0, 0),
		FVector(120000, 10000, 0), FVector(150000, 40000, 0) });
	ATaxiRoute* West = ATCTest::MakeTaxiway(World, { FVector(150000, -40000, 0), FVector(120000, -10000, 0), FVector(100000, 0, 0), FVector(0, 0, 0),
		FVector(-20000, -10000, 0), FVector(-50000, -40000, 0) });
	UAircraftTypeData* Type = ATCTest::MakeNarrowBody();
	AATCAircraft* First = ATCTest::StartOnRoute(World, Type, TEXT("EAST1"), East, 15000.f);
	// Ends 150 m before the first one, which parks at the very end of the route
	AATCAircraft* Second = ATCTest::StartOnRoute(World, Type, TEXT("EAST2"), East, 0.f, East->GetLength() - 15000.f);
	AATCAircraft* Oncoming = ATCTest::StartOnRoute(World, Type, TEXT("WEST1"), West, 15000.f);
	const TArray<AATCAircraft*> All = { First, Second, Oncoming };

	// On the shared taxiway: between its two ends, on its line
	auto OnShared = [](const AATCAircraft* Aircraft)
	{
		const FVector Location = Aircraft->GetActorLocation();
		return Location.X > 0.f && Location.X < 100000.f && FMath::Abs(Location.Y) < 1500.f;
	};
	ATCTest::FWatch Watch;
	bool bHeadOn = false;
	while (Watch.Seconds < 900.f && !Watch.Step(World, All))
	{
		bHeadOn |= (OnShared(First) || OnShared(Second)) && OnShared(Oncoming);
	}

	TestEqual(TEXT("no overlap, ever"), Watch.Overlaps, 0);
	TestFalse(TEXT("never both directions on the shared taxiway at once"), bHeadOn);
	TestTrue(TEXT("all three arrived"), Watch.bAllArrived);
	TestTrue(TEXT("main gear on the line (within 1 cm)"), Watch.MaxOffLineCm < 1.f);
	TestTrue(TEXT("somebody had to wait"), Watch.StoodSeconds.FindRef(TEXT("EAST1")) + Watch.StoodSeconds.FindRef(TEXT("WEST1")) > 10.f);
	AddInfo(FString::Printf(TEXT("Done after %.0f s; stood still: EAST1 %.0f s, EAST2 %.0f s, WEST1 %.0f s; off the line at most %.2f cm"), Watch.Seconds,
		Watch.StoodSeconds.FindRef(TEXT("EAST1")), Watch.StoodSeconds.FindRef(TEXT("EAST2")), Watch.StoodSeconds.FindRef(TEXT("WEST1")), Watch.MaxOffLineCm));
	ATCTest::DestroyPlayWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCCrossingTest, "ATCSim.Aircraft.CrossingQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two taxiways cross at right angles. Two aircraft get there at the same time, a third queues behind the first. One
 * at a time in the crossing, nobody waits inside it, no overlap; the order is logged.
 */
bool FATCCrossingTest::RunTest(const FString& Parameters)
{
	UWorld* World = ATCTest::MakePlayWorld(TEXT("ATCSimCrossingWorld"));
	ATaxiRoute* North = ATCTest::MakeTaxiway(World, { FVector(0, 60000, 0), FVector(0, -60000, 0) });
	ATaxiRoute* East = ATCTest::MakeTaxiway(World, { FVector(-60000, 0, 0), FVector(60000, 0, 0) });
	UAircraftTypeData* Type = ATCTest::MakeNarrowBody();
	AATCAircraft* First = ATCTest::StartOnRoute(World, Type, TEXT("NORTH1"), North, 15000.f);
	AATCAircraft* Behind = ATCTest::StartOnRoute(World, Type, TEXT("NORTH2"), North, 0.f, North->GetLength() - 15000.f);
	AATCAircraft* Crossing = ATCTest::StartOnRoute(World, Type, TEXT("EAST1"), East, 15000.f);
	const TArray<AATCAircraft*> All = { First, Behind, Crossing };

	// In the crossing: main gear within 25 m of where the lines cross; the time each one is through it
	auto InCrossing = [](const AATCAircraft* Aircraft) { return Aircraft->GetActorLocation().Size2D() < 2500.f; };
	TMap<FName, float> Through;
	ATCTest::FWatch Watch;
	bool bTwoInside = false;
	bool bStoodInside = false;
	while (Watch.Seconds < 900.f && !Watch.Step(World, All))
	{
		int32 Inside = 0;
		for (const AATCAircraft* Aircraft : All)
		{
			Inside += InCrossing(Aircraft) ? 1 : 0;
			bStoodInside |= InCrossing(Aircraft) && Aircraft->Movement->Speed < 50.f && Aircraft->TaxiController->GetState() != ETaxiState::Arrived;
			const FVector Location = Aircraft->GetActorLocation();
			if (!Through.Contains(Aircraft->Identity->Callsign) && (Aircraft == Crossing ? Location.X > 0.f : Location.Y < 0.f))
			{
				Through.Add(Aircraft->Identity->Callsign, Watch.Seconds);
			}
		}
		bTwoInside |= Inside > 1;
	}

	TestEqual(TEXT("no overlap, ever"), Watch.Overlaps, 0);
	TestFalse(TEXT("one aircraft at a time in the crossing"), bTwoInside);
	TestFalse(TEXT("nobody stood still in the crossing"), bStoodInside);
	TestTrue(TEXT("all three arrived"), Watch.bAllArrived);
	TestTrue(TEXT("main gear on the line (within 1 cm)"), Watch.MaxOffLineCm < 1.f);
	Through.ValueSort([](float A, float B) { return A < B; });
	TArray<FName> Order;
	Through.GenerateKeyArray(Order);
	AddInfo(FString::Printf(TEXT("Crossing order: %s, done after %.0f s"), *FString::JoinBy(Order, TEXT(", "), [](const FName& Name) { return Name.ToString(); }),
		Watch.Seconds));
	ATCTest::DestroyPlayWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCParallelLanesTest, "ATCSim.Aircraft.ParallelLanes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two parallel taxiways, 160 m apart, between the same two points. With nobody about each direction takes its
 * right-hand lane; an aircraft that would meet traffic head-on on its right-hand lane takes the other one. In both
 * cases the two pass each other without stopping.
 */
bool FATCParallelLanesTest::RunTest(const FString& Parameters)
{
	UWorld* World = ATCTest::MakePlayWorld(TEXT("ATCSimLanesWorld"));
	// +Y is right of the eastbound direction, -Y right of the westbound one
	const TArray<FVector> South = { FVector(0, 0, 0), FVector(30000, 8000, 0), FVector(170000, 8000, 0), FVector(200000, 0, 0) };
	const TArray<FVector> North = { FVector(0, 0, 0), FVector(30000, -8000, 0), FVector(170000, -8000, 0), FVector(200000, 0, 0) };
	auto Reversed = [](TArray<FVector> Points) { Algo::Reverse(Points); return Points; };
	ATaxiRoute* EastSouth = ATCTest::MakeTaxiway(World, South);
	ATaxiRoute* EastNorth = ATCTest::MakeTaxiway(World, North);
	ATaxiRoute* EastNorthOnly = ATCTest::MakeTaxiway(World, North);
	ATaxiRoute* WestNorth = ATCTest::MakeTaxiway(World, Reversed(North));
	ATaxiRoute* WestSouth = ATCTest::MakeTaxiway(World, Reversed(South));
	EastNorth->Alternatives = { EastSouth };
	WestSouth->Alternatives = { WestNorth };
	UAircraftTypeData* Type = ATCTest::MakeNarrowBody();

	// Nobody about: both take their right-hand lane, whatever they were sent on
	AATCAircraft* East = ATCTest::StartOnRoute(World, Type, TEXT("EAST1"), EastNorth, 0.f);
	AATCAircraft* West = ATCTest::StartOnRoute(World, Type, TEXT("WEST1"), WestSouth, 0.f);
	TestTrue(TEXT("eastbound keeps right (south lane)"), East->TaxiController->Route == EastSouth);
	TestTrue(TEXT("westbound keeps right (north lane)"), West->TaxiController->Route == WestNorth);
	ATCTest::FWatch First;
	while (First.Seconds < 600.f && !First.Step(World, { East, West }))
	{
	}
	TestTrue(TEXT("keeping right: both arrived"), First.bAllArrived);
	TestEqual(TEXT("keeping right: no overlap"), First.Overlaps, 0);
	TestTrue(TEXT("keeping right: nobody stopped"), First.StoodSeconds.FindRef(TEXT("EAST1")) < 2.f && First.StoodSeconds.FindRef(TEXT("WEST1")) < 2.f);
	East->Destroy();
	West->Destroy();

	// Traffic on the north lane coming east: the westbound one leaves its right-hand lane to it
	AATCAircraft* Coming = ATCTest::StartOnRoute(World, Type, TEXT("EAST2"), EastNorthOnly, 0.f);
	ATCTest::FWatch Second;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Second.Step(World, { Coming });
	}
	AATCAircraft* Other = ATCTest::StartOnRoute(World, Type, TEXT("WEST2"), WestSouth, 0.f);
	TestTrue(TEXT("opposing traffic beats keeping right: westbound takes the south lane"), Other->TaxiController->Route == WestSouth);
	while (Second.Seconds < 600.f && !Second.Step(World, { Coming, Other }))
	{
	}
	TestTrue(TEXT("opposing traffic: both arrived"), Second.bAllArrived);
	TestEqual(TEXT("opposing traffic: no overlap"), Second.Overlaps, 0);
	TestTrue(TEXT("opposing traffic: nobody stopped"), Second.StoodSeconds.FindRef(TEXT("EAST2")) < 2.f && Second.StoodSeconds.FindRef(TEXT("WEST2")) < 2.f);
	TestTrue(TEXT("main gear on the line (within 1 cm)"), First.MaxOffLineCm < 1.f && Second.MaxOffLineCm < 1.f);
	ATCTest::DestroyPlayWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCRouteLegsTest, "ATCSim.Aircraft.RouteFromLegs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Two taxiway pieces meeting at a corner as one route: one smooth line along both, the stop of the second piece comes along. */
bool FATCRouteLegsTest::RunTest(const FString& Parameters)
{
	UWorld* World = ATCTest::MakePlayWorld(TEXT("ATCSimLegsWorld"));
	ATaxiRoute* First = ATCTest::MakeRoute(World, { FVector(0, 0, 0), FVector(50000, 0, 0) });
	ATaxiRoute* Second = ATCTest::MakeRoute(World, { FVector(50000, 0, 0), FVector(50000, 50000, 0) });
	FTaxiStop Stop;
	Stop.Name = TEXT("HOLD");
	Stop.Distance = 30000.f;
	Stop.HoldTime = 1.f;
	Second->Stops = { Stop };
	Second->Rebuild();

	ATaxiRoute* Joined = ATaxiRoute::JoinRoutes(World, { First, Second });
	if (!TestNotNull(TEXT("joined route"), Joined))
	{
		ATCTest::DestroyPlayWorld(World);
		return false;
	}
	const float Length = Joined->GetLength();
	auto Off = [Joined](const FVector& Point) { return FVector::Dist(Point, Joined->Spline->FindLocationClosestToWorldLocation(Point, ESplineCoordinateSpace::World)); };
	TestTrue(TEXT("length: both pieces, minus the rounded corner"), Length > 95000.f && Length < 100000.f);
	TestTrue(TEXT("on the first piece away from the corner"), Off(FVector(20000, 0, 0)) < 10.f);
	TestTrue(TEXT("on the second piece away from the corner"), Off(FVector(50000, 30000, 0)) < 10.f);
	TestTrue(TEXT("turn rounded: does not go through the corner point"), Off(FVector(50000, 0, 0)) > 300.f);
	const float StopAt = Joined->FindNamedDistance(TEXT("HOLD"));
	TestTrue(TEXT("stop of the second piece at its place"), FMath::Abs(StopAt - (Length - 20000.f)) < 200.f);

	// An aircraft over both pieces: through the corner, stops at HOLD for a second, ends at the end of the second piece
	UAircraftTypeData* Type = ATCTest::MakeNarrowBody();
	AATCAircraft* Aircraft = World->SpawnActor<AATCAircraft>(FVector::ZeroVector, FRotator::ZeroRotator);
	Aircraft->Identity->Callsign = TEXT("LEGS1");
	Aircraft->SetAircraftType(Type);
	Aircraft->PlaceOnRoute(First, 0.f);
	TestTrue(TEXT("taxi via the two pieces"), Aircraft->TaxiController->StartTaxiVia({ First, Second }));
	ATCTest::FWatch Watch;
	bool bStopped = false;
	while (Watch.Seconds < 600.f && !Watch.Step(World, { Aircraft }))
	{
		bStopped |= Aircraft->TaxiController->GetState() == ETaxiState::Holding;
	}
	TestTrue(TEXT("arrived"), Watch.bAllArrived);
	TestTrue(TEXT("held at HOLD"), bStopped);
	TestTrue(TEXT("ends at the end of the second piece"), FVector::Dist(Aircraft->GetActorLocation(), FVector(50000, 50000, 0)) < 50.f);
	TestTrue(TEXT("main gear on the line (within 1 cm)"), Watch.MaxOffLineCm < 1.f);
	ATCTest::DestroyPlayWorld(World);
	return true;
}

namespace ATCTest
{
	/** A claim request for an aircraft of this type on a route. */
	FTaxiClaimRequest ClaimFor(const UAircraftTypeData* Type, ATaxiRoute* Route, float RearCm, float FrontCm)
	{
		FTaxiClaimRequest Request;
		Request.Route = Route;
		Request.RearCm = RearCm;
		Request.FrontCm = FrontCm;
		Request.HalfSpanCm = Type->GetHalfSpanCm();
		Request.LengthCm = Type->GetLengthCm();
		Request.NoseOffsetCm = Type->GetNoseOffsetCm();
		return Request;
	}

	/** An aircraft standing on a route, not taxiing: the test asks the network for its reservations itself. */
	AATCAircraft* StandOnRoute(UWorld* World, UAircraftTypeData* Type, const TCHAR* Callsign, ATaxiRoute* Route, float DistanceCm)
	{
		AATCAircraft* Aircraft = World->SpawnActor<AATCAircraft>(FVector::ZeroVector, FRotator::ZeroRotator);
		Aircraft->Identity->Callsign = Callsign;
		Aircraft->SetAircraftType(Type);
		Aircraft->PlaceOnRoute(Route, DistanceCm);
		Aircraft->Movement->SetPath(Route->Spline, ETravelDirection::Forward, true, DistanceCm);
		return Aircraft;
	}

	/** Game time passes; frames of 0.25 s (the world takes at most 0.4 s per frame). */
	void Advance(UWorld* World, float Seconds)
	{
		for (float Done = 0.f; Done < Seconds; Done += 0.25f)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.25f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCLandingPriorityTest, "ATCSim.Aircraft.LandingPriority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A departure queues for a taxiway while an aircraft on it comes the other way. When that one has gone, a landing asks
 * for the same taxiway in that other direction. On the ground whoever waits longest goes first; a landing counts as
 * having waited LandingHeadStartSeconds already, so it goes first, unless the departure has waited longer than that.
 */
bool FATCLandingPriorityTest::RunTest(const FString& Parameters)
{
	UWorld* World = ATCTest::MakePlayWorld(TEXT("ATCSimPriorityWorld"));
	// The same 1 km taxiway both ways, like in HeadOnOneTaxiway; it starts as far along both routes
	ATaxiRoute* East = ATCTest::MakeTaxiway(World, { FVector(-50000, 40000, 0), FVector(-20000, 10000, 0), FVector(0, 0, 0), FVector(100000, 0, 0),
		FVector(120000, 10000, 0), FVector(150000, 40000, 0) });
	ATaxiRoute* West = ATCTest::MakeTaxiway(World, { FVector(150000, -40000, 0), FVector(120000, -10000, 0), FVector(100000, 0, 0), FVector(0, 0, 0),
		FVector(-20000, -10000, 0), FVector(-50000, -40000, 0) });
	const float SharedStart = (East->GetLength() - 100000.f) / 2.f;
	UTaxiNetworkSubsystem* Network = World->GetSubsystem<UTaxiNetworkSubsystem>();
	UAircraftTypeData* Type = ATCTest::MakeNarrowBody();
	AATCAircraft* Taxiing = ATCTest::StandOnRoute(World, Type, TEXT("EAST1"), East, SharedStart + 50000.f);
	AATCAircraft* Departure = ATCTest::StandOnRoute(World, Type, TEXT("WEST1"), West, SharedStart - 10000.f);
	AATCAircraft* Landing = ATCTest::StandOnRoute(World, Type, TEXT("LAND1"), East, SharedStart + 5000.f);

	AActor* Blocker = nullptr;
	TestTrue(TEXT("EAST1 is on the shared taxiway"), Network->ClaimStretch(Taxiing, ATCTest::ClaimFor(Type, East, SharedStart + 45000.f, SharedStart + 55000.f), Blocker));
	const FTaxiClaimRequest Ahead = ATCTest::ClaimFor(Type, West, SharedStart - 5000.f, SharedStart + 5000.f);
	TestFalse(TEXT("WEST1 cannot take it the other way"), Network->ReserveTwoWay(Departure, Ahead, Blocker));
	TestTrue(TEXT("WEST1 waits for EAST1"), Blocker == Taxiing);
	ATCTest::Advance(World, 5.f);
	Network->ReleaseClaim(Taxiing);

	// The landing's exit lies on the shared taxiway, the way EAST1 went
	FTaxiClaimRequest Exit = ATCTest::ClaimFor(Type, East, SharedStart + 5000.f, SharedStart + 15000.f);
	TestFalse(TEXT("on the ground WEST1 would go first: it waited longer"), Network->ClaimStretch(Landing, Exit, Blocker, true));
	TestTrue(TEXT("held up by WEST1's place in the queue"), Blocker == Departure);
	Exit.bPriority = true;
	TestTrue(TEXT("a landing goes before everybody on the ground"), Network->ClaimStretch(Landing, Exit, Blocker));
	TestFalse(TEXT("WEST1 waits again"), Network->ReserveTwoWay(Departure, Ahead, Blocker));
	TestTrue(TEXT("now for the landing"), Blocker == Landing);

	// The landing is through and EAST1 is back on the shared taxiway: WEST1 keeps waiting, now longer than a landing's
	// head start. Then the next landing waits for WEST1
	Network->ReleaseClaim(Landing);
	Network->ClaimBody(Taxiing, ATCTest::ClaimFor(Type, East, SharedStart + 45000.f, SharedStart + 55000.f));
	TestFalse(TEXT("WEST1 keeps waiting"), Network->ReserveTwoWay(Departure, Ahead, Blocker));
	ATCTest::Advance(World, GetDefault<UATCSimSettings>()->LandingHeadStartSeconds);
	TestFalse(TEXT("WEST1, waiting longer than the head start, goes before the next landing"), Network->ClaimStretch(Landing, Exit, Blocker, true));
	TestTrue(TEXT("held up by WEST1"), Blocker == Departure);
	ATCTest::DestroyPlayWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FATCDeadlockTest, "ATCSim.Aircraft.DeadlockBreaker", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A circle across the two layers: NORTH1 has reserved the crossing ahead, but stands still for EAST1, which is closer to
 * it (the give-way rule of the traffic layer), while EAST1 waits for that reservation. After a minute the deadlock
 * check sees the circle; NORTH1, which holds the crossing ahead of its nose, lets go of it and EAST1 gets the crossing.
 */
bool FATCDeadlockTest::RunTest(const FString& Parameters)
{
	UWorld* World = ATCTest::MakePlayWorld(TEXT("ATCSimDeadlockWorld"));
	ATaxiRoute* North = ATCTest::MakeTaxiway(World, { FVector(0, 60000, 0), FVector(0, -60000, 0) });
	ATaxiRoute* East = ATCTest::MakeTaxiway(World, { FVector(-60000, 0, 0), FVector(60000, 0, 0) });
	UTaxiNetworkSubsystem* Network = World->GetSubsystem<UTaxiNetworkSubsystem>();
	UAircraftTypeData* Type = ATCTest::MakeNarrowBody();
	// Both cross at 600 m along their route. NORTH1 300 m before it; EAST1 with its nose just before it, so it holds
	// nothing ahead of its nose: only NORTH1 can free something
	const float Nose = Type->GetNoseOffsetCm();
	AATCAircraft* NorthOne = ATCTest::StandOnRoute(World, Type, TEXT("NORTH1"), North, 30000.f);
	AATCAircraft* EastOne = ATCTest::StandOnRoute(World, Type, TEXT("EAST1"), East, 54600.f - Nose);
	auto Ask = [&](AATCAircraft* Aircraft, ATaxiRoute* Route, const AActor* HeldBy)
	{
		const float Front = Aircraft->Movement->Distance + Nose;
		FTaxiClaimRequest Request = ATCTest::ClaimFor(Type, Route, Front - Type->GetLengthCm(), Front);
		Request.WantedCm = 80000.f;   // through the crossing
		Request.HeldBy = HeldBy;
		AActor* Blocker = nullptr;
		Network->UpdateClaim(Aircraft, Request, Blocker);
		return Blocker;
	};
	TestNull(TEXT("NORTH1 reserves through the crossing"), Ask(NorthOne, North, nullptr));
	TestTrue(TEXT("EAST1 waits for that reservation"), Ask(EastOne, East, nullptr) == NorthOne);
	Ask(NorthOne, North, EastOne);

	ATCTest::Advance(World, 40.f);
	TestEqual(TEXT("no deadlock yet after 40 s"), Network->GetDeadlockCount(), 0);
	ATCTest::Advance(World, 25.f);
	TestEqual(TEXT("one deadlock after a minute"), Network->GetDeadlockCount(), 1);
	ATaxiRoute* Route = nullptr;
	float From = 0.f;
	float To = 0.f;
	TestTrue(TEXT("NORTH1 keeps the place it stands on"), Network->GetClaim(NorthOne, Route, From, To));
	TestTrue(TEXT("NORTH1 let go of the crossing"), To < 55000.f);
	TestNull(TEXT("EAST1 now gets the crossing"), Ask(EastOne, East, nullptr));
	TestTrue(TEXT("EAST1 reserved through it"), Network->GetClaim(EastOne, Route, From, To) && To > 65000.f);
	ATCTest::DestroyPlayWorld(World);
	return true;
}
