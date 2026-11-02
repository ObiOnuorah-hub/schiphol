#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ATCUnits.h"
#include "AircraftTypeData.generated.h"

class UStaticMesh;

/** ICAO aerodrome reference code, by wingspan (C = up to 36 m, E = up to 65 m). */
UENUM(BlueprintType)
enum class EAerodromeCode : uint8
{
	A, B, C, D, E, F
};

/** ICAO wake turbulence category. */
UENUM(BlueprintType)
enum class EWakeCategory : uint8
{
	Light, Medium, Heavy, Super
};

/** One airline's model of a type: aircraft whose callsign starts with Airline (KLM1427 -> KLM) get this mesh. */
USTRUCT(BlueprintType)
struct FAircraftLivery
{
	GENERATED_BODY()

	/** ICAO airline code, for example KLM, DLH or QTR. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FString Airline;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Same meaning as the type's MeshOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FTransform MeshOffset;
};

/** Dimensions and ground performance of one aircraft type. A new type is a new asset, no code. */
UCLASS(BlueprintType)
class ATCAIRCRAFT_API UAircraftTypeData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("AircraftType"), GetFName()); }

	/** ICAO type designator, for example B738 or A359. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	FName ICAOType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	EAerodromeCode AerodromeCode = EAerodromeCode::C;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	EWakeCategory WakeCategory = EWakeCategory::Medium;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dimensions")
	float WingspanM = 35.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dimensions")
	float LengthM = 39.5f;

	/** Nose gear to main gear. The tug attaches at the nose gear, the actor origin is the main gear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dimensions")
	float WheelbaseM = 15.6f;

	/** Model. Empty = simple placeholder sized from the dimensions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Puts the model with its main gear on the actor origin and the nose along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FTransform MeshOffset;

	/** Models of other airlines; an airline without one gets Mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TArray<FAircraftLivery> Liveries;

	/** The livery for a callsign; nullptr = the type's own Mesh. */
	const FAircraftLivery* FindLivery(FName Callsign) const
	{
		const FString Text = Callsign.ToString();
		return Liveries.FindByPredicate([&Text](const FAircraftLivery& Livery) { return !Livery.Airline.IsEmpty() && Text.StartsWith(Livery.Airline); });
	}

	/** Straight-line taxi speed. Turns are slowed down automatically by MaxLateralAccel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground performance")
	float MaxTaxiSpeedKt = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground performance")
	float PushbackSpeedKt = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground performance")
	float AccelerationMs2 = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground performance")
	float DecelerationMs2 = 1.f;

	/** Comfort limit in turns: v = sqrt(a / curvature). 0.5 m/s2 gives ~10 kt in a 50 m radius turn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground performance")
	float MaxLateralAccelMs2 = 0.5f;

	float GetMaxTaxiSpeedCms() const { return MaxTaxiSpeedKt * ATCUnits::KnotsToCms; }
	float GetPushbackSpeedCms() const { return PushbackSpeedKt * ATCUnits::KnotsToCms; }
	float GetAccelerationCms2() const { return AccelerationMs2 * ATCUnits::MetersToCm; }
	float GetDecelerationCms2() const { return DecelerationMs2 * ATCUnits::MetersToCm; }
	float GetMaxLateralAccelCms2() const { return MaxLateralAccelMs2 * ATCUnits::MetersToCm; }
	float GetWheelbaseCm() const { return WheelbaseM * ATCUnits::MetersToCm; }
	float GetLengthCm() const { return LengthM * ATCUnits::MetersToCm; }
	float GetHalfSpanCm() const { return WingspanM * ATCUnits::MetersToCm * 0.5f; }

	/** Main gear (the actor origin) to the nose tip: the nose gear sits about 12 % of the length behind the tip. */
	float GetNoseOffsetCm() const { return GetWheelbaseCm() + 0.12f * GetLengthCm(); }
};
