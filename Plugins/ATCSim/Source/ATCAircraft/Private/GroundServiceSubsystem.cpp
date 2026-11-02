#include "GroundServiceSubsystem.h"
#include "AircraftIdentityComponent.h"
#include "AircraftMovementComponent.h"
#include "AircraftTypeData.h"
#include "ATCAircraft.h"
#include "ATCSimSettings.h"
#include "ATCUnits.h"
#include "PushbackControllerComponent.h"
#include "Stand.h"
#include "TaxiControllerComponent.h"
#include "TrafficSubsystem.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/**
	 * The sizes that place the vehicles, from the type (cm, aircraft frame: X to the nose, Y to the right wing, origin the
	 * main gear). The fuselage is about 11 % of the span wide, like the placeholder.
	 */
	struct FAircraftDims
	{
		float Nose = 2034.f;
		float Tail = -1916.f;
		float Length = 3950.f;
		float HalfSpan = 1790.f;
		float FuselageHalf = 197.f;
		/** The lane the vehicles drive along: outside the engines (between the inner and outer ones of a four-engine type). */
		float Lane = 1897.f;
	};

	/** Four engines under each wing pair: the lane goes between the inner and the outer engine. */
	bool HasFourEngines(const UAircraftTypeData* Type)
	{
		static const TCHAR* const Types[] = { TEXT("A388"), TEXT("A389"), TEXT("B741"), TEXT("B742"), TEXT("B743"), TEXT("B744"), TEXT("B748"),
			TEXT("A342"), TEXT("A343"), TEXT("A345"), TEXT("A346"), TEXT("A124"), TEXT("IL96") };
		const FString Code = Type ? Type->ICAOType.ToString() : FString();
		for (const TCHAR* FourEngined : Types)
		{
			if (Code == FourEngined)
			{
				return true;
			}
		}
		return Type && Type->WakeCategory == EWakeCategory::Super;
	}

	FAircraftDims DimsOf(const UAircraftTypeData* Type)
	{
		FAircraftDims Dims;
		Dims.Nose = Type ? Type->GetNoseOffsetCm() : 1560.f + 0.12f * 3950.f;
		Dims.Length = Type ? Type->GetLengthCm() : 3950.f;
		Dims.Tail = Dims.Nose - Dims.Length;
		Dims.HalfSpan = Type ? Type->GetHalfSpanCm() : 1790.f;
		Dims.FuselageHalf = FMath::Max(Dims.HalfSpan * 0.11f, 190.f);
		// Twin: outside the engine (a third of the half span) with room for the parked vehicles. Four engines: half way,
		// between the inner engine (about 0.37) and the outer one (about 0.64)
		Dims.Lane = HasFourEngines(Type) ? 0.5f * Dims.HalfSpan : FMath::Max(0.5f * Dims.HalfSpan, Dims.FuselageHalf + 1700.f);
		return Dims;
	}

	/** Left-side staging: behind the tail, where the fuel truck comes from and goes back to. */
	FVector LeftStaging(const FAircraftDims& Dims)
	{
		return FVector(Dims.Tail - 1500.f, -(Dims.Lane + 300.f), 0.f);
	}

	/** Seconds a vehicle stays before it leaves on its own (the baggage train waits for the pushback). */
	FVector2D DwellSeconds(EAirportVehicleKind Kind)
	{
		switch (Kind)
		{
		case EAirportVehicleKind::CateringTruck: return FVector2D(90.f, 180.f);
		case EAirportVehicleKind::FuelTruck:     return FVector2D(120.f, 240.f);
		default:                                 return FVector2D(1.e9f, 1.e9f);
		}
	}
}

void UGroundServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bEnabled = GetDefault<UATCSimSettings>()->bGroundVehicles;
}

bool UGroundServiceSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UGroundServiceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGroundServiceSubsystem, STATGROUP_Tickables);
}

float UGroundServiceSubsystem::Now() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

UMaterialInterface* UGroundServiceSubsystem::GetColorMaterial(const FLinearColor& Color)
{
	const uint32 Key = Color.ToFColor(false).ToPackedARGB();
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = ColorMaterials.Find(Key))
	{
		return Found->Get();
	}
	if (!ShapeMaterial)
	{
		ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!ShapeMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
	Material->SetVectorParameterValue(TEXT("Color"), Color);
	ColorMaterials.Add(Key, Material);
	return Material;
}

