#include "TaxiRoute.h"
#include "AircraftMovementComponent.h"
#include "ATCUnits.h"
#include "TrafficSubsystem.h"
#include "Algo/BinarySearch.h"
#include "Components/BillboardComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ATaxiStopMarker::ATaxiStopMarker()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
#if WITH_EDITORONLY_DATA
	Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(RootComponent);
	}
#endif
	Stop.Name = TEXT("Stop");
}

ATaxiRoute::ATaxiRoute()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = Spline;
	Spline->SetMobility(EComponentMobility::Static);

	Lights = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Lights"));
	Lights->SetupAttachment(Spline);
	Lights->SetMobility(EComponentMobility::Static);   // never moves: lights switch through custom data, and Movable made every light a dynamic instance
	Lights->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lights->SetCanEverAffectNavigation(false);
	Lights->SetCastShadow(false);
	Lights->bAffectDistanceFieldLighting = false;
	Lights->bSelectable = false;   // keeps the lights out of the hit proxy pass (Simulate in the editor viewport)
	Lights->NumCustomDataFloats = 4;
}

float ATaxiRoute::GetLength() const
{
	return Spline->GetSplineLength();
}

FVector ATaxiRoute::GetDirectionAt(float Distance) const
{
	return Spline->GetDirectionAtDistanceAlongSpline(FMath::Clamp(Distance, 0.f, GetLength()), ESplineCoordinateSpace::World);
}

const TArray<float>& ATaxiRoute::GetCurvatureTable(float StepCm) const
{
	const float Step = FMath::Max(StepCm, 50.f);
	const float Length = GetLength();
	const int32 Points = Spline->GetNumberOfSplinePoints();
	if (Step != CurvatureStepCm || Points != CurvaturePoints || !FMath::IsNearlyEqual(Length, CurvatureLengthCm, 1.f))
	{
		CurvatureStepCm = Step;
		CurvatureLengthCm = Length;
		CurvaturePoints = Points;
		CurvatureTable.Reset();
		const int32 Num = FMath::Max(FMath::CeilToInt(Length / Step), 1);
		CurvatureTable.Reserve(Num);
		FVector Previous = GetDirectionAt(0.f);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FVector Next = GetDirectionAt((Index + 1) * Step);
			CurvatureTable.Add(FMath::Acos(FMath::Clamp(FVector::DotProduct(Previous, Next), -1.f, 1.f)) / Step);
			Previous = Next;
		}
	}
	return CurvatureTable;
}

FVector ATaxiRoute::GetLocationAt(float Distance) const
{
	const float Length = GetLength();
	const float OnRoute = FMath::Clamp(Distance, 0.f, Length);
	const FVector Location = Spline->GetLocationAtDistanceAlongSpline(OnRoute, ESplineCoordinateSpace::World);
	if (Distance == OnRoute)
	{
		return Location;
	}
	// Straight on past the ends, the way the last two metres go (the tangent at an end point can be zero)
	const FVector Inside = Spline->GetLocationAtDistanceAlongSpline(Distance < 0.f ? FMath::Min(200.f, Length) : FMath::Max(Length - 200.f, 0.f),
		ESplineCoordinateSpace::World);
	return Location + (Location - Inside).GetSafeNormal() * FMath::Abs(Distance - OnRoute);
}

float ATaxiRoute::GetRunwayVacatedCm(float ClearCm) const
{
	// The runway centre line: the start of the route and the landing direction from its first 20 m
	const FVector Start = GetLocationAt(0.f);
	const FVector Along = (GetLocationAt(2000.f) - Start).GetSafeNormal2D();
	for (float Distance = 0.f; Distance < GetLength(); Distance += 500.f)
	{
		const FVector Offset = GetLocationAt(Distance) - Start;
		if (FMath::Abs(Offset.X * Along.Y - Offset.Y * Along.X) >= ClearCm)
		{
			return Distance;
		}
	}
	return GetLength();
}

