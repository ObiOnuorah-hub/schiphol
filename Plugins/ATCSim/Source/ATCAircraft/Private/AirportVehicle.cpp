#include "AirportVehicle.h"
#include "GroundServiceSubsystem.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	TAutoConsoleVariable<int32> CVarVehicleBeacons(TEXT("atc.VehicleBeacons"), 1,
		TEXT("1 = airport vehicles flash an amber beacon while they drive (a small light each), 0 = no beacons"));

	// Linear colours for the basic shape material
	const FLinearColor Chassis(0.03f, 0.03f, 0.035f);
	const FLinearColor Grey(0.32f, 0.33f, 0.35f);
	const FLinearColor White(0.82f, 0.83f, 0.85f);
	const FLinearColor Silver(0.58f, 0.6f, 0.63f);
	const FLinearColor TugYellow(0.92f, 0.6f, 0.02f);
	const FLinearColor HandlerBlue(0.f, 0.3f, 0.66f);
	const FLinearColor Amber(1.f, 0.42f, 0.f);

	constexpr int32 NumCarts = 2;
	constexpr float CartLengthCm = 330.f;
	constexpr float CartGapCm = 60.f;
	/** Tow bar: from the hitch to the first cart. */
	constexpr float TowBarCm = 40.f;
	/** The tractor's hitch behind its origin. */
	constexpr float HitchBackCm = 150.f;
	/** The carts follow points of the hitch's track this far apart. */
	constexpr float TrailStepCm = 25.f;
	constexpr int32 MaxTrailPoints = 80;
	/** Beyond this the vehicles are not drawn: the far side of a large airport from the tower. */
	constexpr float CullDistanceCm = 400000.f;
}

AAirportVehicle::AAirportVehicle()
{
	// Only while it drives or lifts its box
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// Spawned at runtime and moving around: never streamed out by World Partition (an editor-only setting)
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
	ShapeMaterial = Material.Object;
}

APushbackTug::APushbackTug()
{
	Kind = EAirportVehicleKind::PushbackTug;
	MaxSpeedKmh = 20.f;
}

void AAirportVehicle::BeginPlay()
{
	Super::BeginPlay();
	if (!bBuilt)
	{
		Build();
	}
}

void AAirportVehicle::SetKind(EAirportVehicleKind NewKind)
{
	Kind = NewKind;
	if (bBuilt)
	{
		Build();
	}
}

float AAirportVehicle::GetLengthCm() const
{
	switch (Kind)
	{
	case EAirportVehicleKind::PushbackTug:   return 760.f;
	case EAirportVehicleKind::FuelTruck:     return 900.f;
	case EAirportVehicleKind::CateringTruck: return 800.f;
	default:                                 return 300.f + TowBarCm + NumCarts * (CartLengthCm + CartGapCm);
	}
}

UMaterialInterface* AAirportVehicle::GetMaterial(const FLinearColor& Color)
{
	// One material per colour for the whole airport: identical parts can be drawn together
	if (UGroundServiceSubsystem* Service = GetWorld() ? GetWorld()->GetSubsystem<UGroundServiceSubsystem>() : nullptr)
	{
		if (UMaterialInterface* Shared = Service->GetColorMaterial(Color))
		{
			return Shared;
		}
	}
	if (!ShapeMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Own = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
	Own->SetVectorParameterValue(TEXT("Color"), Color);
	return Own;
}

UStaticMeshComponent* AAirportVehicle::AddPart(UStaticMesh* Mesh, USceneComponent* Parent, const FVector& Center, const FVector& SizeCm,
	const FLinearColor& Color, bool bShadow, const FRotator& Rotation)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetupAttachment(Parent);
	Part->SetStaticMesh(Mesh);
	Part->SetMaterial(0, GetMaterial(Color));
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	// Only the big part of each vehicle casts a shadow: enough to stand on the ground, at a fraction of the cost
	Part->SetCastShadow(bShadow);
	Part->SetCullDistance(CullDistanceCm);
	// The basic shapes are 100 cm big
	Part->SetRelativeTransform(FTransform(Rotation, Center, SizeCm / 100.f));
	Part->RegisterComponent();
	Body.Add(Part);
	return Part;
}

void AAirportVehicle::ClearBody()
{
	for (USceneComponent* Component : Body)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	Body.Reset();
	Carts.Reset();
	LiftBox = nullptr;
	Beacon = nullptr;
	LiftHeight = LiftTarget = 0.f;
}

