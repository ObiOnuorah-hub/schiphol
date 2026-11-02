#include "LightingRigComponent.h"
#include "ATCUnits.h"
#include "TimeOfDaySubsystem.h"
#include "WeatherSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
	template<typename T>
	void FindFirst(UWorld* World, TObjectPtr<T>& Out)
	{
		if (!Out)
		{
			for (TActorIterator<T> It(World); It; ++It)
			{
				Out = *It;
				break;
			}
		}
	}
}

AEnvironmentRig::AEnvironmentRig()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	LightingRig = CreateDefaultSubobject<ULightingRigComponent>(TEXT("LightingRig"));
}

ULightingRigComponent::ULightingRigComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	EnvironmentParameters = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/ATCSim/Environment/MPC_Environment.MPC_Environment")));
	RainSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/ATCSim/Environment/NS_Rain.NS_Rain")));
	SnowSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/ATCSim/Environment/NS_Snow.NS_Snow")));
}

float ULightingRigComponent::VisibilityToFogDensity(float VisibilityM)
{
	// Engine: extinction per cm = FogDensity / 1000 * ln(2)^2 at the fog height (HeightFogCommon.ush, SceneCore.cpp).
	// Koschmieder: 2% contrast left at the visibility, so extinction * visibility = ln(50) = 3.912.
	const float VisibilityCm = FMath::Max(VisibilityM, 10.f) * ATCUnits::MetersToCm;
	return FMath::Clamp(3.912f * 1000.f / (FMath::Square(FMath::Loge(2.f)) * VisibilityCm), 0.f, 10.f);
}

void ULightingRigComponent::FindMissingActors()
{
	UWorld* World = GetWorld();
	FindFirst(World, Sun);
	FindFirst(World, SkyLight);
	FindFirst(World, Fog);
	FindFirst(World, Clouds);
	FindFirst(World, Atmosphere);

	if (!PostProcess)
	{
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			if (It->bUnbound)
			{
				PostProcess = *It;
				break;
			}
		}
	}

	// Without a moon light the night is pitch black; the rig brings its own when the map has none
	if (!Moon && bCreateMoon && Sun)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Moon = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(-35.f, 0.f, 0.f)), Params);
		if (UDirectionalLightComponent* Light = Moon ? Cast<UDirectionalLightComponent>(Moon->GetLightComponent()) : nullptr)
		{
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetAtmosphereSunLight(false);
			Light->SetIntensity(0.f);
			Light->SetLightColor(FLinearColor(0.65f, 0.75f, 1.f));
			Light->SetCastShadows(false);
		}
	}
}

void ULightingRigComponent::BeginPlay()
{
	Super::BeginPlay();
	FindMissingActors();

	if (Moon)
	{
		Moon->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	}
	if (Sun)
	{
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		if (DayIlluminanceLux <= 0.f)
		{
			DayIlluminanceLux = Sun->GetLightComponent()->Intensity;
		}
	}
	CloudComponent = Clouds ? Clouds->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
	if (CloudComponent && CloudComponent->GetMaterial())
	{
		CloudMaterial = UMaterialInstanceDynamic::Create(CloudComponent->GetMaterial(), this);
		CloudComponent->SetMaterial(CloudMaterial);
	}
	Collection = EnvironmentParameters.LoadSynchronous();
	if (Atmosphere)
	{
		BaseMieScattering = Atmosphere->GetComponent()->MieScatteringScale;
	}

	// Register last: both subsystems push their current state into the rig straight away
	if (UWeatherSubsystem* WeatherSystem = GetWorld()->GetSubsystem<UWeatherSubsystem>())
	{
		Weather = WeatherSystem->GetCurrentWeather();
		WeatherSystem->RegisterRig(this);
	}
	if (UTimeOfDaySubsystem* TimeOfDay = GetWorld()->GetSubsystem<UTimeOfDaySubsystem>())
	{
		TimeOfDay->RegisterRig(this);
	}
	Recompose();
}

void ULightingRigComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PrecipitationComponent)
	{
		PrecipitationComponent->DestroyComponent();
	}
	Super::EndPlay(EndPlayReason);
}

void ULightingRigComponent::SetSun(const FRotator& Rotation, float ElevationDeg, float NightAmount)
{
	SunRotation = Rotation;
	SunElevation = ElevationDeg;
	Night = NightAmount;
	bHasSunInput = true;
	Recompose();
}

void ULightingRigComponent::SetWeather(const FWeatherState& State)
{
	if (State.Precipitation != Weather.Precipitation)
	{
		bPrecipitationDirty = true;
	}
	Weather = State;
	Recompose();
}

void ULightingRigComponent::Recompose()
{
	// The atmosphere colours the light near the horizon; below -12 degrees (night) the sun is off
	const float DayFactor = FMath::SmoothStep(-12.f, 0.f, SunElevation);
	if (Moon && Moon != Sun)
	{
		// Roughly opposite the sun, high in the sky; clouds dim it
		Moon->SetActorRotation(FRotator(-35.f, SunRotation.Yaw + 180.f, 0.f));
		Moon->GetLightComponent()->SetIntensity(MoonIlluminanceLux * (1.f - DayFactor) * (1.f - 0.8f * Weather.CloudCoverage));
	}

	if (Sun)
	{
		ULightComponent* Light = Sun->GetLightComponent();
		if (bHasSunInput)
		{
			Sun->SetActorRotation(SunRotation);
		}
		Light->SetIntensity(DayIlluminanceLux * DayFactor * Weather.SunIntensityScale);
		if (UDirectionalLightComponent* Directional = Cast<UDirectionalLightComponent>(Light))
		{
			Directional->SetLightSourceAngle(Weather.SunSourceAngle);
		}
	}

	if (Fog)
	{
		UExponentialHeightFogComponent* FogComponent = Fog->GetComponent();
		FogComponent->SetFogDensity(VisibilityToFogDensity(Weather.VisibilityM) * VisibilityCalibration);
		// Mist is a thick layer near the ground; in clear air the fog thins out quickly with height
		const float Thickness = FMath::Clamp(FMath::Loge(FMath::Max(Weather.VisibilityM, 100.f) / 1000.f) / FMath::Loge(10.f), 0.f, 1.f);
		FogComponent->SetFogHeightFalloff(FMath::Lerp(0.02f, 0.2f, Thickness));
		FogComponent->SetVolumetricFog(bVolumetricFog);
	}

	if (Atmosphere && BaseMieScattering >= 0.f)
	{
		const float Haze = 1.f - FMath::Clamp(Weather.VisibilityM / 20000.f, 0.f, 1.f);
		Atmosphere->GetComponent()->SetMieScatteringScale(BaseMieScattering * (1.f + 4.f * Haze));
	}

	if (CloudComponent)
	{
		CloudComponent->SetLayerBottomAltitude(FMath::Max(Weather.CloudBaseFt * ATCUnits::FeetToCm / 100000.f, 0.1f));
	}
	if (CloudMaterial)
	{
		CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), FMath::Lerp(CloudCoverageRange.X, CloudCoverageRange.Y, Weather.CloudCoverage));
		const float Storm = Weather.Precipitation == EPrecipitationType::Rain ? Weather.PrecipitationRate * 0.7f : 0.f;
		CloudMaterial->SetScalarParameterValue(TEXT("StormClouds"), Storm);
	}

	if (PostProcess)
	{
		// Auto exposure would make a moonlit night look like day: darken from nautical twilight on, keep dusk bright
		const float Darkness = 1.f - FMath::SmoothStep(-12.f, -3.f, SunElevation);
		PostProcess->Settings.bOverride_AutoExposureBias = true;
		PostProcess->Settings.AutoExposureBias = NightExposureBias * Darkness;
	}

	if (Collection)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, Collection, TEXT("Wetness"), Weather.Wetness);
		UKismetMaterialLibrary::SetScalarParameterValue(this, Collection, TEXT("SnowCover"), Weather.SnowCover);
		UKismetMaterialLibrary::SetScalarParameterValue(this, Collection, TEXT("CloudCoverage"), Weather.CloudCoverage);
		UKismetMaterialLibrary::SetScalarParameterValue(this, Collection, TEXT("NightAmount"), Night);
		// The rain and snow materials draw this fraction of their particles
		UKismetMaterialLibrary::SetScalarParameterValue(this, Collection, TEXT("PrecipitationRate"),
			Weather.Precipitation == EPrecipitationType::None ? 0.f : FMath::Clamp(Weather.PrecipitationRate, 0.f, 1.f));
	}

	UpdatePrecipitation();
	RequestRecaptureIfChanged();
}

