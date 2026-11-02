#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnvironmentTypes.h"
#include "WeatherSubsystem.generated.h"

class ULightingRigComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeatherChanged, const FWeatherState&, NewWeather);

/** Current weather and smooth transitions between presets. Pushes every step into the lighting rig. */
UCLASS()
class ATCENVIRONMENT_API UWeatherSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
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

	UPROPERTY(BlueprintAssignable, Category = "ATC|Weather")
	FOnWeatherChanged OnWeatherChanged;

private:
	void Push();
	void NotifyChanged();

	FWeatherState Current;
	FWeatherState Start;
	FWeatherState Target;
	float Alpha = 0.f;
	float Duration = 1.f;
	bool bTransitioning = false;

	TWeakObjectPtr<ULightingRigComponent> LightingRig;
	TArray<TWeakObjectPtr<UObject>> Listeners;
};