ATaxiRoute* ATaxiRoute::JoinRoutes(UObject* WorldContext, const TArray<ATaxiRoute*>& InLegs)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || InLegs.Num() == 0)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.bDeferConstruction = true;
	ATaxiRoute* Route = World->SpawnActor<ATaxiRoute>(ATaxiRoute::StaticClass(), FTransform::Identity, Params);
	if (!Route)
	{
		return nullptr;
	}
	Route->bShowCenterline = false;
	Route->bShowLights = false;   // the legs light up with their own lights
	Route->bDrawDebug = false;
	Route->Legs.Append(InLegs);
	Route->FinishSpawning(FTransform::Identity);
	return Route;
}

int32 ATaxiRoute::GetZoneIndexAt(float Distance) const
{
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < SpeedZones.Num(); ++Index)
	{
		const FSpeedZone& Zone = SpeedZones[Index];
		if (Distance >= Zone.StartDistance && Distance <= Zone.EndDistance
			&& (Best == INDEX_NONE || Zone.MaxSpeedKt < SpeedZones[Best].MaxSpeedKt))
		{
			Best = Index;
		}
	}
	return Best;
}

float ATaxiRoute::GetZoneLimitKt(float Distance) const
{
	const int32 Index = GetZoneIndexAt(Distance);
	return Index == INDEX_NONE ? 1000.f : SpeedZones[Index].MaxSpeedKt;
}

float ATaxiRoute::FindNamedDistance(FName Name) const
{
	for (const FTaxiStop& Stop : ResolvedStops)
	{
		if (Stop.Name == Name)
		{
			return Stop.Distance;
		}
	}
	for (const FTaxiWaypoint& Waypoint : Waypoints)
	{
		if (Waypoint.Name == Name)
		{
			return Waypoint.Distance;
		}
	}
	return -1.f;
}

void ATaxiRoute::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void ATaxiRoute::Rebuild()
{
	if (Legs.Num() > 0)
	{
		BuildFromLegs();
	}
	ResolveStops();
	BuildCenterline();
	BuildLights();
}

