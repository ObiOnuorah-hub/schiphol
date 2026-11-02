// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AircraftSplineActor.generated.h"

UENUM(BlueprintType)
enum class EFlightPhase : uint8
{
	Pushback = 0 UMETA(DisplayName = "Pushback"),
	Taxi = 1 UMETA(DisplayName = "Taxi"),
	Takeoff = 2 UMETA(DisplayName = "Takeoff"),
	Landing = 3 UMETA(DisplayName = "Landing")
};

UCLASS()
class IMPORT_TESTING_OBI_API AAircraftSplineActor : public AActor
{
	GENERATED_BODY()

public:
	AAircraftSplineActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

protected:
	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aircraft")
	UStaticMeshComponent* AircraftMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	USplineComponent* PushbackSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	USplineComponent* TaxiSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	USplineComponent* TakeoffSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	USplineComponent* LandingSpline;

	// Flight Parameters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft|Speed")
	float PushbackSpeed = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft|Speed")
	float TaxiSpeed = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft|Speed")
	float TaxiAcceleration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft|Speed")
	float TakeoffMaxSpeed = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft|Speed")
	float TakeoffAcceleration = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft|Speed")
	float LandingStartSpeed = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft|Speed")
	float LandingDeceleration = 20.0f;

	// State variables
	EFlightPhase CurrentPhase;
	USplineComponent* CurrentSpline;
	float DistanceAlongSpline;
	float CurrentSpeed;
	float TargetSpeed;
	float CurrentAccelerationRate;

private:
	void UpdateSpeed(float DeltaTime);
	void UpdateMovement(float DeltaTime);
	bool HasReachedSplineEnd() const;
	void AdvanceToNextPhase();
	void InitializePhase(EFlightPhase NewPhase);
};