void AAirportVehicle::Build()
{
	ClearBody();
	UStaticMesh* Cube = CubeMesh;
	UStaticMesh* Cylinder = CylinderMesh ? CylinderMesh.Get() : CubeMesh.Get();
	if (!Cube)
	{
		return;
	}

	// Origin on the ground in the middle of the vehicle, +X forward; sizes in cm
	FVector BeaconAt = FVector::ZeroVector;
	switch (Kind)
	{
	case EAirportVehicleKind::PushbackTug:
		// Long and flat: the nose wheel sits in its cradle at the origin, the low cab is at the front, past the nose
		AddPart(Cube, Root, FVector(150.f, 0.f, 35.f), FVector(760.f, 330.f, 50.f), Chassis, false);
		AddPart(Cube, Root, FVector(120.f, 0.f, 80.f), FVector(690.f, 320.f, 45.f), TugYellow, true);
		AddPart(Cube, Root, FVector(560.f, 0.f, 140.f), FVector(110.f, 300.f, 90.f), TugYellow, false);
		BeaconAt = FVector(560.f, 0.f, 195.f);
		break;

	case EAirportVehicleKind::BaggageTrain:
	{
		AddPart(Cube, Root, FVector(0.f, 0.f, 40.f), FVector(300.f, 150.f, 55.f), Chassis, false);
		AddPart(Cube, Root, FVector(40.f, 0.f, 90.f), FVector(220.f, 145.f, 55.f), White, true);
		AddPart(Cube, Root, FVector(-60.f, 0.f, 185.f), FVector(120.f, 145.f, 8.f), Chassis, false);
		for (int32 Index = 0; Index < NumCarts; ++Index)
		{
			USceneComponent* Cart = NewObject<USceneComponent>(this);
			Cart->SetMobility(EComponentMobility::Movable);
			Cart->SetupAttachment(Root);
			Cart->RegisterComponent();
			Body.Add(Cart);
			Carts.Add(Cart);
			AddPart(Cube, Cart, FVector(0.f, 0.f, 40.f), FVector(CartLengthCm, 160.f, 35.f), Chassis, false);
			AddPart(Cube, Cart, FVector(0.f, 0.f, 115.f), FVector(CartLengthCm - 20.f, 155.f, 115.f), Index % 2 == 0 ? HandlerBlue : Grey, true);
		}
		BeaconAt = FVector(-60.f, 0.f, 197.f);
		break;
	}

	case EAirportVehicleKind::FuelTruck:
		AddPart(Cube, Root, FVector(0.f, 0.f, 50.f), FVector(900.f, 240.f, 60.f), Chassis, false);
		AddPart(Cube, Root, FVector(330.f, 0.f, 165.f), FVector(230.f, 245.f, 210.f), White, true);
		// The tank: a cylinder lying along the truck
		AddPart(Cylinder, Root, FVector(-110.f, 0.f, 190.f), FVector(230.f, 230.f, 620.f), Silver, true, FRotator(90.f, 0.f, 0.f));
		BeaconAt = FVector(330.f, 0.f, 280.f);
		break;

	case EAirportVehicleKind::CateringTruck:
	{
		AddPart(Cube, Root, FVector(0.f, 0.f, 50.f), FVector(800.f, 240.f, 60.f), Chassis, false);
		AddPart(Cube, Root, FVector(300.f, 0.f, 160.f), FVector(200.f, 245.f, 200.f), White, true);
		AddPart(Cube, Root, FVector(-100.f, 0.f, 95.f), FVector(460.f, 220.f, 30.f), Grey, false);
		USceneComponent* Lift = NewObject<USceneComponent>(this);
		Lift->SetMobility(EComponentMobility::Movable);
		Lift->SetupAttachment(Root);
		Lift->SetRelativeLocation(FVector(-100.f, 0.f, 110.f));
		Lift->RegisterComponent();
		Body.Add(Lift);
		LiftBox = Lift;
		LiftBaseZ = Lift->GetRelativeLocation().Z;
		AddPart(Cube, Lift, FVector(0.f, 0.f, 130.f), FVector(480.f, 240.f, 250.f), White, true);
		AddPart(Cube, Lift, FVector(0.f, 0.f, 215.f), FVector(482.f, 242.f, 30.f), HandlerBlue, false);
		BeaconAt = FVector(300.f, 0.f, 270.f);
		break;
	}
	}

	// The beacon: an amber block, and a light that pulses while it drives
	AddPart(Cube, Root, BeaconAt, FVector(25.f, 25.f, 15.f), Amber, false);
	if (bBeacon)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(Root);
		Light->SetRelativeLocation(BeaconAt + FVector(0.f, 0.f, 25.f));
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(0.f);
		Light->SetAttenuationRadius(700.f);
		Light->SetLightColor(FLinearColor(1.f, 0.5f, 0.05f));
		Light->SetCastShadows(false);
		Light->SetVisibility(false);
		Light->RegisterComponent();
		Body.Add(Light);
		Beacon = Light;
	}
	bBuilt = true;
	UpdateCarts(true);
}