FVector UGroundServiceSubsystem::GetStagingPoint(const UAircraftTypeData* Type)
{
	const FAircraftDims Dims = DimsOf(Type);
	// On the lane itself: from here to the spots the way stays outside the engines
	return FVector(Dims.Nose - 100.f, Dims.Lane, 0.f);
}

TArray<FServiceSpot> UGroundServiceSubsystem::PlanSpots(const UAircraftTypeData* Type, bool bFuel, bool bCatering)
{
	const FAircraftDims Dims = DimsOf(Type);
	TArray<FServiceSpot> Spots;

	// Baggage train on the right, along the fuselage at the aft hold, facing the tail: the carts at the door
	FServiceSpot Baggage;
	Baggage.Kind = EAirportVehicleKind::BaggageTrain;
	Baggage.Location = FVector(0.5f * Dims.Tail - 355.f, Dims.FuselageHalf + 260.f, 0.f);
	Baggage.YawDeg = 180.f;
	Spots.Add(Baggage);

	// Fuel truck on the left, along the fuselage behind the main gear and the wing, facing the nose
	if (bFuel)
	{
		FServiceSpot Fuel;
		Fuel.Kind = EAirportVehicleKind::FuelTruck;
		Fuel.Location = FVector(-0.12f * Dims.Length - 450.f, -(Dims.FuselageHalf + 450.f), 0.f);
		Fuel.YawDeg = 0.f;
		Spots.Add(Fuel);
	}

	// Catering truck square to the fuselage at the front right door, its box at the door: ahead of the engines, and out of
	// the way of the baggage train (the jet bridge is on the left)
	if (bCatering)
	{
		FServiceSpot Catering;
		Catering.Kind = EAirportVehicleKind::CateringTruck;
		Catering.Location = FVector(Dims.Nose - 500.f, Dims.FuselageHalf + 520.f, 0.f);
		Catering.YawDeg = -90.f;
		Catering.LiftCm = FMath::Clamp(Dims.FuselageHalf * 1.1f, 150.f, 400.f);
		Spots.Add(Catering);
	}
	return Spots;
}

TArray<FVector> UGroundServiceSubsystem::PathIn(const UAircraftTypeData* Type, const FServiceSpot& Spot)
{
	const FAircraftDims Dims = DimsOf(Type);
	const FVector& P = Spot.Location;
	switch (Spot.Kind)
	{
	case EAirportVehicleKind::FuelTruck:
		// From behind the tail along the left lane, in to the fuselage behind its spot
		return { LeftStaging(Dims), FVector(P.X - 1600.f, -Dims.Lane, 0.f), FVector(P.X - 600.f, P.Y, 0.f), P };
	case EAirportVehicleKind::CateringTruck:
		// Along the right lane, then straight in to the door
		return { GetStagingPoint(Type), FVector(P.X, Dims.Lane, 0.f), FVector(P.X, P.Y + 400.f, 0.f), P };
	default:
		// Along the right lane, in to the fuselage in front of its spot (behind the main gear and the engines), then along
		// the fuselage towards the tail
		return { GetStagingPoint(Type), FVector(P.X + 1000.f, Dims.Lane, 0.f), FVector(P.X + 300.f, P.Y, 0.f), P };
	}
}

TArray<FVector> UGroundServiceSubsystem::PathOut(const UAircraftTypeData* Type, const FServiceSpot& Spot)
{
	const FAircraftDims Dims = DimsOf(Type);
	const FVector& P = Spot.Location;
	const FVector Staging = GetStagingPoint(Type);
	switch (Spot.Kind)
	{
	case EAirportVehicleKind::FuelTruck:
		// Backs away from under the wing along the fuselage (the engine is just in front of it), then out to the lane and
		// back behind the tail
		return { FVector(P.X - 1500.f, P.Y, 0.f), FVector(P.X - 900.f, -Dims.Lane, 0.f), FVector(P.X - 1500.f, -(Dims.Lane + 300.f), 0.f),
			LeftStaging(Dims) };
	case EAirportVehicleKind::CateringTruck:
		// Backs away from the door to the lane (the first point), then on to the staging point beside the nose
		return { FVector(P.X, Dims.Lane, 0.f), FVector(P.X + 600.f, Dims.Lane, 0.f), Staging };
	default:
		// On towards the tail, out to the lane, round behind the wing and back along the lane
		return { FVector(P.X - 700.f, P.Y + 400.f, 0.f), FVector(P.X - 1100.f, Dims.Lane, 0.f), FVector(P.X - 300.f, Dims.Lane, 0.f),
			FVector(P.X + 1500.f, Dims.Lane, 0.f), Staging };
	}
}

