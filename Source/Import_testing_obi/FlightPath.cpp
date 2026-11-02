#include "FlightPath.h"
#include "Components/SplineComponent.h"

namespace
{
	/** Spline point plus optional exact travel direction (zero = taken from the neighbour points). */
	struct FPathPoint
	{
		FVector Position;
		FVector Direction;
	};

	FPathPoint Pt(double X, double Y, double Z)
	{
		return { FVector(X, Y, Z), FVector::ZeroVector };
	}

	FPathPoint Pt(double X, double Y, double Z, double DirX, double DirY, double DirZ)
	{
		return { FVector(X, Y, Z), FVector(DirX, DirY, DirZ) };
	}

	/** Fills a spline with smooth custom tangents. Tangent length = shortest neighbour segment, so the curve never overshoots. */
	void BuildSpline(USplineComponent* Spline, const TArray<FPathPoint>& Points)
	{
		const int32 Num = Points.Num();

		Spline->ClearSplinePoints(false);
		for (const FPathPoint& Point : Points)
		{
			Spline->AddSplinePoint(Point.Position, ESplineCoordinateSpace::Local, false);
		}

		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FVector& Here = Points[Index].Position;
			const FVector& Prev = Points[FMath::Max(Index - 1, 0)].Position;
			const FVector& Next = Points[FMath::Min(Index + 1, Num - 1)].Position;

			FVector Direction = Points[Index].Direction;
			if (Direction.IsNearlyZero())
			{
				Direction = Next - Prev;
			}

			// Points on the ground keep a flat tangent so the curve never dips below the ground.
			if (Here.Z <= 0.0)
			{
				Direction.Z = 0.0;
			}

			const double PrevLength = Index > 0 ? FVector::Dist(Here, Prev) : FVector::Dist(Here, Next);
			const double NextLength = Index < Num - 1 ? FVector::Dist(Here, Next) : PrevLength;
			const FVector Tangent = Direction.GetSafeNormal() * FMath::Min(PrevLength, NextLength);

			Spline->SetTangentsAtSplinePoint(Index, Tangent, Tangent, ESplineCoordinateSpace::Local, false);
			Spline->SetSplinePointType(Index, ESplinePointType::CurveCustomTangent, false);
		}

		Spline->UpdateSpline();
	}
}

