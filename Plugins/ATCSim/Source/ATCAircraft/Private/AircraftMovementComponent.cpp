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
	JoinOffset = FQuat::Identity;
	JoinBlend = 0.f;

	if (Path)
	{
		Distance = FMath::Clamp(StartDistance, 0.f, Path->GetSplineLength());
		LastDistance = Distance;
		// The heading it has now against the heading of the new path: that difference fades out as it rolls
		FQuat Exact;
		if (!bSnap && GetExactRotation(Exact))
		{
			JoinOffset = Exact.Inverse() * CurrentRotation;
			JoinOffset.Normalize();
			JoinBlend = JoinOffset.AngularDistance(FQuat::Identity) > KINDA_SMALL_NUMBER ? 1.f : 0.f;
		}
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

bool UAircraftMovementComponent::GetExactRotation(FQuat& OutRotation) const
{
	// Main gear on the spline, nose gear on the spline one wheelbase away in a straight line: the heading follows from the
	// two. In a turn that point is a little further along the line than one wheelbase (the chord is shorter than the arc):
	// a few steps find it, so both gears are on the line, also for a long wide-body in a tight turn
	const bool bNoseAhead = (Direction == ETravelDirection::Forward) == bNoseAlongTravel;
	const float Sign = bNoseAhead ? 1.f : -1.f;
	const FVector MainGear = GetPointAt(Distance);
	float Along = WheelbaseCm;
	FVector NoseGear = GetPointAt(Distance + Sign * Along);
	for (int32 Iteration = 0; Iteration < 4; ++Iteration)
	{
		const float Chord = FVector::Dist(MainGear, NoseGear);
		if (Chord < KINDA_SMALL_NUMBER || FMath::Abs(WheelbaseCm - Chord) < 0.2f)
		{
			break;
		}
		Along += WheelbaseCm - Chord;
		NoseGear = GetPointAt(Distance + Sign * Along);
	}
	const FVector Heading = (NoseGear - MainGear).GetSafeNormal();
	if (Heading.IsNearlyZero())
	{
		return false;
	}
	OutRotation = Heading.Rotation().Quaternion();
	return true;
}

void UAircraftMovementComponent::UpdateTransform(float DeltaTime, bool bSnap)
{
	FQuat Exact;
	if (!Path || !GetExactRotation(Exact))
	{
		return;
	}
	const FVector MainGear = GetPointAt(Distance);

	if (bSnap)
	{
		JoinBlend = 0.f;
		CurrentRotation = Exact;
	}
	else if (RotationSmoothing > 0.f)
	{
		CurrentRotation = FMath::QInterpTo(CurrentRotation, Exact, DeltaTime, RotationSmoothing);
	}
	else
	{
		// Both gears on the line: the heading is the line itself. Only a heading left over from the previous path fades,
		// by distance rolled, so an aircraft that stands still does not turn
		if (JoinBlend > 0.f)
		{
			JoinBlend *= FMath::Exp(-FMath::Abs(Distance - LastDistance) / FMath::Max(JoinBlendDistanceCm, 1.f));
			JoinBlend = JoinBlend < 0.001f ? 0.f : JoinBlend;
		}
		CurrentRotation = JoinBlend > 0.f ? Exact * FQuat::Slerp(FQuat::Identity, JoinOffset, JoinBlend) : Exact;
	}
	LastDistance = Distance;
	GetOwner()->SetActorLocationAndRotation(MainGear, CurrentRotation);
}