void ATaxiRoute::BuildFromLegs()
{
	TArray<ATaxiRoute*> Used;
	for (ATaxiRoute* Leg : Legs)
	{
		if (Leg && Leg != this && !Leg->Legs.Contains(this) && Leg->GetLength() > 100.f)
		{
			Used.Add(Leg);
		}
	}
	if (Used.Num() == 0)
	{
		return;
	}

	// Where each leg meets the next: the closest pair of points, the end of one on the other or where they cross
	TArray<float> Enter;
	TArray<float> Exit;
	Enter.Init(0.f, Used.Num());
	Exit.Init(0.f, Used.Num());
	for (int32 Index = 0; Index < Used.Num(); ++Index)
	{
		Exit[Index] = Used[Index]->GetLength();
	}
	for (int32 Index = 0; Index + 1 < Used.Num(); ++Index)
	{
		const ATaxiRoute* Leg = Used[Index];
		const ATaxiRoute* Next = Used[Index + 1];
		float Best = BIG_NUMBER;
		for (float Distance = Enter[Index];; Distance += 100.f)
		{
			const float At = FMath::Min(Distance, Leg->GetLength());
			const FVector Point = Leg->Spline->GetLocationAtDistanceAlongSpline(At, ESplineCoordinateSpace::World);
			const float OnNext = UAircraftMovementComponent::GetDistanceClosestTo(Next->Spline, Point);
			const float Gap = FVector::Dist(Point, Next->Spline->GetLocationAtDistanceAlongSpline(OnNext, ESplineCoordinateSpace::World));
			if (Gap < Best - 1.f)
			{
				Best = Gap;
				Exit[Index] = At;
				Enter[Index + 1] = OnNext;
			}
			if (At >= Leg->GetLength())
			{
				break;
			}
		}
	}

	// What the legs say about stops, waypoints and speed zones, by place: the distances change with the new spline
	TArray<TPair<FTaxiStop, FVector>> LegStops;
	TArray<TPair<FTaxiWaypoint, FVector>> LegWaypoints;
	TArray<TPair<FSpeedZone, TPair<FVector, FVector>>> LegZones;
	TArray<FVector> Points;
	TArray<int32> Joins;
	for (int32 Index = 0; Index < Used.Num(); ++Index)
	{
		const ATaxiRoute* Leg = Used[Index];
		if (Exit[Index] < Enter[Index])
		{
			UE_LOG(LogATC, Warning, TEXT("%s: leg %s would be driven backwards; check the order of the legs."), *GetName(), *Leg->GetName());
			Exit[Index] = Enter[Index];
		}
		auto Inside = [&](float Distance) { return Distance >= Enter[Index] - 1.f && Distance <= Exit[Index] + 1.f; };
		for (const FTaxiStop& Stop : Leg->GetAllStops())
		{
			if (Inside(Stop.Distance))
			{
				LegStops.Add({ Stop, Leg->GetLocationAt(Stop.Distance) });
			}
		}
		for (const FTaxiWaypoint& Waypoint : Leg->Waypoints)
		{
			if (Inside(Waypoint.Distance))
			{
				LegWaypoints.Add({ Waypoint, Leg->GetLocationAt(Waypoint.Distance) });
			}
		}
		for (const FSpeedZone& Zone : Leg->SpeedZones)
		{
			const float From = FMath::Max(Zone.StartDistance, Enter[Index]);
			const float To = FMath::Min(Zone.EndDistance, Exit[Index]);
			if (From < To)
			{
				LegZones.Add(MakeTuple(Zone, MakeTuple(Leg->GetLocationAt(From), Leg->GetLocationAt(To))));
			}
		}
		// Points every 3 m along the part of the leg that is used
		if (Index > 0)
		{
			Joins.Add(Points.Num());
		}
		for (float Distance = Enter[Index];; Distance += 300.f)
		{
			const float At = FMath::Min(Distance, Exit[Index]);
			Points.Add(Leg->Spline->GetLocationAtDistanceAlongSpline(At, ESplineCoordinateSpace::World));
			if (At >= Exit[Index])
			{
				break;
			}
		}
	}

	// A rounded turn at every join: a curve that leaves the first leg along it and joins the next one along it
	const int32 Steps = FMath::Max(2, FMath::RoundToInt(JoinRadiusCm / 300.f));
	for (int32 Index = Joins.Num() - 1; Index >= 0; --Index)
	{
		const int32 Join = Joins[Index];
		const int32 Before = FMath::Max(Join - 1 - Steps, 0);
		const int32 After = FMath::Min(Join + Steps, Points.Num() - 1);
		const FVector Corner = (Points[Join - 1] + Points[Join]) * 0.5f;
		const FVector From = Points[Before];
		const FVector To = Points[After];
		TArray<FVector> Curve;
		for (int32 Step = 1; Step < 2 * Steps; ++Step)
		{
			const float T = float(Step) / (2 * Steps);
			Curve.Add(FMath::Lerp(FMath::Lerp(From, Corner, T), FMath::Lerp(Corner, To, T), T));
		}
		Points.RemoveAt(Before + 1, After - Before - 1);
		Points.Insert(Curve, Before + 1);
	}

	Spline->ClearSplinePoints(false);
	FVector Previous = FVector(BIG_NUMBER);
	for (const FVector& Point : Points)
	{
		if (FVector::DistSquared(Previous, Point) > 50.f * 50.f)
		{
			Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
			Previous = Point;
		}
	}
	for (int32 Index = 0; Index < Spline->GetNumberOfSplinePoints(); ++Index)
	{
		Spline->SetSplinePointType(Index, ESplinePointType::CurveClamped, false);
	}
	Spline->UpdateSpline();

	Stops.Reset();
	Waypoints.Reset();
	SpeedZones.Reset();
	for (const TPair<FTaxiStop, FVector>& Entry : LegStops)
	{
		FTaxiStop& Stop = Stops.Add_GetRef(Entry.Key);
		Stop.Distance = UAircraftMovementComponent::GetDistanceClosestTo(Spline, Entry.Value);
	}
	for (const TPair<FTaxiWaypoint, FVector>& Entry : LegWaypoints)
	{
		FTaxiWaypoint& Waypoint = Waypoints.Add_GetRef(Entry.Key);
		Waypoint.Distance = UAircraftMovementComponent::GetDistanceClosestTo(Spline, Entry.Value);
	}
	for (const TPair<FSpeedZone, TPair<FVector, FVector>>& Entry : LegZones)
	{
		FSpeedZone& Zone = SpeedZones.Add_GetRef(Entry.Key);
		Zone.StartDistance = UAircraftMovementComponent::GetDistanceClosestTo(Spline, Entry.Value.Key);
		Zone.EndDistance = UAircraftMovementComponent::GetDistanceClosestTo(Spline, Entry.Value.Value);
	}
}

