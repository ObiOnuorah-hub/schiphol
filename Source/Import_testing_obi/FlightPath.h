#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AircraftTypes.h"
#include "FlightPath.generated.h"

class USplineComponent;

/** Holds the 8 splines of the flight cycle. The end of each spline is the start of the next one. */
UCLASS()
class IMPORT_TESTING_OBI_API AFlightPath : public AActor
{
	GENERATED_BODY()

public:
	AFlightPath();

	/** Spline for a flight phase. Returns nullptr for Parked. */
	UFUNCTION(BlueprintPure, Category = "Flight Path")
	USplineComponent* GetSpline(EAircraftState State) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	TObjectPtr<USplineComponent> PushbackSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	TObjectPtr<USplineComponent> TaxiSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	TObjectPtr<USplineComponent> TakeoffSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	TObjectPtr<USplineComponent> DepartureSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	TObjectPtr<USplineComponent> CruiseSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	TObjectPtr<USplineComponent> ArrivalSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	TObjectPtr<USplineComponent> LandingSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Splines")
	TObjectPtr<USplineComponent> TaxiToGateSpline;
};
