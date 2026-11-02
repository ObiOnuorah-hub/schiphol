#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "TaxiRoute.generated.h"

class USplineComponent;
class UStaticMesh;
class UMaterialInterface;
class UBillboardComponent;
class UInstancedStaticMeshComponent;

UENUM(BlueprintType)
enum class ETaxiStopType : uint8
{
	HoldingPoint,
	StopBar,
	Intersection,
	Stand
};

/** Named point on a route, for example the crossing with taxiway A5. */
USTRUCT(BlueprintType)
struct FTaxiWaypoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	FName Name;

	/** cm along the route spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	float Distance = 0.f;
};

/** Position where the aircraft stops, for example a holding point before the runway. */
USTRUCT(BlueprintType)
struct FTaxiStop
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	FName Name;

	/** cm along the route spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	float Distance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	ETaxiStopType Type = ETaxiStopType::HoldingPoint;

	/** Seconds to wait before continuing on its own (only without clearance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	float HoldTime = 5.f;

	/** Waits for ContinueTaxi (clearance from the controller). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	bool bRequiresClearance = false;
};

/** Part of a route with a lower speed limit, for example the apron. */
USTRUCT(BlueprintType)
struct FSpeedZone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	float StartDistance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	float EndDistance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	float MaxSpeedKt = 10.f;
};

/** Marker that designers drag next to a route; the route turns it into a stop on the nearest spline distance. */
UCLASS()
class ATCAIRCRAFT_API ATaxiStopMarker : public AActor
{
	GENERATED_BODY()

public:
	ATaxiStopMarker();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	FTaxiStop Stop;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif
};

/** A taxi or pushback route: a spline with waypoints, stop positions and speed zones. */
UCLASS(Blueprintable)
class ATCAIRCRAFT_API ATaxiRoute : public AActor
{
	GENERATED_BODY()

public:
	ATaxiRoute();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return bDrawDebug; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Taxi")
	TObjectPtr<USplineComponent> Spline;

	/** ID for scenarios and commands, for example Route.EHAM.D7.Rwy36L. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Taxi")
	FGameplayTag RouteTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	TArray<FTaxiWaypoint> Waypoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	TArray<FTaxiStop> Stops;

	/** Markers placed in the level; added to the stops at their nearest distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	TArray<TObjectPtr<ATaxiStopMarker>> StopMarkers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	TArray<FSpeedZone> SpeedZones;

	/**
	 * The same trip over other taxiways, for example over the parallel taxiway: same start, same end. When an aircraft
	 * starts taxiing, the network gives it the lane with the least opposing traffic, otherwise the right-hand one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi")
	TArray<TObjectPtr<ATaxiRoute>> Alternatives;

	/**
	 * Made of other routes (taxiway pieces): the spline follows the legs one after another, each from where it meets
	 * the one before to where it meets the next, with a rounded turn at every join. Stops, waypoints and speed zones
	 * come from the legs and replace this route's own. Empty = an ordinary route that you draw yourself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Legs")
	TArray<TObjectPtr<ATaxiRoute>> Legs;

	/** Radius of the turn where two legs meet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Legs", meta = (Units = "cm"))
	float JoinRadiusCm = 3000.f;

	/**
	 * Arrival route that starts on a runway: paved runway behind the start (cm). Landing traffic touches down about
	 * 450 m past the runway end and rolls out to the start. 0 = unknown: a 900 m rollout.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Landing")
	float RunwayBeforeStartCm = 0.f;

	/** Yellow centerline along the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Visual")
	bool bShowCenterline = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Visual")
	TObjectPtr<UMaterialInterface> CenterlineMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Visual")
	float CenterlineWidthCm = 15.f;

	/** Stops, zones and waypoints drawn in the editor viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Visual")
	bool bDrawDebug = true;

	/**
	 * Green centerline lights along the route and a red stop bar at every stop that needs clearance. While an aircraft
	 * taxis the greens light up ahead of it up to the next stop bar ("follow the greens"); clearance switches the bar off.
	 * Pushback routes of a stand get no lights.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Lights")
	bool bShowLights = true;

	/** Build the lights when the first aircraft taxis here, not at Play: a big airport has many routes that wait unused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Lights")
	bool bLightsOnFirstUse = true;

	/** ICAO: 15 m on straight taxiways; turns get half of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Lights")
	float LightSpacingCm = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Lights")
	float StopBarWidthCm = 2300.f;

	/** How far ahead of the aircraft the greens are lit (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Lights")
	float GuidanceAheadCm = 40000.f;

	/** The stop bar lies on the waypoint with the stop's name (the setup tools put one on the painted line); otherwise this far past the stop (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Lights")
	float StopBarOffsetCm = 3000.f;

	/** Empty: /ATCSim/Lights/SM_TaxiLight and M_TaxiLight (made by ATC.CreateTaxiLights). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Lights")
	TObjectPtr<UStaticMesh> LightMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taxi|Lights")
	TObjectPtr<UMaterialInterface> LightMaterial;

	/** All lights in one draw call; custom data per light: on (0/1) and colour (RGB). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Taxi|Lights")
	TObjectPtr<UInstancedStaticMeshComponent> Lights;

	/**
	 * Follow the greens for one aircraft with its main gear at Distance; stops before ClearedStops are cleared.
	 * Several aircraft can use a route at once: each lights its own stretch.
	 */
	void ShowGuidance(const UObject* Aircraft, float Distance, int32 ClearedStops);

