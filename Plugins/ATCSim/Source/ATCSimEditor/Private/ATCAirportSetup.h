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

	/** A parking place: the stretch of a stand's lead-in line where the aircraft stands (from the nose stop back). */
	struct FStandZone
	{
		FString Ref;
		FVector Nose = FVector::ZeroVector;
		FVector Back = FVector::ZeroVector;
		/** A taxi line closer than this to the stretch runs through the parking place. */
		float ReachCm = 800.f;
		/** The nodes of the site's network in it. */
		TArray<int32> Nodes;

		bool Contains(const FVector& Point) const
		{
			return FMath::PointDistToSegment(FVector(Point.X, Point.Y, 0.0), Nose, Back) < ReachCm;
		}
	};

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
		/** The parking places (AddStandZones): a route only goes through its own. Empty = no such rule. */
		TArray<FStandZone> Stands;
		/**
		 * A cost factor per network node (AddSideLines): 1 on a centreline, 30 on a line beside one (a taxiway edge
		 * line), 10 on paint away from every taxiway. Empty = not known (the network's own LineCost is all there is).
		 */
		TArray<float> SideCost;

		bool IsSideLine(int32 Node) const { return SideCost.IsValidIndex(Node) && SideCost[Node] > 1.f; }
	};

	/**
	 * Which painted lines are centrelines: the OSM taxiways, taxilanes and lead-in lines are drawn along the real
	 * centrelines, so of two painted lines that run side by side the one nearer the OSM line is the centreline and the
	 * other a taxiway edge line. Aircraft follow the centreline: the route builder stays off the others (Site.SideCost).
	 */
	void AddSideLines(FSite& Site);

	/** Straight on over junctions where the painted centreline stops and goes on beyond, along an OSM taxiway. */
	void AddStraightGaps(FSite& Site);

	/**
	 * The parking places of the airport: every OSM parking position from its nose end (the end away from the taxiways)
	 * back over 65 % of its lead-in line, and every stand in the world (20 m ahead of the main gear to 15 m behind).
	 */
	void AddStandZones(FSite& Site, UWorld* World);

	/**
	 * A cost factor per network node for FNetwork::FindPath: 1, and Factor in every parking place that is not the
	 * route's own (the zone with OwnRef, and any zone within 25 m of OwnSpot). Lead-in lines of stands that stand nose
	 * to nose make a short cut between two taxilanes otherwise, straight through both parking places.
	 */
	TArray<float> StandCosts(const FSite& Site, const FString& OwnRef, const FVector& OwnSpot, float Factor = 100.f);

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
		/** Narrow-body gates: the arrival from the Oostbaan (22) for a late sidestep from the 27 final (AddSidestep). */
		ATaxiRoute* SidestepRoute = nullptr;
		/** Build the arrival with the narrow-body runway exit although the type is a wide-body (the 787's early exit). */
		bool bEarlyExit = false;
		/** Wide-body gates: the arrival over the early (narrow-body) exit, for the 787 (AddEarlyExit). */
		ATaxiRoute* EarlyExitRoute = nullptr;
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
