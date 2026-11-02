#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/Interface.h"
#include "EnvironmentTypes.generated.h"

UENUM(BlueprintType)
enum class EPrecipitationType : uint8
{
	None,
	Rain,
	Snow
};

/** Phase of the day by sun elevation. */
UENUM(BlueprintType)
enum class EDayPhase : uint8
{
	/** Sun above -0.833 degrees (sunrise/sunset line). */
	Day,
	/** Down to -6 degrees: airport lighting on. */
	CivilTwilight,
	/** Down to -12 degrees. */
	NauticalTwilight,
	Night
};

/** The whole weather picture as numbers, so two presets can be blended. */
USTRUCT(BlueprintType)
struct ATCENVIRONMENT_API FWeatherState
{
	GENERATED_BODY()

	/** 0 = clear, 1 = overcast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudCoverage = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float CloudBaseFt = 3000.f;

	/** Meteorological visibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float VisibilityM = 10000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	EPrecipitationType Precipitation = EPrecipitationType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float PrecipitationRate = 0.f;

	/** Wet ground and puddles (MPC_Environment.Wetness). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float Wetness = 0.f;

	/** Snow layer (MPC_Environment.SnowCover). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float SnowCover = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float SunIntensityScale = 1.f;

	/** Larger = softer shadows (overcast). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float SunSourceAngle = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float WindDirectionDeg = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float WindSpeedKt = 8.f;

	static FWeatherState Lerp(const FWeatherState& A, const FWeatherState& B, float Alpha);
};

/** A weather preset, linked to the METAR a controller would read. */
UCLASS(BlueprintType)
class ATCENVIRONMENT_API UWeatherPresetData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("WeatherPreset"), GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	FString MetarExample;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	FWeatherState State;
};

UINTERFACE(BlueprintType, Blueprintable)
class ATCENVIRONMENT_API UWeatherListener : public UInterface
{
	GENERATED_BODY()
};

/** For actors that react to the weather, for example a windsock. Register with the WeatherSubsystem. */
class ATCENVIRONMENT_API IWeatherListener
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ATC|Weather")
	void OnWeatherChanged(const FWeatherState& NewWeather);
};

UINTERFACE(BlueprintType, Blueprintable)
class ATCENVIRONMENT_API UDayNightListener : public UInterface
{
	GENERATED_BODY()
};

/** For actors that react to day and night, for example lamps or screens. Register with the TimeOfDaySubsystem. */
class ATCENVIRONMENT_API IDayNightListener
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ATC|TimeOfDay")
	void OnDayPhaseChanged(EDayPhase NewPhase);
};
