#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AirportVehicle.h"
#include "GroundServiceSubsystem.generated.h"

class AATCAircraft;
class UAircraftTypeData;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/** Where one service vehicle parks at an aircraft, relative to its main gear (X to the nose, Y to the right wing). */
struct ATCAIRCRAFT_API FServiceSpot
{
	EAirportVehicleKind Kind = EAirportVehicleKind::BaggageTrain;
	FVector Location = FVector::ZeroVector;
	/** Yaw relative to the aircraft: 180 = facing the tail. */
	float YawDeg = 180.f;
	/** Catering: how far the box goes up to the door. */
	float LiftCm = 0.f;
};

/**
 * Ground handling around parked aircraft: a baggage train at the hold, a fuel truck under the wing and a catering truck at
 * the rear door. They come when an aircraft has parked on its stand, stay during the turnaround (fuel and catering leave
 * earlier) and drive away as soon as the pushback is asked for. All on the right-hand side, away from the jet bridges,
 * on paths that keep clear of the engines and of the neighbouring stands. Aircraft already parked when the map starts
 * get theirs at once. UATCSimSettings: bGroundVehicles, MaxServiceVehicles, ServicedAtStartShare.
 */
UCLASS()
class ATCAIRCRAFT_API UGroundServiceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Off: every service vehicle is removed and pushbacks get no tug. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Vehicles")
	void SetEnabled(bool bEnable);

	UFUNCTION(BlueprintPure, Category = "ATC|Vehicles")
	bool IsEnabled() const { return bEnabled; }

	/** Service vehicles on the airport now (pushback tugs not counted). */
	UFUNCTION(BlueprintPure, Category = "ATC|Vehicles")
	int32 GetVehicleCount() const;

	/** One line per aircraft that is being served, for ATC.Vehicles. */
	FString Describe() const;

	/** The basic shape material in this colour, one for the whole world: identical vehicle parts can be drawn together. */
	UMaterialInterface* GetColorMaterial(const FLinearColor& Color);

	/** The vehicles a turnaround of this type gets and where they park (tests and tools). */
	static TArray<FServiceSpot> PlanSpots(const UAircraftTypeData* Type, bool bFuel, bool bCatering);

	/** Beside the nose on the right, on the vehicles' lane: where service vehicles come from and go back to. */
	static FVector GetStagingPoint(const UAircraftTypeData* Type);

	/** The way in from the staging point to a spot (aircraft frame), ending on the spot along its heading. */
	static TArray<FVector> PathIn(const UAircraftTypeData* Type, const FServiceSpot& Spot);

	/** The way out: straight on, round outside the wing and back to the staging point. */
	static TArray<FVector> PathOut(const UAircraftTypeData* Type, const FServiceSpot& Spot);

	/** Parked on its stand and not about to leave: the moment for ground handling. */
	static bool IsParkedAtStand(const AATCAircraft* Aircraft);

private:
	enum class EPhase : uint8
	{
		Waiting,
		Coming,
		Parked,
		Leaving,
		Done
	};

	struct FServiceVehicle
	{
		FServiceSpot Spot;
		TWeakObjectPtr<AAirportVehicle> Vehicle;
		EPhase Phase = EPhase::Waiting;
		float ComeAt = 0.f;
		/** Leaves before the pushback (fuel, catering); a large value = stays until then. */
		float LeaveAt = 0.f;
	};

	struct FService
	{
		TWeakObjectPtr<AATCAircraft> Aircraft;
		TWeakObjectPtr<const UAircraftTypeData> Type;
		/** The aircraft on its stand: the vehicles keep to this frame also after it has gone. */
		FTransform Frame;
		TArray<FServiceVehicle> Vehicles;
		bool bLeaving = false;
	};

	void Poll();
	void StartService(AATCAircraft* Aircraft, bool bAtStart);
	void Send(FService& Service, FServiceVehicle& Entry, bool bPlaceAtOnce);
	void SendAway(FService& Service, FServiceVehicle& Entry);
	void RemoveAll();
	float Now() const;

	TArray<FService> Services;

	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> ColorMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ShapeMaterial;

	FRandomStream Random = FRandomStream(4711);
	float PollTimer = 0.f;
	/** World time of the first poll: aircraft parked then were there when the map started. */
	float FirstPollTime = -1.f;
	bool bEnabled = true;
};