void ATaxiRoute::ResolveStops()
{
	ResolvedStops = Stops;
	for (const ATaxiStopMarker* Marker : StopMarkers)
	{
		if (Marker)
		{
			FTaxiStop Stop = Marker->Stop;
			Stop.Distance = UAircraftMovementComponent::GetDistanceClosestTo(Spline, Marker->GetActorLocation());
			ResolvedStops.Add(Stop);
		}
	}
	ResolvedStops.Sort([](const FTaxiStop& A, const FTaxiStop& B) { return A.Distance < B.Distance; });
}

void ATaxiRoute::BuildCenterline()
{
	// Segments are instance components, so they are saved with the level; every rebuild replaces them
	TInlineComponentArray<USplineMeshComponent*> Old(this);
	for (USplineMeshComponent* Segment : Old)
	{
		RemoveInstanceComponent(Segment);
		Segment->DestroyComponent();
	}
	if (!bShowCenterline)
	{
		return;
	}

	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!Plane)
	{
		return;
	}

	const float Length = GetLength();
	const int32 Count = FMath::Max(1, FMath::CeilToInt(Length / 500.f));
	const float Step = Length / Count;
	const FVector Lift(0.f, 0.f, 2.f);  // just above the ground, no z-fighting
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float D0 = Index * Step;
		const float D1 = (Index + 1) * Step;

		USplineMeshComponent* Segment = NewObject<USplineMeshComponent>(this);
		Segment->CreationMethod = EComponentCreationMethod::Instance;
		AddInstanceComponent(Segment);
		Segment->SetMobility(EComponentMobility::Static);
		Segment->SetStaticMesh(Plane);
		if (CenterlineMaterial)
		{
			Segment->SetMaterial(0, CenterlineMaterial);
		}
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetCastShadow(false);
		Segment->SetupAttachment(Spline);

		const FVector P0 = Spline->GetLocationAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local) + Lift;
		const FVector P1 = Spline->GetLocationAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local) + Lift;
		const FVector T0 = Spline->GetDirectionAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local) * Step;
		const FVector T1 = Spline->GetDirectionAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local) * Step;
		Segment->SetStartAndEnd(P0, T0, P1, T1, false);

		const FVector2D Scale(CenterlineWidthCm / 100.f, 1.f);
		Segment->SetStartScale(Scale, false);
		Segment->SetEndScale(Scale, false);
		Segment->RegisterComponent();
	}
}

namespace
{
	const FLinearColor Green(0.05f, 1.f, 0.3f);
	const FLinearColor Red(1.f, 0.03f, 0.02f);
}

void ATaxiRoute::SetShowLights(bool bShow)
{
	if (bShowLights != bShow)
	{
		bShowLights = bShow;
		BuildLights();
	}
}