bool UGroundServiceSubsystem::IsParkedAtStand(const AATCAircraft* Aircraft)
{
	if (!Aircraft || !Aircraft->Identity || !Aircraft->PushbackController || !Aircraft->TaxiController || !Aircraft->Movement)
	{
		return false;
	}
	const AStand* Stand = Aircraft->Identity->Stand;
	if (!Stand || Aircraft->IsLanding() || Aircraft->IsTakingOff() || Aircraft->PushbackController->GetState() != EPushbackState::Parked)
	{
		return false;
	}
	const ETaxiState Taxi = Aircraft->TaxiController->GetState();
	if ((Taxi != ETaxiState::Idle && Taxi != ETaxiState::Arrived) || Aircraft->Movement->Speed > 10.f)
	{
		return false;
	}
	// On the stand itself: the director gives an arrival its stand already when it lands
	return FVector::DistSquared2D(Aircraft->GetActorLocation(), Stand->GetParkingTransform().GetLocation()) < FMath::Square(800.f);
}

void UGroundServiceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	PollTimer -= DeltaTime;
	if (PollTimer <= 0.f)
	{
		PollTimer = 1.f;
		Poll();
	}
}

void UGroundServiceSubsystem::Poll()
{
	if (FirstPollTime < 0.f)
	{
		FirstPollTime = Now();
	}
	if (!bEnabled)
	{
		return;
	}
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	const float Time = Now();

	// Aircraft that are leaving (pushback asked for) or gone: their vehicles drive away
	for (FService& Service : Services)
	{
		AATCAircraft* Aircraft = Service.Aircraft.Get();
		const bool bStay = Aircraft && IsParkedAtStand(Aircraft);
		for (FServiceVehicle& Entry : Service.Vehicles)
		{
			// Arrived at its spot: at work (the catering box goes up to the door)
			AAirportVehicle* Vehicle = Entry.Vehicle.Get();
			if (Entry.Phase == EPhase::Coming && Vehicle && !Vehicle->IsDriving())
			{
				Entry.Phase = EPhase::Parked;
				Vehicle->SetLift(Entry.Spot.LiftCm);
			}
			const bool bDone = (Entry.Phase == EPhase::Coming || Entry.Phase == EPhase::Parked) && Time >= Entry.LeaveAt;
			if (!bStay && !Service.bLeaving)
			{
				// The pushback goes first: the catering and fuel trucks drive off at once, the baggage train a moment later
				if (Entry.Phase == EPhase::Waiting)
				{
					Entry.Phase = EPhase::Done;
				}
				else if (Entry.Phase == EPhase::Coming || Entry.Phase == EPhase::Parked)
				{
					Entry.LeaveAt = Entry.Spot.Kind == EAirportVehicleKind::BaggageTrain ? Time + 4.f : Time;
				}
			}
			else if (bDone)
			{
				SendAway(Service, Entry);
			}
		}
		Service.bLeaving |= !bStay;
	}
	Services.RemoveAll([](const FService& Service)
	{
		if (!Service.bLeaving)
		{
			return false;
		}
		for (const FServiceVehicle& Entry : Service.Vehicles)
		{
			if (Entry.Phase != EPhase::Done && Entry.Vehicle.IsValid())
			{
				return false;
			}
			if (Entry.Phase != EPhase::Done && Entry.Phase != EPhase::Leaving)
			{
				return false;
			}
		}
		return true;
	});

	// Aircraft that just parked
	if (const UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>())
	{
		for (AActor* Actor : Traffic->GetAllAircraft())
		{
			AATCAircraft* Aircraft = Cast<AATCAircraft>(Actor);
			if (!Aircraft || !IsParkedAtStand(Aircraft) || Services.ContainsByPredicate([Aircraft](const FService& Service) { return Service.Aircraft == Aircraft; }))
			{
				continue;
			}
			// Parked when the map started (or a scenario put it there): its handling is under way already
			StartService(Aircraft, Time - FirstPollTime < 5.f);
		}
	}

	// Vehicles whose time has come, within the budget
	int32 Count = GetVehicleCount();
	for (FService& Service : Services)
	{
		for (FServiceVehicle& Entry : Service.Vehicles)
		{
			if (Service.bLeaving || Entry.Phase != EPhase::Waiting || Time < Entry.ComeAt || Count >= Settings->MaxServiceVehicles)
			{
				continue;
			}
			Send(Service, Entry, false);
			Count += Entry.Vehicle.IsValid() ? 1 : 0;
		}
	}
}

