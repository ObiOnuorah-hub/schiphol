#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LensSetData.generated.h"

/** A lens: Wide 24 mm, Normal 50 mm, Tele 100 mm or a custom one. Field of view = 2 * atan(sensor width / (2 * focal length)). */
USTRUCT(BlueprintType)
struct FLensPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lens")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lens", meta = (Units = "mm"))
	float FocalLengthMm = 50.f;

	/** Full frame = 36 mm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lens", meta = (Units = "mm"))
	float SensorWidthMm = 36.f;
};

/** Where the camera looks: pan, tilt and zoom, for example Cam.EHAM.Threshold18R. */
USTRUCT(BlueprintType)
struct FCameraPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float PanDeg = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float TiltDeg = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float FocalLengthMm = 50.f;
};

/** The lenses of a camera type (DA_LensSet_GMI). A custom lens is one more row; the zoom range is the camera's FocalLimitsMm. */
UCLASS(BlueprintType)
class ATCCAMERA_API ULensSetData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("LensSet"), GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens")
	TArray<FLensPreset> Lenses;
};

UCLASS()
class ATCCAMERA_API ULensLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Horizontal field of view: 2 * atan(sensor width / (2 * focal length)). */
	UFUNCTION(BlueprintPure, Category = "ATC|Camera")
	static float FocalLengthToFOV(float FocalLengthMm, float SensorWidthMm = 36.f);

	UFUNCTION(BlueprintPure, Category = "ATC|Camera")
	static float FOVToFocalLength(float FOVDeg, float SensorWidthMm = 36.f);

	/**
	 * Focal length part way through a zoom: interpolated logarithmically, so 24 -> 50 mm takes as long as 50 -> 100 mm,
	 * like a real zoom lens. Alpha 0..1 is already eased.
	 */
	UFUNCTION(BlueprintPure, Category = "ATC|Camera")
	static float InterpolateFocalLength(float FromMm, float ToMm, float Alpha);
};