void ULightingRigComponent::RequestRecaptureIfChanged()
{
	const float Yaw = SunRotation.Yaw;
	const float Visibility = FMath::Max(Weather.VisibilityM, 10.f);
	const bool bChanged = !bHasCaptured
		|| FMath::Abs(SunElevation - CapturedElevation) >= 1.f
		|| FMath::Abs(FMath::FindDeltaAngleDegrees(CapturedYaw, Yaw)) >= 2.f
		|| FMath::Abs(Weather.CloudCoverage - CapturedCoverage) >= 0.05f
		|| FMath::Abs(FMath::Loge(Visibility / FMath::Max(CapturedVisibility, 10.f))) >= 0.2f
		|| FMath::Abs(Night - CapturedNight) >= 0.05f;
	if (bChanged)
	{
		bRecapturePending = true;
		bHasCaptured = true;
		CapturedElevation = SunElevation;
		CapturedYaw = Yaw;
		CapturedCoverage = Weather.CloudCoverage;
		CapturedVisibility = Visibility;
		CapturedNight = Night;
	}
}

void ULightingRigComponent::UpdatePrecipitation()
{
	if (bPrecipitationDirty || ActivePrecipitation != Weather.Precipitation)
	{
		// Rain and snow fall in a volume that follows the camera, not over the whole airport
		// Wait until the player has a pawn: before that the view target does not follow the camera
		APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
		if (!CameraManager || !Cast<APawn>(CameraManager->GetViewTarget()))
		{
			bPrecipitationDirty = true;
			return;
		}
		if (PrecipitationComponent)
		{
			PrecipitationComponent->DestroyComponent();
			PrecipitationComponent = nullptr;
		}

		UNiagaraSystem* System = Weather.Precipitation == EPrecipitationType::Rain ? RainSystem.LoadSynchronous()
			: Weather.Precipitation == EPrecipitationType::Snow ? SnowSystem.LoadSynchronous() : nullptr;
		const AActor* ViewTarget = CameraManager->GetViewTarget();
		if (System && ViewTarget && ViewTarget->GetRootComponent())
		{
			// Follow the player (the camera manager itself does not move with the view)
			PrecipitationComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(System, ViewTarget->GetRootComponent(), NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
		}
		else if (Weather.Precipitation != EPrecipitationType::None)
		{
			UE_LOG(LogATC, Warning, TEXT("LightingRig: no Niagara system set for %s."), *UEnum::GetValueAsString(Weather.Precipitation));
		}
		ActivePrecipitation = Weather.Precipitation;
		bPrecipitationDirty = false;
	}

	// The amount is MPC_Environment.PrecipitationRate (set in Recompose), read by the rain and snow materials.
	// Wind direction and speed are not passed to the particles yet: NS_Rain/NS_Snow use a fixed light wind
}

void ULightingRigComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bPrecipitationDirty)
	{
		UpdatePrecipitation();
	}

	// A sky light without real-time capture is recaptured after a visible change, at most once every two seconds
	RecaptureTimer += DeltaTime;
	if (bRecapturePending && RecaptureTimer >= 2.f && SkyLight)
	{
		USkyLightComponent* SkyComponent = SkyLight->GetLightComponent();
		if (!SkyComponent->bRealTimeCapture)
		{
			SkyComponent->RecaptureSky();
		}
		bRecapturePending = false;
		RecaptureTimer = 0.f;
	}
}
