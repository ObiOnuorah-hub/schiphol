#pragma once

#include "CoreMinimal.h"
#include "AirportData.h"
#include "ATCOsm.h"
#include "GameplayTagContainer.h"

class AStand;
class ATaxiRoute;
class UAircraftTypeData;
class UScenarioData;
class UWorld;

namespace ATCMapSurvey
{
	struct FNetwork;
}

namespace ATCSetup
{
	/** Geo reference being fitted: the same projection as UAirportData::GeoToMap. */
	struct FGeo
	{
		double Latitude = 0.0;
		double Longitude = 0.0;
		float NorthYawDeg = 0.f;
		FVector Origin = FVector::ZeroVector;

		/** LatLon: X = latitude, Y = longitude. */
		FVector ToMap(const FVector2D& LatLon) const;
	};

	/** How an OSM runway lies against the evidence in the map. */
	struct FRunwayFit
	{
		FString Ref;
		double AngleDeg = 0.0;
		double OffsetCm = 0.0;
		double SpreadCm = 0.0;
		double LengthCm = 0.0;
		FVector2D Normal = FVector2D::ZeroVector;
		int32 Points = 0;
	};

	TArray<FRunwayFit> MeasureRunways(const FGeo& Geo, const TArray<ATCOsm::FRunway>& Runways, const TArray<FVector>& Evidence);

	/** Rotation (optional) and translation that put the OSM runways on the evidence. */
	FGeo FitGeo(FGeo Geo, const TArray<ATCOsm::FRunway>& Runways, const TArray<FVector>& Evidence, bool bRotate);

	/** A runway in map coordinates: A and B are the ends of the paved runway. */
	struct FMapRunway
	{
		FString Ref;
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		double HalfWidthCm = 2250.0;

		FVector Along() const { return (B - A).GetSafeNormal2D(); }
		double Length() const { return FVector::Dist2D(A, B); }
		/** Distance along the centerline from A, and sideways from it (cm, always positive). */
		void Locate(const FVector& Point, double& OutAlong, double& OutSide) const;
	};

	/** OSM runways in the map; with evidence (runway paint) each one is shifted sideways onto it. */
	TArray<FMapRunway> MapRunways(const FGeo& Geo, const TArray<ATCOsm::FRunway>& Runways, const TArray<FVector>& Evidence);

	/** Everything the route builder needs to know about one airport map. */
	struct FSite
	{
		FString ICAO;
		FGeo Geo;
		ATCOsm::FData Osm;
		TArray<FMapRunway> Runways;
		const ATCMapSurvey::FNetwork* Network = nullptr;
		/** Network made here (from OSM lines) instead of the map's paint. */
		TSharedPtr<ATCMapSurvey::FNetwork> OwnedNetwork;
		/** Painted runway-holding position markings or OSM holding positions; empty = stop 90 m from the runway centerline. */
		TArray<FVector> HoldMarkings;
		/** OSM aprons in the map; the apron speed limit ends where a route leaves them. */
		TArray<TArray<FVector2D>> Aprons;
	};

	/** OSM holding positions (as hold markings) and aprons. */
	void AddOsmHoldingAndAprons(FSite& Site);

	/** Route network from the OSM taxiway, taxilane and parking position lines, for maps without painted lines. */
	void BuildOsmNetwork(FSite& Site, TFunctionRef<double(const FVector&)> GroundZ);

	/** One departure: stand (OSM ref), pushback and taxi route to the runway, built from the network. */
	struct FDeparture
	{
		FString StandRef;
		FString Runway;
		UAircraftTypeData* Type = nullptr;
		FName StandTag;
		FName PushRouteTag;
		FName TaxiRouteTag;

		FTransform Parking;
		TArray<FVector> Push;
		TArray<FVector> Taxi;
		/** North, East, South or West: where the nose points after the pushback. */
		FString Face;
		FVector ApronEnd = FVector::ZeroVector;
		int32 RunwayIndex = INDEX_NONE;

		AStand* Stand = nullptr;
		ATaxiRoute* PushRoute = nullptr;
		ATaxiRoute* TaxiRoute = nullptr;

		/** Arrival on this landing runway (for example "36C"), from its exit to the stand. Empty = no arrival route. */
		FString ArrivalRunway;
		FName ArrivalRouteTag;
		TArray<FVector> Arrival;
		/** The arrival route enters the apron here: 10 kt from this point to the stand. */
		FVector ArrivalApronStart = FVector::ZeroVector;
		int32 ArrivalRunwayIndex = INDEX_NONE;
		ATaxiRoute* ArrivalRoute = nullptr;
	};

	bool BuildDeparture(const FSite& Site, FDeparture& Departure);

	/** Arrival route: rollout on the landing runway, the first exit towards the stand after 1.4 km, then taxi in, nose-in on the stand. */
	bool BuildArrival(const FSite& Site, FDeparture& Departure);

	/** Stand, pushback route and taxi route actors (outliner folder ATC, actor tag ATCSetup). */
	bool SpawnDeparture(UWorld* World, const FSite& Site, FDeparture& Departure);

	/** Removes what an earlier setup run placed (actor tag ATCSetup). */
	void RemoveSetupActors(UWorld* World);

	/** Top-down check picture of the network, runways and departures. */
	void PlotSite(const FSite& Site, const TArray<FDeparture>& Departures, const FString& Name);

	/** DA_Airport_<ICAO>: geo reference, map, towers and runways from the site. */
	UAirportData* UpdateAirport(const FSite& Site, const FString& Name, const FString& MapPackage, const TArray<FTowerPosition>& Towers, float ElevationFt);

	/** DA_Scenario_<ICAO>_Vertrek: one departure seen from the tower, pushback after 5 s. */
	UScenarioData* MakeDepartureScenario(const FSite& Site, const FDeparture& Departure, const TCHAR* Callsign, const FGameplayTag& Tower, const FString& Title);

	/** Copy of a map file in Saved/Backup_before_ATCSetup, made once before the first setup run changes it. */
	void BackupMapOnce(const FString& PackageName);
}