void ATaxiRoute::BuildLights()
{
	Lights->ClearInstances();
	LightDistances.Reset();
	StopBars.Reset();
	LightOn.Reset();
	Guidance.Reset();
	bLightsBuilt = true;
	// Only while playing: the editor's hit proxy pass cannot build a pipeline for the light material (it moves vertices),
	// which is a fatal shader error in the editor
	if (!bShowLights || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	if (!LightMesh)
	{
		LightMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/ATCSim/Lights/SM_TaxiLight.SM_TaxiLight"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!LightMaterial)
	{
		LightMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/ATCSim/Lights/M_TaxiLight.M_TaxiLight"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!LightMesh || !LightMaterial)
	{
		UE_LOG(LogATC, Warning, TEXT("%s: no taxi light mesh or material; run ATC.CreateTaxiLights."), *GetName());
		return;
	}
	Lights->SetStaticMesh(LightMesh);
	Lights->SetMaterial(0, LightMaterial);
	Lights->SetNumCustomDataFloats(4);

	// Centerline: every LightSpacingCm, half of it where the route turns more than 10 degrees
	const float Length = GetLength();
	const float Spacing = FMath::Max(LightSpacingCm, 100.f);
	const FVector Lift(0.f, 0.f, 2.f);
	TArray<FTransform> Transforms;
	for (float D = Spacing * 0.5f; D < Length;)
	{
		LightDistances.Add(D);
		Transforms.Add(FTransform(Spline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World) + Lift));
		const bool bTurn = FVector::DotProduct(GetDirectionAt(D), GetDirectionAt(D + Spacing)) < FMath::Cos(FMath::DegreesToRadians(10.f));
		D += bTurn ? Spacing * 0.5f : Spacing;
	}

	// Stop bars: a row of lights across the route, one every 3 m
	for (int32 StopIndex = 0; StopIndex < ResolvedStops.Num(); ++StopIndex)
	{
		const FTaxiStop& Stop = ResolvedStops[StopIndex];
		if (!Stop.bRequiresClearance)
		{
			continue;
		}
		FStopBar Bar;
		Bar.StopIndex = StopIndex;
		Bar.Distance = Stop.Distance + StopBarOffsetCm;
		for (const FTaxiWaypoint& Waypoint : Waypoints)
		{
			if (Waypoint.Name == Stop.Name)
			{
				Bar.Distance = Waypoint.Distance;
			}
		}
		Bar.Distance = FMath::Min(Bar.Distance, Length);
		Bar.FirstLight = Transforms.Num();
		Bar.NumLights = FMath::Max(2, FMath::RoundToInt(StopBarWidthCm / 300.f) + 1);
		const FVector Center = Spline->GetLocationAtDistanceAlongSpline(Bar.Distance, ESplineCoordinateSpace::World) + Lift;
		const FVector Side = FVector::CrossProduct(FVector::UpVector, GetDirectionAt(Bar.Distance)).GetSafeNormal();
		for (int32 Index = 0; Index < Bar.NumLights; ++Index)
		{
			Transforms.Add(FTransform(Center + Side * StopBarWidthCm * (float(Index) / (Bar.NumLights - 1) - 0.5f)));
		}
		StopBars.Add(Bar);
	}

	Lights->AddInstances(Transforms, false, true, false);
	for (int32 Index = 0; Index < Transforms.Num(); ++Index)
	{
		const bool bBar = Index >= LightDistances.Num();
		const FLinearColor& Color = bBar ? Red : Green;
		const float Data[4] = { bBar ? 1.f : 0.f, Color.R, Color.G, Color.B };
		Lights->SetCustomData(Index, MakeArrayView(Data));
		LightOn.Add(bBar);
	}
	Lights->MarkRenderStateDirty();
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		UE_LOG(LogATC, Verbose, TEXT("%s: %d centerline lights, %d stop bars"), *RouteTag.ToString(), LightDistances.Num(), StopBars.Num());
	}
}

void ATaxiRoute::SetLightOn(int32 Index, bool bOn)
{
	Lights->SetCustomDataValue(Index, 0, bOn ? 1.f : 0.f);
}

void ATaxiRoute::ShowGuidance(const UObject* Aircraft, float Distance, int32 ClearedStops)
{
	if (!bLightsBuilt && bShowLights)
	{
		BuildLights();
	}
	if (Lights->GetInstanceCount() == 0)
	{
		return;
	}

	// Greens up to the first bar the aircraft is not cleared through; a bar it is cleared through stays off until the
	// aircraft is well past it, then lights again for the next one
	FGuidance New;
	float End = FMath::Min(GetLength(), Distance + GuidanceAheadCm);
	for (int32 BarIndex = 0; BarIndex < StopBars.Num() && BarIndex < 64; ++BarIndex)
	{
		const FStopBar& Bar = StopBars[BarIndex];
		if (Bar.StopIndex >= ClearedStops && Bar.Distance > Distance)
		{
			End = FMath::Min(End, Bar.Distance);
		}
		if (Bar.StopIndex < ClearedStops && Distance < Bar.Distance + 4000.f)
		{
			New.BarsOff |= uint64(1) << BarIndex;
		}
	}
	New.First = Algo::LowerBound(LightDistances, Distance);
	New.Last = FMath::Max(New.First, Algo::UpperBound(LightDistances, End));   // exclusive

	FGuidance& Current = Guidance.FindOrAdd(Aircraft);
	if (Current.First != New.First || Current.Last != New.Last || Current.BarsOff != New.BarsOff)
	{
		Current = New;
		RefreshLights();
	}
}

void ATaxiRoute::ClearGuidance(const UObject* Aircraft)
{
	if (Guidance.Remove(Aircraft) > 0)
	{
		RefreshLights();
	}
}

void ATaxiRoute::RefreshLights()
{
	// Every aircraft on the route lights its own stretch; a bar is off while any aircraft is cleared through it
	TArray<bool> Wanted;
	Wanted.Init(false, Lights->GetInstanceCount());
	for (const FStopBar& Bar : StopBars)
	{
		for (int32 Index = 0; Index < Bar.NumLights; ++Index)
		{
			Wanted[Bar.FirstLight + Index] = true;
		}
	}
	for (auto It = Guidance.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
			continue;
		}
		for (int32 Index = It.Value().First; Index < It.Value().Last; ++Index)
		{
			Wanted[Index] = true;
		}
		for (int32 BarIndex = 0; BarIndex < StopBars.Num() && BarIndex < 64; ++BarIndex)
		{
			if (It.Value().BarsOff & (uint64(1) << BarIndex))
			{
				for (int32 Index = 0; Index < StopBars[BarIndex].NumLights; ++Index)
				{
					Wanted[StopBars[BarIndex].FirstLight + Index] = false;
				}
			}
		}
	}

	bool bChanged = false;
	for (int32 Index = 0; Index < Wanted.Num() && Index < LightOn.Num(); ++Index)
	{
		if (Wanted[Index] != LightOn[Index])
		{
			LightOn[Index] = Wanted[Index];
			SetLightOn(Index, Wanted[Index]);
			bChanged = true;
		}
	}
	if (bChanged)
	{
		Lights->MarkRenderStateDirty();
	}
}

