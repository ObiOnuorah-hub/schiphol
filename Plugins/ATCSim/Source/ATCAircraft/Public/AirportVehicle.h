#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AirportVehicle.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EAirportVehicleKind : uint8
{
	/** Towbarless tug: the nose wheel sits in it during the pushback. */
	PushbackTug,
	/** Baggage tractor with two carts. */
	BaggageTrain,
	FuelTruck,
	/** Catering truck: its box goes up to the door. */
	CateringTruck
};

/**
 * A small airport vehicle, built from simple shapes in its own colours, so it needs no content: pushback tug, baggage
 * train, fuel truck or catering truck. It drives through a few points like a car (slow in turns, a turn rate, the carts
 * following its track), parks, and can be removed at the end of its drive. Only ticks while it drives or lifts.
 *
 * UGroundServiceSubsystem sends them to parked aircraft; UPushbackControllerComponent uses APushbackTug.
 */
UCLASS(Blueprintable)
class ATCAIRCRAFT_API AAirportVehicle : public AActor
{
	GENERATED_BODY()

public:
	AAirportVehicle();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** What it is; builds its body again when it already has one. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Vehicle")
	void SetKind(EAirportVehicleKind NewKind);

	/**
	 * Drive through Points (world; the height is taken from the vehicle), stop exactly on the last one and face EndYawDeg.
	 * The first ReversePoints points are driven backwards (a truck backing away from an aircraft door).
	 * bRemoveAtEnd: it is gone when it gets there. A catering box that is up comes down first.
	 */
	void DriveThrough(const TArray<FVector>& Points, float EndYawDeg, bool bRemoveAtEnd, int32 ReversePoints = 0);

	/** At once on this spot, standing still (the carts straight behind). */
	void Place(const FVector& Location, float YawDeg);

	UFUNCTION(BlueprintPure, Category = "ATC|Vehicle")
	bool IsDriving() const { return bDriving; }

	/** Catering truck: the box goes up this far (cm) to the aircraft door, 0 = down. */
	void SetLift(float HeightCm);

	/** Front to back, carts included (cm). */
	float GetLengthCm() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ATC|Vehicle")
	EAirportVehicleKind Kind = EAirportVehicleKind::BaggageTrain;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Vehicle", meta = (Units = "km/h"))
	float MaxSpeedKmh = 25.f;

	/** Fastest turn when it goes slowly; faster it turns on a wider circle, like a car. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Vehicle")
	float TurnRateDegS = 60.f;

	/** Rotating amber beacon while it drives (a small light, no shadows). atc.VehicleBeacons 0 switches them off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Vehicle")
	bool bBeacon = true;

private:
	void Build();
	void ClearBody();
	UStaticMeshComponent* AddPart(UStaticMesh* Mesh, USceneComponent* Parent, const FVector& Center, const FVector& SizeCm, const FLinearColor& Color,
		bool bShadow, const FRotator& Rotation = FRotator::ZeroRotator);
	UMaterialInterface* GetMaterial(const FLinearColor& Color);
	void TickDrive(float DeltaSeconds);
	void FinishDrive();
	void UpdateCarts(bool bStraight);
	FVector PointBehindHitch(float BackCm) const;
	FVector GetHitch() const;
	void UpdateTickEnabled();

	UPROPERTY(VisibleAnywhere, Category = "ATC|Vehicle")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Body;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Carts;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> LiftBox;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Beacon;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ShapeMaterial;

	TArray<FVector> Route;
	int32 NextPoint = 0;
	/** Points before this index are driven backwards. */
	int32 ReverseUntil = 0;
	float EndYaw = 0.f;
	bool bDriving = false;
	bool bTurningOnSpot = false;
	bool bRemoveAtEnd = false;
	float Speed = 0.f;

	/** Where the hitch was, oldest first, a little apart: the carts follow this track. */
	TArray<FVector> Trail;

	float LiftHeight = 0.f;
	float LiftTarget = 0.f;
	float LiftBaseZ = 0.f;
	float BeaconPhase = 0.f;
	bool bBuilt = false;
};

/** The vehicle a pushback uses: UPushbackControllerComponent's default TugClass. */
UCLASS()
class ATCAIRCRAFT_API APushbackTug : public AAirportVehicle
{
	GENERATED_BODY()

public:
	APushbackTug();
};