void UGroundServiceSubsystem::StartService(AATCAircraft* Aircraft, bool bAtStart)
{
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	FService& Service = Services.AddDefaulted_GetRef();
	Service.Aircraft = Aircraft;
	Service.Type = Aircraft->AircraftType.Get();
	Service.Frame = FTransform(FRotator(0.f, Aircraft->GetActorRotation().Yaw, 0.f), Aircraft->GetActorLocation());
	// Not every aircraft that was parked at the start has its handling going on: the rest stands quiet
	if (bAtStart && Random.FRand() > Settings->ServicedAtStartShare)
	{
		return;
	}

	const float Time = Now();
	const TArray<FServiceSpot> Spots = PlanSpots(Aircraft->AircraftType, Random.FRand() < 0.8f, Random.FRand() < 0.65f);
	int32 Count = GetVehicleCount();
	for (const FServiceSpot& Spot : Spots)
	{
		FServiceVehicle& Entry = Service.Vehicles.AddDefaulted_GetRef();
		Entry.Spot = Spot;
		const FVector2D Dwell = DwellSeconds(Spot.Kind);
		if (bAtStart)
		{
			// Already at work, some of it nearly done
			Entry.ComeAt = Time;
			Entry.LeaveAt = Time + Random.FRandRange(0.f, Dwell.Y);
			if (Count < Settings->MaxServiceVehicles)
			{
				Send(Service, Entry, true);
				Count += Entry.Vehicle.IsValid() ? 1 : 0;
			}
			else
			{
				Entry.Phase = EPhase::Done;
			}
		}
		else
		{
			// The baggage first, then the others, a little apart
			Entry.ComeAt = Time + (Spot.Kind == EAirportVehicleKind::BaggageTrain ? Random.FRandRange(5.f, 15.f) : Random.FRandRange(15.f, 60.f));
			Entry.LeaveAt = Entry.ComeAt + Random.FRandRange(Dwell.X, Dwell.Y);
		}
	}
}