void AAirportVehicle::Place(const FVector& Location, float YawDeg)
{
	SetActorLocationAndRotation(Location, FRotator(0.f, YawDeg, 0.f));
	Route.Reset();
	bDriving = false;
	bTurningOnSpot = false;
	Speed = 0.f;
	UpdateCarts(true);
	UpdateTickEnabled();
}

void AAirportVehicle::DriveThrough(const TArray<FVector>& Points, float EndYawDeg, bool bInRemoveAtEnd, int32 ReversePoints)
{
	Route = Points;
	NextPoint = 0;
	ReverseUntil = FMath::Clamp(ReversePoints, 0, Points.Num());
	EndYaw = EndYawDeg;
	bRemoveAtEnd = bInRemoveAtEnd;
	bTurningOnSpot = false;
	bDriving = Route.Num() > 0;
	// A box that is up comes down before it drives off
	LiftTarget = 0.f;
	if (!bDriving && bRemoveAtEnd)
	{
		Destroy();
		return;
	}
	UpdateTickEnabled();
}

void AAirportVehicle::SetLift(float HeightCm)
{
	LiftTarget = LiftBox ? FMath::Max(HeightCm, 0.f) : 0.f;
	UpdateTickEnabled();
}

void AAirportVehicle::UpdateTickEnabled()
{
	const bool bLifting = LiftBox && FMath::Abs(LiftHeight - LiftTarget) > 0.5f;
	SetActorTickEnabled(bDriving || bLifting);
}

void AAirportVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (LiftBox && FMath::Abs(LiftHeight - LiftTarget) > 0.5f)
	{
		// Scissor lift: half a metre a second
		LiftHeight = FMath::FInterpConstantTo(LiftHeight, LiftTarget, DeltaSeconds, 50.f);
		const FVector Relative = LiftBox->GetRelativeLocation();
		LiftBox->SetRelativeLocation(FVector(Relative.X, Relative.Y, LiftBaseZ + LiftHeight));
	}
	// Drives off once the box is down
	if (bDriving && LiftHeight <= 1.f)
	{
		TickDrive(DeltaSeconds);
		// Removed at the end of its drive
		if (!IsValid(this))
		{
			return;
		}
	}

	if (Beacon)
	{
		const bool bOn = bBeacon && bDriving && CVarVehicleBeacons.GetValueOnGameThread() > 0;
		Beacon->SetVisibility(bOn);
		if (bOn)
		{
			// A rotating beacon: a short flash about one and a half times a second
			BeaconPhase = FMath::Fmod(BeaconPhase + DeltaSeconds * 1.5f, 1.f);
			Beacon->SetIntensity(80.f * FMath::Pow(FMath::Max(FMath::Sin(BeaconPhase * 2.f * PI), 0.f), 4.f));
		}
	}
	UpdateTickEnabled();
}

