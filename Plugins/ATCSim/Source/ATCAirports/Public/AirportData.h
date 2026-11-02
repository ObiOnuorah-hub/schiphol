#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AirportData.generated.h"

class UWeatherPresetData;
class UWorld;

/** A viewpoint in a tower. Schiphol has three in the same world: Centrum, West and the emergency tower. */
USTRUCT(BlueprintType)
struct FTowerPosition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airport")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airport")
	FText Name;

	/** Floor point in the tower cab; X points where the controller looks by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airport")
	FTransform Transform;

	/** Eye height above the floor for a non-VR view (VR uses the real head height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airport")
	float EyeHeightM = 1.7f;
};

USTRUCT(BlueprintType)
struct FRunwayInfo
{
	GENERATED_BODY()

	/** For example 18R/36L. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airport")
	FString Designator;

	/** For example Polderbaan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airport")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airport")
	float LengthM = 0.f;
};

/** Everything that makes one airport: map, location, tower positions, runways and default weather. */
UCLASS(BlueprintType)
class ATCAIRPORTS_API UAirportData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("Airport"), GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport")
	FName ICAO;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport")
	TSoftObjectPtr<UWorld> Map;

	/** WGS84 position of MapOrigin. Also used for the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport|Geo")
	double Latitude = 52.3086;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport|Geo")
	double Longitude = 4.7639;

	/** Map location (cm) of Latitude/Longitude. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport|Geo")
	FVector MapOrigin = FVector::ZeroVector;

	/** Unreal yaw of true north in this map (0 = north along +X, -90 = north along -Y). Used for the sun and for GeoToMap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport|Geo")
	float NorthOffsetDeg = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport")
	float ElevationFt = 0.f;

	/** Map location of a WGS84 position (Z = MapOrigin.Z). Local tangent plane: a few centimetres off within 10 km. */
	UFUNCTION(BlueprintPure, Category = "Airport|Geo")
	FVector GeoToMap(double InLatitude, double InLongitude) const;

	UFUNCTION(BlueprintPure, Category = "Airport|Geo")
	void MapToGeo(const FVector& Location, double& OutLatitude, double& OutLongitude) const;

	/** The same projection without an asset (editor tools fit a reference first). */
	static FVector ProjectGeo(double OriginLatitude, double OriginLongitude, float NorthYawDeg, const FVector& Origin, double InLatitude, double InLongitude);
	static void UnprojectGeo(double OriginLatitude, double OriginLongitude, float NorthYawDeg, const FVector& Origin, const FVector& Location,
		double& OutLatitude, double& OutLongitude);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport")
	TArray<FTowerPosition> TowerPositions;

	/** Plain Play (no scenario) puts the player in this tower. Empty = the map's own player start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport")
	FGameplayTag PlayTower;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport")
	TArray<FRunwayInfo> Runways;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airport")
	TSoftObjectPtr<UWeatherPresetData> DefaultWeather;

	const FTowerPosition* FindTower(const FGameplayTag& Tag) const;

	/** The airport whose Map is this world (also in Play in Editor); nullptr when no airport uses it. */
	static UAirportData* FindForWorld(const UWorld* World);
};
