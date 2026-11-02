#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnvironmentTypes.h"
#include "TimeOfDaySubsystem.generated.h"

class ULightingRigComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSunEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDayPhaseChanged, EDayPhase, NewPhase);

/**
 * Sun position for the current airport and the simulation clock (Core). Uses the engine's Sun Position plugin,
 * always in UTC: no daylight-saving mistakes. Updates once per simulated second; the sun moves 0.25 degrees a minute.
 * The light itself only turns when the sun has moved a tenth of a degree (every 24 s in real time): every turn of the
 * directional light throws away the cached shadow maps, which is not worth an invisible step.
 */
UCLASS()
class ATCENVIRONMENT_API UTimeOfDaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Airport location. NorthOffsetDeg = angle between Unreal +X and true north in this map. */
	UFUNCTION(BlueprintCallable, Category = "ATC|TimeOfDay")
	void SetLocation(double InLatitude, double InLongitude, float InNorthOffsetDeg);

	UFUNCTION(BlueprintPure, Category = "ATC|TimeOfDay")
	float GetSunElevation() const { return Elevation; }

	UFUNCTION(BlueprintPure, Category = "ATC|TimeOfDay")
	float GetSunAzimuth() const { return Azimuth; }

	UFUNCTION(BlueprintPure, Category = "ATC|TimeOfDay")
	EDayPhase GetDayPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "ATC|TimeOfDay")
	FDateTime GetSunriseUTC() const { return SunriseUTC; }

	UFUNCTION(BlueprintPure, Category = "ATC|TimeOfDay")
	FDateTime GetSunsetUTC() const { return SunsetUTC; }

	UFUNCTION(BlueprintPure, Category = "ATC|TimeOfDay")
	double GetLatitude() const { return Latitude; }

	UFUNCTION(BlueprintPure, Category = "ATC|TimeOfDay")
	double GetLongitude() const { return Longitude; }

	/** Actor implementing IDayNightListener. */
	UFUNCTION(BlueprintCallable, Category = "ATC|TimeOfDay")
	void RegisterListener(UObject* Listener);

	void RegisterRig(ULightingRigComponent* Rig);

	/** Corrected elevation and azimuth (degrees, azimuth clockwise from north). */
	static void ComputeSun(double Latitude, double Longitude, const FDateTime& TimeUTC, float& OutElevation, float& OutAzimuth);

	static EDayPhase PhaseForElevation(float ElevationDeg);

	/** Directional light rotation: light travels away from the sun (azimuth + 180), pitch down by the elevation. */
	static FRotator SunLightRotation(float ElevationDeg, float AzimuthDeg, float NorthOffsetDeg);

	UPROPERTY(BlueprintAssignable, Category = "ATC|TimeOfDay")
	FOnSunEvent OnSunrise;

	UPROPERTY(BlueprintAssignable, Category = "ATC|TimeOfDay")
	FOnSunEvent OnSunset;

	UPROPERTY(BlueprintAssignable, Category = "ATC|TimeOfDay")
	FOnDayPhaseChanged OnDayPhaseChanged;

private:
	UFUNCTION()
	void HandleTimeSet(FDateTime NewTimeUTC);

	void Update(bool bJumped);

	double Latitude = 52.3086;   // Schiphol until an airport is loaded
	double Longitude = 4.7639;
	float NorthOffsetDeg = 0.f;

	float Elevation = 0.f;
	float Azimuth = 0.f;
	EDayPhase Phase = EDayPhase::Day;
	FDateTime SunriseUTC;
	FDateTime SunsetUTC;
	FDateTime LastUpdate;
	bool bHasUpdated = false;

	/** The sun as the rig last got it: it only moves the light again after a visible step (see SunStepDeg). */
	float PushedElevation = 0.f;
	float PushedAzimuth = 0.f;
	bool bHasPushed = false;

	TWeakObjectPtr<ULightingRigComponent> LightingRig;
	TArray<TWeakObjectPtr<UObject>> Listeners;
};