void UGroundServiceSubsystem::Send(FService& Service, FServiceVehicle& Entry, bool bPlaceAtOnce)
{
	UWorld* World = GetWorld();
	const UAircraftTypeData* Type = Service.Type.Get();
	const TArray<FVector> Local = PathIn(Type, Entry.Spot);
	const float FrameYaw = Service.Frame.Rotator().Yaw;
	const float EndYaw = FrameYaw + Entry.Spot.YawDeg;
	// Placed: on its spot. Driving in: at the staging point, facing its first point
	const FVector Start = Service.Frame.TransformPosition(bPlaceAtOnce ? Entry.Spot.Location : Local[0]);
	const float StartYaw = bPlaceAtOnce ? EndYaw : (Service.Frame.TransformPosition(Local[1]) - Start).Rotation().Yaw;
	const FTransform Transform(FRotator(0.f, StartYaw, 0.f), Start);
	AAirportVehicle* Vehicle = World ? World->SpawnActorDeferred<AAirportVehicle>(AAirportVehicle::StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
	if (!Vehicle)
	{
		Entry.Phase = EPhase::Done;
		return;
	}
	Vehicle->Kind = Entry.Spot.Kind;
	Vehicle->FinishSpawning(Transform);
	Entry.Vehicle = Vehicle;

	if (bPlaceAtOnce)
	{
		Vehicle->Place(Start, EndYaw);
		Vehicle->SetLift(Entry.Spot.LiftCm);
		Entry.Phase = EPhase::Parked;
		return;
	}
	TArray<FVector> Points;
	for (int32 Index = 1; Index < Local.Num(); ++Index)
	{
		Points.Add(Service.Frame.TransformPosition(Local[Index]));
	}
	Vehicle->DriveThrough(Points, EndYaw, false);
	Entry.Phase = EPhase::Coming;
}

void UGroundServiceSubsystem::SendAway(FService& Service, FServiceVehicle& Entry)
{
	AAirportVehicle* Vehicle = Entry.Vehicle.Get();
	if (!Vehicle || Entry.Phase == EPhase::Leaving || Entry.Phase == EPhase::Done)
	{
		Entry.Phase = Vehicle ? Entry.Phase : EPhase::Done;
		return;
	}
	const TArray<FVector> Local = PathOut(Service.Type.Get(), Entry.Spot);
	TArray<FVector> Points;
	for (const FVector& Point : Local)
	{
		Points.Add(Service.Frame.TransformPosition(Point));
	}
	const float EndYaw = Points.Num() >= 2 ? (Points.Last() - Points[Points.Num() - 2]).Rotation().Yaw : Vehicle->GetActorRotation().Yaw;
	// The trucks back away from the door or from under the wing first; the baggage train drives on
	Vehicle->DriveThrough(Points, EndYaw, true, Entry.Spot.Kind == EAirportVehicleKind::BaggageTrain ? 0 : 1);
	Entry.Phase = EPhase::Leaving;
}

void UGroundServiceSubsystem::SetEnabled(bool bEnable)
{
	if (bEnable == bEnabled)
	{
		return;
	}
	bEnabled = bEnable;
	if (!bEnabled)
	{
		RemoveAll();
	}
	UE_LOG(LogATC, Display, TEXT("Ground vehicles %s"), bEnabled ? TEXT("on") : TEXT("off"));
}

void UGroundServiceSubsystem::RemoveAll()
{
	for (FService& Service : Services)
	{
		for (FServiceVehicle& Entry : Service.Vehicles)
		{
			if (AAirportVehicle* Vehicle = Entry.Vehicle.Get())
			{
				Vehicle->Destroy();
			}
		}
	}
	Services.Reset();
}

int32 UGroundServiceSubsystem::GetVehicleCount() const
{
	int32 Count = 0;
	for (const FService& Service : Services)
	{
		for (const FServiceVehicle& Entry : Service.Vehicles)
		{
			Count += Entry.Vehicle.IsValid() ? 1 : 0;
		}
	}
	return Count;
}

FString UGroundServiceSubsystem::Describe() const
{
	FString Text = FString::Printf(TEXT("ground vehicles %s, %d service vehicles at %d aircraft (at most %d)\n"), bEnabled ? TEXT("on") : TEXT("off"),
		GetVehicleCount(), Services.Num(), GetDefault<UATCSimSettings>()->MaxServiceVehicles);
	for (const FService& Service : Services)
	{
		const AATCAircraft* Aircraft = Service.Aircraft.Get();
		FString Line = FString::Printf(TEXT("%s%s:"), Aircraft ? *IAircraftInfo::Execute_GetCallsign(Aircraft).ToString() : TEXT("(gone)"),
			Service.bLeaving ? TEXT(" (leaving)") : TEXT(""));
		for (const FServiceVehicle& Entry : Service.Vehicles)
		{
			static const TCHAR* const Phases[] = { TEXT("waiting"), TEXT("coming"), TEXT("parked"), TEXT("leaving"), TEXT("done") };
			Line += FString::Printf(TEXT(" %s %s"), *StaticEnum<EAirportVehicleKind>()->GetNameStringByValue(static_cast<int64>(Entry.Spot.Kind)),
				Phases[static_cast<int32>(Entry.Phase)]);
		}
		Text += Line + TEXT("\n");
	}
	return Text;
}
