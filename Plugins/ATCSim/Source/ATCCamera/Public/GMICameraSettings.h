#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GMICameraSettings.generated.h"

class UInputAction;
class UInputMappingContext;

/** GMI cameras: render budget and operator input. Project Settings > Plugins > ATC Sim Camera. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ATC Sim Camera"))
class ATCCAMERA_API UGMICameraSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGMICameraSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/**
	 * Every capture renders the whole scene again, and VR renders it twice already: at most this many cameras are live
	 * (at their own CaptureRateHz). The camera the joystick moves goes first, then the screens that are biggest in view.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0"))
	int32 MaxLiveCameras = 2;

	/** The other cameras whose screen is in view. A camera whose screens nobody sees does not render at all. */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (Units = "Hz", ClampMin = "0"))
	float BackgroundRateHz = 5.f;

	/** At most this many captures in one frame, so the cost is spread over the frames. */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxCapturesPerFrame = 2;

	/** Mapping for the camera the operator moves (added with priority 1 when play starts). */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> InputContext;

	/** Axis: right is positive. */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputAction> PanAction;

	/** Axis: up is positive. */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputAction> TiltAction;

	/** Axis: zoom in is positive. */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputAction> ZoomAction;

	/** 1 = next camera, -1 = previous. */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputAction> NextCameraAction;

	/** 1 = next preset, -1 = previous. */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputAction> NextPresetAction;

	/** The value is the preset number (1 = the first preset). */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputAction> PresetAction;

	/** Next lens of the lens set. */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputAction> LensAction;
};
