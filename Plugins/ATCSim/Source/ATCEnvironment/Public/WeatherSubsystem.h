#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ATCSimSettings.h"
#include "EnvironmentTypes.h"
#include "MetarReport.h"
#include "WeatherSubsystem.generated.h"

class ULightingRigComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeatherChanged, const FWeatherState&, NewWeather);

/**
 * Current weather and smooth transitions between presets. Pushes every step into the lighting rig.
 *
 * Where the weather comes from is the WeatherMode (Project Settings > Plugins > ATC Sim > Environment, or ATC.WeatherMode):
 * - Fixed: only presets (scenario, airport, ATC.Weather).
 * - Dynamic: every 10 to 30 minutes the weather drifts to a new, plausible one: clouds come and go, showers, mist at
 *   dawn, snow only in winter. It blends over minutes, like a front moving in.
 * - Live: the real METAR of the airport (aviationweather.gov), asked every few minutes. No answer: Dynamic until there is one.
 * The weather only changes by itself while an environment rig shows it.
 */
UCLASS()
class ATCENVIRONMENT_API UWeatherSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Blend to a preset in TransitionSeconds (0 = at once). */
	UFUNCTION(BlueprintCallable, Category = "ATC|Weather")
	void SetWeather(UWeatherPresetData* Preset, float TransitionSeconds = 20.f);

	UFUNCTION(BlueprintCallable, Category = "ATC|Weather")
	void SetWeatherState(const FWeatherState& NewState, float TransitionSeconds = 20.f);

	UFUNCTION(BlueprintPure, Category = "ATC|Weather")
	FWeatherState GetCurrentWeather() const { return Current; }

	UFUNCTION(BlueprintPure, Category = "ATC|Weather")
	bool IsTransitioning() const { return bTransitioning; }

	/** Actor implementing IWeatherListener; called after every completed change. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Weather")
	void RegisterListener(UObject* Listener);

	void RegisterRig(ULightingRigComponent* Rig);

	/** False in a map without an environment rig (for example an imported airport before a scenario starts). */
	bool HasRig() const { return LightingRig.IsValid(); }

	/** Fixed, Dynamic or Live. Live asks for the METAR at once. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Weather")
	void SetWeatherMode(EATCWeatherMode NewMode);

	UFUNCTION(BlueprintPure, Category = "ATC|Weather")
	EATCWeatherMode GetWeatherMode() const { return Mode; }

	/** The ICAO code whose METAR live weather uses (the airport of the map). Not a real code: DefaultMetarStation. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Weather")
	void SetMetarStation(const FString& ICAO);

	UFUNCTION(BlueprintPure, Category = "ATC|Weather")
	FString GetMetarStation() const { return Station; }

	/** Ask for the METAR now (Live). */
	UFUNCTION(BlueprintCallable, Category = "ATC|Weather")
	void RefreshLiveWeather();

	/**
	 * A scenario or map just set its start weather: live weather goes back to the real weather at once (the last METAR,
	 * and it asks for a newer one); dynamic weather takes its next step from the new start.
	 */
	UFUNCTION(BlueprintCallable, Category = "ATC|Weather")
	void RestartAutomaticWeather();

	/**
	 * Use a METAR text as if it had just come in, also offline: "EHAM 021225Z 22012KT 4000 RA BKN008 11/10 Q1004".
	 * False when the text is not a METAR.
	 */
	UFUNCTION(BlueprintCallable, Category = "ATC|Weather")
	bool ApplyMetar(const FString& MetarText, float TransitionSeconds = 120.f);

	/** The last METAR that was used, as received. */
	UFUNCTION(BlueprintPure, Category = "ATC|Weather")
	FString GetLastMetar() const { return LastMetar; }

	/** One line for the log: mode, station, last METAR and what comes next. */
	FString Describe() const;

	/**
	 * Dynamic weather: the next weather after From, for this month and local hour. Mostly a small step (clear to some
	 * cloud, cloud to overcast or showers), mist only at night and in the morning, snow only in winter.
	 */
	static FWeatherState MakeDynamicWeather(FRandomStream& InRandom, const FWeatherState& From, int32 Month, int32 LocalHour);

	UPROPERTY(BlueprintAssignable, Category = "ATC|Weather")
	FOnWeatherChanged OnWeatherChanged;

private:
	void Push();
	void NotifyChanged();
	void TickLive(float SimDeltaSeconds);
	void TickDynamic(float SimDeltaSeconds);
	void StartDynamicChange();
	void HandleMetarText(const FString& Text, bool bSucceeded, int32 ResponseCode);

	/** Real seconds times the clock's time scale: a faster clock also brings the weather sooner. */
	float GetSimDelta(float DeltaTime) const;

	FWeatherState Current;
	FWeatherState Start;
	FWeatherState Target;
	float Alpha = 0.f;
	float Duration = 1.f;
	bool bTransitioning = false;
	/** Long transitions update the rig ten times a second, not every frame. */
	float PushTimer = 0.f;

	EATCWeatherMode Mode = EATCWeatherMode::Fixed;
	FString Station;
	FString LastMetar;
	FMetarReport LastReport;
	bool bHasLiveWeather = false;
	bool bRequestInFlight = false;
	int32 FailedRequests = 0;
	/** Seconds until the next METAR request. */
	float LiveTimer = 0.f;
	/** Seconds until the next dynamic change. */
	float DynamicTimer = 0.f;
	FRandomStream Random;

	TWeakObjectPtr<ULightingRigComponent> LightingRig;
	TArray<TWeakObjectPtr<UObject>> Listeners;
};