AFlightPath::AFlightPath()
{
	PrimaryActorTick.bCanEverTick = false;

	// Always loaded in World Partition (Open World) maps, the route is kilometers long (an editor-only setting)
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	auto MakeSpline = [this](const TCHAR* Name, const TArray<FPathPoint>& Points)
	{
		USplineComponent* Spline = CreateDefaultSubobject<USplineComponent>(Name);
		Spline->SetupAttachment(RootComponent);
		Spline->ReparamStepsPerSegment = 32;
		BuildSpline(Spline, Points);
		return Spline;
	};

	// Default layout (cm). Gate at the actor origin, nose facing +Y. Runway centerline at Y = -9000, takeoff towards +X.
	// Every spline starts exactly where the previous one ends. Edit the points in the level to make your own airport.

	// Points go in nose direction (towards the gate). The aircraft travels this spline from end to start.
	PushbackSpline = MakeSpline(TEXT("PushbackSpline"), {
		Pt(-5000, -9000, 0, 1, 0, 0),
		Pt(-1464, -7536, 0),
		Pt(0, -4000, 0, 0, 1, 0),
		Pt(0, -2000, 0),
		Pt(0, 0, 0, 0, 1, 0),
	});

	TaxiSpline = MakeSpline(TEXT("TaxiSpline"), {
		Pt(-5000, -9000, 0),
		Pt(5000, -9000, 0),
		Pt(15000, -9000, 0),
		Pt(25000, -9000, 0),
		Pt(35000, -9000, 0),
		Pt(45000, -9000, 0),
	});

	TakeoffSpline = MakeSpline(TEXT("TakeoffSpline"), {
		Pt(45000, -9000, 0),
		Pt(85000, -9000, 0),
		Pt(125000, -9000, 0),
		Pt(165000, -9000, 0),
		Pt(205000, -9000, 400),
		Pt(245000, -9000, 2500, 1, 0, 0.0525),
	});

	DepartureSpline = MakeSpline(TEXT("DepartureSpline"), {
		Pt(245000, -9000, 2500, 1, 0, 0.0525),
		Pt(445000, -9000, 15000),
		Pt(695000, -9000, 45000),
		Pt(945000, -9000, 80000),
		Pt(1195000, -9000, 98000),
		Pt(1445000, -9000, 100000, 1, 0, 0),
	});

	const double CruiseZ = 100000.0;
	CruiseSpline = MakeSpline(TEXT("CruiseSpline"), {
		Pt(1445000, -9000, CruiseZ, 1, 0, 0),
		Pt(1657132, 78868, CruiseZ),
		Pt(1745000, 291000, CruiseZ),
		Pt(1657132, 503132, CruiseZ),
		Pt(1445000, 591000, CruiseZ, -1, 0, 0),
		Pt(820000, 591000, CruiseZ),
		Pt(195000, 591000, CruiseZ),
		Pt(-430000, 591000, CruiseZ),
		Pt(-1055000, 591000, CruiseZ, -1, 0, 0),
		Pt(-1267132, 503132, CruiseZ),
		Pt(-1355000, 291000, CruiseZ),
		Pt(-1267132, 78868, CruiseZ),
		Pt(-1055000, -9000, CruiseZ, 1, 0, 0),
	});

	ArrivalSpline = MakeSpline(TEXT("ArrivalSpline"), {
		Pt(-1055000, -9000, CruiseZ, 1, 0, 0),
		Pt(-830000, -9000, 75000),
		Pt(-605000, -9000, 50000),
		Pt(-455000, -9000, 30000),
		Pt(-305000, -9000, 20000, 1, 0, -0.0524),
	});

	// 3 degree glide slope, touchdown at X = 77000, rollout to X = 157000.
	LandingSpline = MakeSpline(TEXT("LandingSpline"), {
		Pt(-305000, -9000, 20000, 1, 0, -0.0524),
		Pt(-105000, -9000, 9500),
		Pt(77000, -9000, 0),
		Pt(117000, -9000, 0),
		Pt(157000, -9000, 0, 1, 0, 0),
	});

	TaxiToGateSpline = MakeSpline(TEXT("TaxiToGateSpline"), {
		Pt(157000, -9000, 0, 1, 0, 0),
		Pt(160536, -10464, 0),
		Pt(162000, -14000, 0),
		Pt(160536, -17536, 0),
		Pt(157000, -19000, 0, -1, 0, 0),
		Pt(119000, -19000, 0),
		Pt(81000, -19000, 0),
		Pt(43000, -19000, 0),
		Pt(5000, -19000, 0, -1, 0, 0),
		Pt(1464, -17536, 0),
		Pt(0, -14000, 0, 0, 1, 0),
		Pt(0, -9000, 0),
		Pt(0, -4000, 0),
		Pt(0, 0, 0, 0, 1, 0),
	});
}

USplineComponent* AFlightPath::GetSpline(EAircraftState State) const
{
	switch (State)
	{
	case EAircraftState::Pushback:   return PushbackSpline;
	case EAircraftState::Taxi:       return TaxiSpline;
	case EAircraftState::Takeoff:    return TakeoffSpline;
	case EAircraftState::Departure:  return DepartureSpline;
	case EAircraftState::Cruise:     return CruiseSpline;
	case EAircraftState::Arrival:    return ArrivalSpline;
	case EAircraftState::Landing:    return LandingSpline;
	case EAircraftState::TaxiToGate: return TaxiToGateSpline;
	default:                         return nullptr;
	}
}
