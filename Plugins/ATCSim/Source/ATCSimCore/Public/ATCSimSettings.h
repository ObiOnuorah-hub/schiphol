#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ATCSimSettings.generated.h"

/** Where the weather comes from. */
UENUM(BlueprintType)
enum class EATCWeatherMode : uint8
{
	/** Only presets (scenario, airport, ATC.Weather): the weather never changes by itself. */
	Fixed,
	/** Starts like Fixed, then drifts to a new plausible weather every 10-30 minutes (clouds, showers, mist at dawn, snow in winter). */
	Dynamic,
	/** The real weather of the airport from its METAR, refreshed every few minutes. Offline: Dynamic. */
	Live
};

/** Project-wide defaults for the ATC building blocks: Project Settings > Plugins > ATC Sim. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ATC Sim"))
class ATCSIMCORE_API UATCSimSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UATCSimSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Console variables applied when a scenario starts with a VR headset active. Lighter rendering for 90 Hz. */
	UPROPERTY(Config, EditAnywhere, Category = "Render profiles")
	TMap<FString, FString> VRProfile;

	/** Console variables applied when a scenario starts without a headset (instructor screen). Empty = keep the project settings. */
	UPROPERTY(Config, EditAnywhere, Category = "Render profiles")
	TMap<FString, FString> DesktopProfile;

	/** Safety distance: taxiing aircraft stop this far behind the aircraft ahead (nose to tail, along the path). */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float TaxiSeparationM = 60.f;

	/** How far past its braking distance a taxiing aircraft looks for traffic and reserves its route. */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float TrafficLookAheadM = 150.f;

	/** Spawning waits until no other aircraft is this close to the new one (along its heading). */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float SpawnClearanceM = 80.f;

	/** Room that must stay between the wingtips of two aircraft on crossing or neighbouring taxiways. */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float WingtipClearanceM = 7.5f;

	/**
	 * Two parallel taxiways for the same trip (a route with Alternatives): with equal traffic each aircraft takes the
	 * right-hand one, so the two directions each keep to their own taxiway. Opposing traffic on a lane always counts first.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic")
	bool bKeepRightOnParallelTaxiways = true;

	/**
	 * In the queues a landing counts as having waited this long already: it goes before everybody on the ground who has
	 * waited less. Whoever has waited longer goes first and keeps the landing in the stack, so a stream of landings
	 * never keeps a departure waiting for good.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "s"))
	float LandingHeadStartSeconds = 120.f;

	/**
	 * A landing holds in the stack until its runway exit can be cleared (nobody on it, nobody waiting for it longer),
	 * at most this long. Then it comes anyway, and goes around when the exit is still taken 1.5 km before the runway.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "s"))
	float MaxHoldingSeconds = 300.f;

	/** Landings on the same runway come out of the stack at least this far apart. */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "s"))
	float LandingSpacingSeconds = 75.f;

	/**
	 * Plain Play in an airport map (no scenario) gets the environment rig too: the sun and the sky follow the clock, and the
	 * weather follows WeatherMode. Off = the map keeps its own light, as it was saved.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Environment")
	bool bLiveEnvironmentOnPlay = true;

	/**
	 * The clock runs on the real UTC time of this computer: day and night outside are day and night in the tower. A scenario
	 * with bLiveEnvironment off keeps its own StartTimeUTC. ATC.Time and ATC.TimeScale leave real time until ATC.RealTime.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Environment")
	bool bRealTimeClock = true;

	UPROPERTY(Config, EditAnywhere, Category = "Environment")
	EATCWeatherMode WeatherMode = EATCWeatherMode::Live;

	/** {ICAO} is replaced by the airport code. The answer is the raw METAR text. */
	UPROPERTY(Config, EditAnywhere, Category = "Environment")
	FString MetarUrl = TEXT("https://aviationweather.gov/api/data/metar?ids={ICAO}&format=raw");

	/** An airport without a real ICAO code (the showcase) uses the weather of this station. */
	UPROPERTY(Config, EditAnywhere, Category = "Environment")
	FString DefaultMetarStation = TEXT("EHAM");

	/** METARs come out every 30 minutes; asking more often only catches a new one sooner. */
	UPROPERTY(Config, EditAnywhere, Category = "Environment", meta = (Units = "min", ClampMin = "1"))
	float MetarRefreshMinutes = 10.f;

	/** A new METAR blends in over this time, so a front moves in instead of switching on. */
	UPROPERTY(Config, EditAnywhere, Category = "Environment", meta = (Units = "s", ClampMin = "0"))
	float LiveWeatherBlendSeconds = 120.f;

	/** Dynamic weather: time between two weather changes, min and max (real minutes, the clock's TimeScale speeds it up). */
	UPROPERTY(Config, EditAnywhere, Category = "Environment")
	FVector2D DynamicWeatherMinutes = FVector2D(10.f, 30.f);

	/** Dynamic weather: one change takes this long, min and max (real minutes). */
	UPROPERTY(Config, EditAnywhere, Category = "Environment")
	FVector2D DynamicBlendMinutes = FVector2D(3.f, 8.f);

	/** Ground vehicles serve parked aircraft: fuel truck, baggage train, catering truck; and a tug for every pushback. */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles")
	bool bGroundVehicles = true;

	/** Upper limit of service vehicles in the whole airport (each is a few simple meshes). Pushback tugs do not count. */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "0"))
	int32 MaxServiceVehicles = 40;

	/** Share of the aircraft already parked when the map starts that have service vehicles around them. */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "0", ClampMax = "1"))
	float ServicedAtStartShare = 0.5f;

	/** Asset paths that must not depend on content outside the project or this plugin (see the reference validator). */
	UPROPERTY(Config, EditAnywhere, Category = "Validation")
	TArray<FString> ValidatedPaths;

	/** Content roots that are always allowed as dependencies. */
	UPROPERTY(Config, EditAnywhere, Category = "Validation")
	TArray<FString> AllowedDependencyRoots;
};