void ATaxiRoute::BeginPlay()
{
	Super::BeginPlay();
	// Made of legs: follow them as they are now (a leg may have been changed after this route was built)
	if (Legs.Num() > 0)
	{
		BuildFromLegs();
		BuildCenterline();
	}
	ResolveStops();
	bLightsBuilt = false;
	if (!bLightsOnFirstUse)
	{
		BuildLights();
	}

	if (UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>())
	{
		Traffic->RegisterRoute(this);
	}

	// Debug drawing is for the editor only
	SetActorTickEnabled(false);
}

void ATaxiRoute::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UTrafficSubsystem>() : nullptr)
	{
		Traffic->UnregisterRoute(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ATaxiRoute::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if ENABLE_DRAW_DEBUG
	if (!bDrawDebug || !GetWorld() || GetWorld()->IsGameWorld())
	{
		return;
	}

	const FVector Up(0.f, 0.f, 150.f);
	for (const FTaxiStop& Stop : ResolvedStops)
	{
		const FVector Location = Spline->GetLocationAtDistanceAlongSpline(Stop.Distance, ESplineCoordinateSpace::World);
		DrawDebugSphere(GetWorld(), Location + Up, 120.f, 8, Stop.bRequiresClearance ? FColor::Red : FColor::Orange);
		DrawDebugString(GetWorld(), Location + Up * 2.f, Stop.Name.ToString(), nullptr, FColor::White, 0.f);
	}
	for (const FSpeedZone& Zone : SpeedZones)
	{
		for (float D = Zone.StartDistance; D < Zone.EndDistance; D += 500.f)
		{
			const FVector A = Spline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World) + Up * 0.3f;
			const FVector B = Spline->GetLocationAtDistanceAlongSpline(FMath::Min(D + 400.f, Zone.EndDistance), ESplineCoordinateSpace::World) + Up * 0.3f;
			DrawDebugLine(GetWorld(), A, B, FColor::Cyan, false, -1.f, 0, 30.f);
		}
	}
	for (const FTaxiWaypoint& Waypoint : Waypoints)
	{
		const FVector Location = Spline->GetLocationAtDistanceAlongSpline(Waypoint.Distance, ESplineCoordinateSpace::World);
		DrawDebugString(GetWorld(), Location + Up, Waypoint.Name.ToString(), nullptr, FColor::Yellow, 0.f);
	}
#endif
}
