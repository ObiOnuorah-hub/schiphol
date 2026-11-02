#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AircraftTypes.h"
#include "Aircraft.generated.h"

class AFlightPath;
class UCameraComponent;
class USplineComponent;
class USpringArmComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAircraftStateChanged, EAircraftState, NewState);

/** One aircraft that flies the full cycle Pushback -> Parked, always locked to the splines of a FlightPath. */
UCLASS(Blueprintable)
class IMPORT_TESTING_OBI_API AAircraft : public AActor
{
	GENERATED_BODY()

public:
	AAircraft();

	virtual void Tick(float DeltaTime) override;

	/** Aircraft model, this is the root of the actor. Nose must point along +X. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aircraft")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aircraft")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aircraft")
	TObjectPtr<UCameraComponent> Camera;

	/** Player 0 watches through the chase camera when play starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	bool bFollowCamera = true;

	/** Show the current phase on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	bool bShowPhaseOnScreen = true;

	/** Turn the model if its nose does not point along +X (for example Yaw 90 or -90). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	FRotator MeshRotation = FRotator::ZeroRotator;

	/** Route to fly. If empty, the first FlightPath in the level is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	TObjectPtr<AFlightPath> FlightPath;

	/** Speed and pitch per phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	TMap<EAircraftState, FAircraftPhaseSettings> PhaseSettings;

	/** Max speed loss in cm/s per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	float Deceleration = 500.f;

	/** Braking used to stop exactly at the end of a spline (phases with bStopAtEnd). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	float StopDeceleration = 100.f;

	/** Lowest speed while a spline is not finished, so the aircraft always reaches the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	float MinMoveSpeed = 20.f;

	/** How fast the nose follows the spline rotation. 0 = snap exactly to the spline rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	float RotationInterpSpeed = 10.f;

	/** Max distance (cm) between the end of a spline and the start of the next before a warning is logged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	float RouteGapTolerance = 100.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Aircraft|State")
	EAircraftState CurrentState = EAircraftState::Pushback;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Aircraft|State")
	TObjectPtr<USplineComponent> CurrentSpline;

	/** Distance travelled along the current spline (cm). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Aircraft|State")
	float CurrentDistance = 0.f;

	/** Speed along the spline (cm/s). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Aircraft|State")
	float CurrentSpeed = 0.f;

	/** Fires every time a new phase starts (also for Parked). */
	UPROPERTY(BlueprintAssignable, Category = "Aircraft")
	FOnAircraftStateChanged OnStateChanged;

protected:
	virtual void BeginPlay() override;

private:
	void EnterState(EAircraftState NewState, float StartDistance);
	void UpdateTransform(const FAircraftPhaseSettings& Phase, float DeltaTime, bool bSnapRotation);
	void ValidateRoute() const;
	FVector GetPhasePoint(EAircraftState State, bool bExit) const;

	FQuat CurrentRotation = FQuat::Identity;
};
