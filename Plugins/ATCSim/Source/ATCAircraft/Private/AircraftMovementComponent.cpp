#include "AircraftMovementComponent.h"
#include "AircraftTypeData.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"

UAircraftMovementComponent::UAircraftMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UAircraftMovementComponent::ApplyAircraftType(const UAircraftTypeData* Type)
{
	if (Type)
	{
		AccelerationCms2 = Type->GetAccelerationCms2();
		DecelerationCms2 = Type->GetDecelerationCms2();
		WheelbaseCm = Type->GetWheelbaseCm();
	}
}

float UAircraftMovementComponent::GetDistanceClosestTo(const USplineComponent* Spline, const FVector& WorldLocation)
{
	if (!Spline)
	{
		return 0.f;
	}
	const float Key = Spline->FindInputKeyClosestToWorldLocation(WorldLocation);
	return Spline->GetDistanceAlongSplineAtSplineInputKey(Key);
}

void UAircraftMovementComponent::SetPath(USplineComponent* NewPath, ETravelDirection NewDirection, bool bInNoseAlongTravel, float StartDistance, bool bSnap)
{
	Path = NewPath;
	Direction = NewDirection;
	bNoseAlongTravel = bInNoseAlongTravel;
	bEndReported = false;
	CurrentRotation = GetOwner()->GetActorQuat();

	if (Path)
	{
		Distance = FMath::Clamp(StartDistance, 0.f, Path->GetSplineLength());
		UpdateTransform(0.f, bSnap);
	}
}

void UAircraftMovementComponent::ClearPath()
{
	Path = nullptr;
	StopImmediately();
}

float UAircraftMovementComponent::GetRemainingDistance() const
{
	if (!Path)
	{
		return 0.f;
	}
	return Direction == ETravelDirection::Forward ? Path->GetSplineLength() - Distance : Distance;
}

void UAircraftMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Path)
	{
		return;
	}

	const float Rate = TargetSpeed > Speed ? AccelerationCms2 : DecelerationCms2;
	Speed = FMath::FInterpConstantTo(Speed, TargetSpeed, DeltaTime, Rate);

	const float Length = Path->GetSplineLength();
	const float Sign = Direction == ETravelDirection::Forward ? 1.f : -1.f;
	Distance += Sign * Speed * DeltaTime;

	const bool bAtEnd = Direction == ETravelDirection::Forward ? Distance >= Length : Distance <= 0.f;
	if (bAtEnd)
	{
		Distance = FMath::Clamp(Distance, 0.f, Length);
		StopImmediately();
	}

	UpdateTransform(DeltaTime, false);

	if (bAtEnd && !bEndReported)
	{
		bEndReported = true;
		OnPathEndReached.Broadcast();
	}
}

FVector UAircraftMovementComponent::GetPointAt(float AtDistance) const
{
	// Outside the spline: continue in a straight line, so the nose gear has a place at the very start and end
	const float Length = Path->GetSplineLength();
	if (AtDistance < 0.f)
	{
		return Path->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World)
			+ Path->GetDirectionAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World) * AtDistance;
	}
	if (AtDistance > Length)
	{
		return Path->GetLocationAtDistanceAlongSpline(Length, ESplineCoordinateSpace::World)
			+ Path->GetDirectionAtDistanceAlongSpline(Length, ESplineCoordinateSpace::World) * (AtDistance - Length);
	}
	return Path->GetLocationAtDistanceAlongSpline(AtDistance, ESplineCoordinateSpace::World);
}

void UAircraftMovementComponent::UpdateTransform(float DeltaTime, bool bSnap)
{
	// Main gear on the spline, nose gear on the spline one wheelbase further: the heading follows from the two
	const bool bNoseAhead = (Direction == ETravelDirection::Forward) == bNoseAlongTravel;
	const FVector MainGear = GetPointAt(Distance);
	const FVector NoseGear = GetPointAt(Distance + (bNoseAhead ? WheelbaseCm : -WheelbaseCm));
	FVector Heading = (NoseGear - MainGear).GetSafeNormal();
	if (Heading.IsNearlyZero())
	{
		return;
	}
	// Nose first: oversteer, the heading partly along the line at the main gear, so the nose goes wide in a tight turn and
	// the main gear stays on the line (user 18-11-2026: like a bus, as real aircraft taxi)
	if (bNoseAlongTravel && OversteerShare > 0.f)
	{
		// Only in a really tight turn (user: not everywhere): none above a 60 m radius, all of it from 25 m, the radius from
		// the change of direction over the 20 m around the main gear
		const float Ahead = bNoseAhead ? 1.f : -1.f;
		const FVector Here = GetPointAt(Distance);
		const FVector In = (Here - GetPointAt(Distance - Ahead * 1000.f)).GetSafeNormal2D();
		const FVector Out = (GetPointAt(Distance + Ahead * 1000.f) - Here).GetSafeNormal2D();
		const float Turn = FMath::Acos(FMath::Clamp(float(In | Out), -1.f, 1.f));
		const float RadiusCm = Turn > KINDA_SMALL_NUMBER ? 1000.f / Turn : BIG_NUMBER;
		const float Tight = FMath::Clamp((6000.f - RadiusCm) / 3500.f, 0.f, 1.f);
		const FVector Along = (GetPointAt(Distance + Ahead * 200.f) - GetPointAt(Distance - Ahead * 200.f)).GetSafeNormal();
		if (Tight > 0.f && !Along.IsNearlyZero())
		{
			Heading = FMath::Lerp(Heading, Along, OversteerShare * Tight).GetSafeNormal();
		}
	}

	const FQuat TargetRotation = Heading.Rotation().Quaternion();
	CurrentRotation = (bSnap || RotationSmoothing <= 0.f) ? TargetRotation : FMath::QInterpTo(CurrentRotation, TargetRotation, DeltaTime, RotationSmoothing);

	// In a turn the body rolls a little outwards on its gear: lateral acceleration (forward speed x yaw rate) over g,
	// times 10 degrees, at most 2; it settles in about half a second
	FRotator Rotation = CurrentRotation.Rotator();
	if (!bSnap && DeltaTime > KINDA_SMALL_NUMBER)
	{
		const float YawRate = FMath::DegreesToRadians(FRotator::NormalizeAxis(Rotation.Yaw - LastYawDeg)) / DeltaTime;
		const float ForwardSpeed = bNoseAhead ? Speed : -Speed;
		LeanDeg = FMath::FInterpTo(LeanDeg, FMath::Clamp(-ForwardSpeed * YawRate / 981.f * 10.f, -2.f, 2.f), DeltaTime, 2.f);
	}
	else
	{
		LeanDeg = 0.f;
	}
	LastYawDeg = Rotation.Yaw;
	Rotation.Roll = LeanDeg;
	GetOwner()->SetActorLocationAndRotation(MainGear, Rotation);
}