	/** The aircraft no longer taxis on this route: its greens go off and its bars on again. */
	void ClearGuidance(const UObject* Aircraft);

	UFUNCTION(BlueprintCallable, Category = "Taxi")
	void SetShowLights(bool bShow);

	UFUNCTION(BlueprintPure, Category = "Taxi")
	float GetLength() const;

	UFUNCTION(BlueprintPure, Category = "Taxi")
	FVector GetDirectionAt(float Distance) const;

	/** Location at a distance (world); before the start and past the end the route goes on in a straight line. */
	UFUNCTION(BlueprintPure, Category = "Taxi")
	FVector GetLocationAt(float Distance) const;

	/**
	 * Arrival route that starts on its runway: where the main gear is ClearCm (90 m, the holding position distance) from
	 * the runway centre line, so a landing aircraft past it is clear of the runway. The route length when it never gets there.
	 */
	UFUNCTION(BlueprintPure, Category = "Taxi")
	float GetRunwayVacatedCm(float ClearCm = 9000.f) const;

	/** A route along these routes after each other (its Legs), made for one trip; it is not saved with the map. */
	UFUNCTION(BlueprintCallable, Category = "Taxi", meta = (WorldContext = "WorldContext"))
	static ATaxiRoute* JoinRoutes(UObject* WorldContext, const TArray<ATaxiRoute*>& InLegs);

	/** Lowest zone limit at a distance in kt; a large value when no zone applies. */
	UFUNCTION(BlueprintPure, Category = "Taxi")
	float GetZoneLimitKt(float Distance) const;

	/** Index of the speed zone at a distance, INDEX_NONE outside zones. */
	int32 GetZoneIndexAt(float Distance) const;

	/** Distance (cm) of the stop or waypoint with this name; stops first. -1 when there is none. */
	UFUNCTION(BlueprintPure, Category = "Taxi")
	float FindNamedDistance(FName Name) const;

	/** Stops and markers together, sorted by distance. */
	UFUNCTION(BlueprintPure, Category = "Taxi")
	const TArray<FTaxiStop>& GetAllStops() const { return ResolvedStops; }

	/** Rebuilds stops from markers and the centerline. Call after changing the spline from code. */
	UFUNCTION(BlueprintCallable, Category = "Taxi")
	void Rebuild();

private:
	void BuildFromLegs();
	void ResolveStops();
	void BuildCenterline();
	void BuildLights();
	void SetLightOn(int32 Index, bool bOn);
	void RefreshLights();

	UPROPERTY(Transient)
	TArray<FTaxiStop> ResolvedStops;

	struct FStopBar
	{
		int32 StopIndex = 0;
		float Distance = 0.f;
		int32 FirstLight = 0;
		int32 NumLights = 0;
	};

	/** What one aircraft wants: greens [First, Last) on and the bars it is cleared through off. */
	struct FGuidance
	{
		int32 First = 0;
		int32 Last = 0;
		uint64 BarsOff = 0;
	};

	/** Distance of every centerline light (the first instances), ascending. The stop bar lights come after them. */
	TArray<float> LightDistances;
	TArray<FStopBar> StopBars;
	TArray<bool> LightOn;
	TMap<TWeakObjectPtr<const UObject>, FGuidance> Guidance;
	bool bLightsBuilt = false;
};
