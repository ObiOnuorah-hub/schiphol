#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AircraftMovementComponent.generated.h"

class USplineComponent;
class UAircraftTypeData;

UENUM(BlueprintType)
enum class ETravelDirection : uint8
{
	/** Distance along the spline increases. */
	Forward,
	/** Distance along the spline decreases. */
	Reverse
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPathEndReached);

/**
 * The only component that moves the aircraft. It follows a spline at a target speed and knows nothing about
 * stops or turns: the taxi and pushback controllers decide which spline and which speed.
 * The actor origin is the main gear, exactly on the spline; the nose gear is on the spline too, one wheelbase away.
 * Nothing moves an aircraft off its line: other traffic only changes its speed.
 */
UCLASS(ClassGroup = (ATC), meta = (BlueprintSpawnableComponent))
class ATCAIRCRAFT_API UAircraftMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAircraftMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Follow a spline. bInNoseAlongTravel false = pushback: the nose points against the travel direction. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Movement")
	void SetPath(USplineComponent* NewPath, ETravelDirection NewDirection, bool bInNoseAlongTravel, float StartDistance, bool bSnap = false);

	UFUNCTION(BlueprintCallable, Category = "ATC|Movement")
	void ClearPath();

	UFUNCTION(BlueprintCallable, Category = "ATC|Movement")
	void SetTargetSpeed(float NewTargetSpeedCms) { TargetSpeed = FMath::Max(NewTargetSpeedCms, 0.f); }

	/** Speed and target speed to zero at once (end of a stop, emergency). */
	UFUNCTION(BlueprintCallable, Category = "ATC|Movement")
	void StopImmediately() { Speed = 0.f; TargetSpeed = 0.f; }

	/** Distance left to the end of the spline in the travel direction. */
	UFUNCTION(BlueprintPure, Category = "ATC|Movement")
	float GetRemainingDistance() const;

	/** Distance needed to stop from the current speed. */
	UFUNCTION(BlueprintPure, Category = "ATC|Movement")
	float GetBrakingDistance() const { return Speed * Speed / (2.f * FMath::Max(DecelerationCms2, 1.f)); }

	UFUNCTION(BlueprintPure, Category = "ATC|Movement")
	bool HasPath() const { return Path != nullptr; }

	/** Takes acceleration, braking and wheelbase from the aircraft type. */
	void ApplyAircraftType(const UAircraftTypeData* Type);

	/** Distance along a spline closest to a world location. */
	static float GetDistanceClosestTo(const USplineComponent* Spline, const FVector& WorldLocation);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "ATC|Movement")
	TObjectPtr<USplineComponent> Path;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "ATC|Movement")
	float Distance = 0.f;

	/** cm/s, always >= 0. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "ATC|Movement")
	float Speed = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "ATC|Movement")
	float TargetSpeed = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "ATC|Movement")
	ETravelDirection Direction = ETravelDirection::Forward;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "ATC|Movement")
	bool bNoseAlongTravel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Movement")
	float AccelerationCms2 = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Movement")
	float DecelerationCms2 = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Movement")
	float WheelbaseCm = 1500.f;

	/**
	 * 0 (default) = exact: main gear and nose gear both on the line, so the aircraft follows the taxi line exactly and turns
	 * as smoothly as the line does. Above 0 = the old behaviour: the heading lags behind the line (interpolation speed).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Movement")
	float RotationSmoothing = 0.f;

	/**
	 * A new path that starts at another heading than the aircraft has (end of a pushback onto a taxi route): the
	 * difference fades out over this distance travelled, instead of the aircraft turning on the spot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Movement", meta = (Units = "cm"))
	float JoinBlendDistanceCm = 1000.f;

	/** Fired once when the end (Forward) or start (Reverse) of the spline is reached. */
	UPROPERTY(BlueprintAssignable, Category = "ATC|Movement")
	FOnPathEndReached OnPathEndReached;

private:
	void UpdateTransform(float DeltaTime, bool bSnap);
	FVector GetPointAt(float AtDistance) const;

	/**
	 * Heading with the main gear at Distance and the nose gear on the path one wheelbase away in a straight line (in a turn a
	 * little further along the path than one wheelbase). False on a zero-length path.
	 */
	bool GetExactRotation(FQuat& OutRotation) const;

	FQuat CurrentRotation = FQuat::Identity;
	bool bEndReported = false;

	/** Heading left over from the previous path (relative to the exact one) and how much of it is still there (1 -> 0). */
	FQuat JoinOffset = FQuat::Identity;
	float JoinBlend = 0.f;
	float LastDistance = 0.f;
};
