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
	const FVector Heading = (NoseGear - MainGear).GetSafeNormal();
	if (Heading.IsNearlyZero())
	{
		return;
	}

	const FQuat TargetRotation = Heading.Rotation().Quaternion();
	CurrentRotation = (bSnap || RotationSmoothing <= 0.f) ? TargetRotation : FMath::QInterpTo(CurrentRotation, TargetRotation, DeltaTime, RotationSmoothing);
	GetOwner()->SetActorLocationAndRotation(MainGear, CurrentRotation);
}
