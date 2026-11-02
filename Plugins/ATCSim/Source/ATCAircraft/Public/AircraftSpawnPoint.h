#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "AircraftSpawnSubsystem.h"
#include "AircraftSpawnPoint.generated.h"

class UArrowComponent;
class UBillboardComponent;

/**
 * A place on a taxi route where traffic enters, for example "arrivals from runway 18R" or "queue for 36L".
 * Drag it next to a route: in the editor it snaps onto the spline and points along it. At runtime it sends out
 * aircraft once or every IntervalSeconds, through the spawn manager (which waits when the place is taken).
 */
UCLASS(Blueprintable)
class ATCAIRCRAFT_API AAircraftSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AAircraftSpawnPoint();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** ID for scenarios and ATC.Spawn, for example Spawn.EHAM.Rwy36L. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	FGameplayTag SpawnPointTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TObjectPtr<ATaxiRoute> Route;

	/** Where the point stands on the route (main gear of the new aircraft), from its position. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (Units = "m"))
	float StartDistanceM = 0.f;

	/** Stop or waypoint where the taxi ends. Empty = EndDistanceM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FName EndStop;

	/** < 0 = end of the route. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (Units = "m"))
	float EndDistanceM = -1.f;

	/** Which aircraft come from here. Type overrides it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TObjectPtr<UTrafficMixData> TrafficMix;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TObjectPtr<UAircraftTypeData> Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TSubclassOf<AATCAircraft> AircraftClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bStartTaxi = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bRemoveAtRouteEnd = true;

	/** Start sending out aircraft at Play. Off: only a scenario, ATC.Spawn or Blueprint starts it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Stream")
	bool bSpawnOnBeginPlay = false;

	/** How many aircraft per stream; 0 = keep going. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Stream", meta = (ClampMin = "0"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Stream", meta = (Units = "s", ClampMin = "1"))
	float IntervalSeconds = 90.f;

	/** The request this point makes; Blueprints can change it before passing it to the spawn manager. */
	UFUNCTION(BlueprintPure, Category = "ATC|Spawn")
	FAircraftSpawnRequest MakeRequest() const;

	/** One aircraft now (or queued). Empty callsign / no type = from the traffic mix. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Spawn")
	FName SpawnOne(FName Callsign = NAME_None, UAircraftTypeData* TypeOverride = nullptr);

	/** InCount aircraft (0 = endless), the first now, then one every InIntervalSeconds. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Spawn")
	void StartStream(int32 InCount, float InIntervalSeconds);

	UFUNCTION(BlueprintCallable, Category = "ATC|Spawn")
	void StopStream();

private:
	void StreamTick();

	UPROPERTY(VisibleAnywhere, Category = "Spawn")
	TObjectPtr<UArrowComponent> Arrow;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif

	FTimerHandle StreamTimer;
	int32 StreamLeft = 0;
};
