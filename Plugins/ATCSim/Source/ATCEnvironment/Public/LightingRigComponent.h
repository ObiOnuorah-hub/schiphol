#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnvironmentTypes.h"
#include "LightingRigComponent.generated.h"

class ADirectionalLight;
class APostProcessVolume;
class AExponentialHeightFog;
class ASkyAtmosphere;
class ASkyLight;
class AVolumetricCloud;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UNiagaraComponent;
class UNiagaraSystem;
class UVolumetricCloudComponent;

/**
 * The only writer to sun, sky light, fog, clouds, MPC_Environment and precipitation. Day/night supplies the sun
 * direction, the weather supplies clouds, visibility and precipitation; this component combines both.
 */
UCLASS(ClassGroup = (ATC), meta = (BlueprintSpawnableComponent))
class ATCENVIRONMENT_API ULightingRigComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULightingRigComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Input from day/night. */
	void SetSun(const FRotator& Rotation, float ElevationDeg, float NightAmount);

	/** Input from the weather. */
	void SetWeather(const FWeatherState& State);

	/** Fog density for a visibility, from the engine's height fog formula and Koschmieder (visibility = 3.912 / extinction). */
	static float VisibilityToFogDensity(float VisibilityM);

	/** Empty = first one found in the level at BeginPlay. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "ATC|Lighting")
	TObjectPtr<ADirectionalLight> Sun;

	/** Optional second directional light for the night, without atmosphere so the sky stays dark. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "ATC|Lighting")
	TObjectPtr<ADirectionalLight> Moon;

	/** No Moon set: make one at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Lighting")
	bool bCreateMoon = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "ATC|Lighting")
	TObjectPtr<ASkyLight> SkyLight;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "ATC|Lighting")
	TObjectPtr<AExponentialHeightFog> Fog;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "ATC|Lighting")
	TObjectPtr<AVolumetricCloud> Clouds;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "ATC|Lighting")
	TObjectPtr<ASkyAtmosphere> Atmosphere;

	/** Optional unbound post process volume; the rig lowers the exposure at night. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "ATC|Lighting")
	TObjectPtr<APostProcessVolume> PostProcess;

	/** Exposure compensation at full night (stops). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Lighting")
	float NightExposureBias = -4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Lighting")
	TSoftObjectPtr<UMaterialParameterCollection> EnvironmentParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Lighting")
	TSoftObjectPtr<UNiagaraSystem> RainSystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Lighting")
	TSoftObjectPtr<UNiagaraSystem> SnowSystem;

	/** Sun intensity at full daylight. 0 = take the value the sun has in the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Lighting")
	float DayIlluminanceLux = 0.f;

	/** Moonlight at full night, clear sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Lighting")
	float MoonIlluminanceLux = 0.3f;

	/** Multiplies the fog density; 1 = physically matched to the visibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Lighting")
	float VisibilityCalibration = 1.f;

	/** Volumetric fog is too heavy for VR; only for the desktop/instructor view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Lighting")
	bool bVolumetricFog = false;

	/** Cloud_GlobalCoverage of the engine cloud material at coverage 0 and 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Lighting")
	FVector2D CloudCoverageRange = FVector2D(-0.8f, 0.6f);

	UFUNCTION(BlueprintPure, Category = "ATC|Lighting")
	FWeatherState GetWeather() const { return Weather; }

private:
	void FindMissingActors();
	void Recompose();
	void UpdatePrecipitation();

	/**
	 * A sky light without real-time capture is captured again (a whole cubemap) only when the sky changed enough to
	 * see: the sun one degree further, other clouds or visibility, or dusk turning into night. Not every second.
	 */
	void RequestRecaptureIfChanged();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

	/** Found once at BeginPlay, not on every update. */
	UPROPERTY(Transient)
	TObjectPtr<UVolumetricCloudComponent> CloudComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> Collection;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> PrecipitationComponent;

	FWeatherState Weather;
	FRotator SunRotation = FRotator(-45.f, 0.f, 0.f);
	float SunElevation = 45.f;
	float Night = 0.f;
	bool bHasSunInput = false;
	float BaseMieScattering = -1.f;
	EPrecipitationType ActivePrecipitation = EPrecipitationType::None;
	bool bPrecipitationDirty = false;
	float RecaptureTimer = 0.f;
	bool bRecapturePending = false;

	/** The sky at the last capture. */
	bool bHasCaptured = false;
	float CapturedElevation = 0.f;
	float CapturedYaw = 0.f;
	float CapturedCoverage = 0.f;
	float CapturedVisibility = 0.f;
	float CapturedNight = 0.f;
};

/** Container actor for the lighting rig; placed once per airport map (as Level Instance). */
UCLASS(Blueprintable)
class ATCENVIRONMENT_API AEnvironmentRig : public AActor
{
	GENERATED_BODY()

public:
	AEnvironmentRig();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Lighting")
	TObjectPtr<ULightingRigComponent> LightingRig;
};