void AAirportVehicle::TickDrive(float DeltaSeconds)
{
	FVector Location = GetActorLocation();
	float Yaw = GetActorRotation().Yaw;

	// The last centimetres: on the spot, then turn to the end heading
	if (bTurningOnSpot)
	{
		const float Error = FMath::FindDeltaAngleDegrees(Yaw, EndYaw);
		if (FMath::Abs(Error) < 0.5f)
		{
			SetActorRotation(FRotator(0.f, EndYaw, 0.f));
			FinishDrive();
			return;
		}
		Yaw += FMath::Clamp(Error, -30.f * DeltaSeconds, 30.f * DeltaSeconds);
		SetActorRotation(FRotator(0.f, Yaw, 0.f));
		UpdateCarts(false);
		return;
	}

	// Passed a point: on to the next one. Backing up it goes all the way to the point, so the next one starts there
	FVector Target = Route[NextPoint];
	while (NextPoint < Route.Num() - 1 && FVector::Dist2D(Location, Target) < (NextPoint < ReverseUntil ? 30.f : 350.f))
	{
		if (NextPoint < ReverseUntil)
		{
			Speed = 0.f;
		}
		Target = Route[++NextPoint];
	}
	const bool bLast = NextPoint == Route.Num() - 1;
	const bool bReverse = NextPoint < ReverseUntil;
	const FVector2D To(Target.X - Location.X, Target.Y - Location.Y);
	const float Distance = To.Size();
	if (bLast && Distance < 15.f)
	{
		SetActorLocation(FVector(Target.X, Target.Y, Location.Z));
		Speed = 0.f;
		// Gone at the end: no need to line up first
		if (bRemoveAtEnd)
		{
			FinishDrive();
			return;
		}
		bTurningOnSpot = true;
		return;
	}

	// Slow in a sharp turn and towards the end point; backing up the rear points the way
	const float WantYaw = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
	const float Error = FMath::FindDeltaAngleDegrees(bReverse ? Yaw + 180.f : Yaw, WantYaw);
	float WantSpeed = (bReverse ? 7.f : MaxSpeedKmh) / 3.6f * 100.f * FMath::Clamp(1.f - FMath::Abs(Error) / 100.f, 0.2f, 1.f);
	if (bLast || bReverse)
	{
		WantSpeed = FMath::Min(WantSpeed, FMath::Sqrt(2.f * 200.f * Distance) + 15.f);
	}
	Speed = FMath::FInterpConstantTo(Speed, WantSpeed, DeltaSeconds, WantSpeed > Speed ? 150.f : 300.f);

	if (bLast && Distance < 60.f)
	{
		// Close enough to roll straight onto the spot, whatever the heading
		const FVector2D Step = To / Distance * FMath::Min(Speed * DeltaSeconds, Distance);
		Location += FVector(Step.X, Step.Y, 0.f);
	}
	else
	{
		// Steer like a car: it only turns while it rolls
		const float MaxTurn = TurnRateDegS * FMath::Clamp(Speed / 100.f, 0.f, 1.f) * DeltaSeconds;
		Yaw += FMath::Clamp(Error, -MaxTurn, MaxTurn);
		Location += FRotator(0.f, bReverse ? Yaw + 180.f : Yaw, 0.f).Vector() * Speed * DeltaSeconds;
	}
	SetActorLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f));

	// The track of the hitch, for the carts
	const FVector Hitch = GetHitch();
	if (Trail.Num() == 0 || FVector::DistSquared2D(Trail.Last(), Hitch) > TrailStepCm * TrailStepCm)
	{
		Trail.Add(Hitch);
		if (Trail.Num() > MaxTrailPoints)
		{
			Trail.RemoveAt(0, Trail.Num() - MaxTrailPoints);
		}
	}
	UpdateCarts(false);
}

void AAirportVehicle::FinishDrive()
{
	bDriving = false;
	bTurningOnSpot = false;
	Speed = 0.f;
	Route.Reset();
	if (Beacon)
	{
		Beacon->SetVisibility(false);
	}
	if (bRemoveAtEnd)
	{
		Destroy();
		return;
	}
	UpdateTickEnabled();
}

FVector AAirportVehicle::GetHitch() const
{
	return GetActorLocation() - GetActorForwardVector() * HitchBackCm;
}

FVector AAirportVehicle::PointBehindHitch(float BackCm) const
{
	// Back along the track: from the hitch now, through the points it passed, newest first
	FVector Previous = GetHitch();
	float Left = BackCm;
	for (int32 Index = Trail.Num() - 1; Index >= 0; --Index)
	{
		const FVector Point = Trail[Index];
		const float Segment = FVector::Dist2D(Previous, Point);
		if (Segment >= Left && Segment > KINDA_SMALL_NUMBER)
		{
			return Previous + (Point - Previous) * (Left / Segment);
		}
		Left -= Segment;
		Previous = Point;
	}
	// Past the oldest point: straight on behind it
	const FVector Direction = Trail.Num() >= 2 ? (Trail[0] - Trail[1]).GetSafeNormal2D() : -GetActorForwardVector();
	return Previous + Direction * FMath::Max(Left, 0.f);
}

void AAirportVehicle::UpdateCarts(bool bStraight)
{
	if (Carts.Num() == 0)
	{
		return;
	}
	if (bStraight)
	{
		// Straight behind the tractor, oldest (furthest) point first
		Trail.Reset();
		const FVector Hitch = GetHitch();
		const FVector Back = -GetActorForwardVector();
		for (int32 Step = 40; Step >= 1; --Step)
		{
			Trail.Add(Hitch + Back * (Step * 30.f));
		}
	}
	const float Ground = GetActorLocation().Z;
	for (int32 Index = 0; Index < Carts.Num(); ++Index)
	{
		const float Middle = TowBarCm + CartLengthCm * 0.5f + Index * (CartLengthCm + CartGapCm);
		const FVector Front = PointBehindHitch(Middle - CartLengthCm * 0.5f);
		const FVector Rear = PointBehindHitch(Middle + CartLengthCm * 0.5f);
		const FVector Center = (Front + Rear) * 0.5f;
		const FVector Along = Front - Rear;
		const float CartYaw = Along.SizeSquared2D() > 1.f ? FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)) : GetActorRotation().Yaw;
		Carts[Index]->SetWorldLocationAndRotation(FVector(Center.X, Center.Y, Ground), FRotator(0.f, CartYaw, 0.f));
	}
}
