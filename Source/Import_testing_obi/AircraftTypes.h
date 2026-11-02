#pragma once

#include "CoreMinimal.h"
#include "AircraftTypes.generated.h"

/** Flight phases in flight order. The aircraft always goes to the next value when a spline is done. */
UENUM(BlueprintType)
enum class EAircraftState : uint8
{
	Pushback,
	Taxi,
	Takeoff,
	Departure,
	Cruise,
	Arrival,
	Landing,
	TaxiToGate,
	Parked
};

/** How the aircraft moves along the spline of one phase. Speeds in cm/s, pitch in degrees. */
USTRUCT(BlueprintType)
struct FAircraftPhaseSettings
{
	GENERATED_BODY()

	/** Target speed at the start of the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	float StartSpeed = 1000.f;

	/** Target speed at the end of the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	float EndSpeed = 1000.f;

	/** Max speed gain in cm/s per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	float Acceleration = 400.f;

	/** Progress (0-1) where the speed starts going from StartSpeed to EndSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float SpeedRampStart = 0.f;

	/** Brake so the aircraft stops exactly at the end of the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	bool bStopAtEnd = false;

	/** Travel the spline from end to start (pushback). The nose keeps pointing along the spline direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	bool bReverse = false;

	/** Extra nose-up pitch on top of the spline rotation at the start of the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pitch")
	float StartPitch = 0.f;

	/** Extra nose-up pitch on top of the spline rotation at the end of the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pitch")
	float EndPitch = 0.f;

	/** Progress (0-1) where the pitch starts going from StartPitch to EndPitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pitch", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float PitchRampStart = 0.f;
};
