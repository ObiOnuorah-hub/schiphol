#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ATCUnits.generated.h"

ATCSIMCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogATC, Log, All);

/** The only place with conversion factors. Assets use aviation units, code uses Unreal units (cm, cm/s). */
namespace ATCUnits
{
	constexpr float KnotsToCms = 51.4444f;   // 1 kt = 0.514444 m/s
	constexpr float FeetToCm = 30.48f;
	constexpr float MetersToCm = 100.f;
	constexpr float NmToCm = 185200.f;       // nautical mile
}

UCLASS()
class ATCSIMCORE_API UATCUnitsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "ATC|Units")
	static float KnotsToCms(float Knots) { return Knots * ATCUnits::KnotsToCms; }

	UFUNCTION(BlueprintPure, Category = "ATC|Units")
	static float CmsToKnots(float Cms) { return Cms / ATCUnits::KnotsToCms; }

	UFUNCTION(BlueprintPure, Category = "ATC|Units")
	static float FeetToCm(float Feet) { return Feet * ATCUnits::FeetToCm; }

	UFUNCTION(BlueprintPure, Category = "ATC|Units")
	static float MetersToCm(float Meters) { return Meters * ATCUnits::MetersToCm; }
};
