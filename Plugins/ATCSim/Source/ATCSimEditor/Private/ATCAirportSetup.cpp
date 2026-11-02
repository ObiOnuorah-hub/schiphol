// Real airport data in the maps. ATC.GeoFit lays the OpenStreetMap runways over what the map shows (runway paint or
// lights) and finds where north is and where the map origin lies; the setup commands use that to place stands and
// taxi routes, and the scenario that uses them.

#include "ATCAirportSetup.h"
#include "ATCExampleContent.h"
#include "ATCMapSurvey.h"
#include "ATCTowerScreens.h"
#include "AircraftMovementComponent.h"
#include "AircraftSpawnPoint.h"
#include "GMICamera.h"
#include "LensSetData.h"
#include "MonitorDisplay.h"
#include "PTZControllerComponent.h"
#include "RadioLogDisplay.h"
#include "TrafficDirector.h"
#include "TrafficMixData.h"
#include "AircraftTypeData.h"
#include "AirportData.h"
#include "ATCAircraft.h"
#include "ATCUnits.h"
#include "EnvironmentTypes.h"
#include "ScenarioData.h"
#include "Stand.h"
#include "TaxiRoute.h"
#include "Algo/Find.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameplayTagsManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "MeshDescription.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Engine/SkeletalMesh.h"
#include "StaticMeshAttributes.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace ATCSetup
{
	const FName SetupTag(TEXT("ATCSetup"));

	FGameplayTag Tag(const FName& Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(Name, true);
	}

	template<typename T>
	T* GetOrCreateAsset(const FString& Folder, const FString& Name);
	FScenarioEvent Event(float Seconds, const TCHAR* Callsign, const TCHAR* Command, const FString& Parameter = FString());

	FVector FGeo::ToMap(const FVector2D& LatLon) const
	{
		return UAirportData::ProjectGeo(Latitude, Longitude, NorthYawDeg, Origin, LatLon.X, LatLon.Y);
	}

	// ---------------------------------------------------------------- Fitting OSM onto the map

	TArray<FRunwayFit> MeasureRunways(const FGeo& Geo, const TArray<ATCOsm::FRunway>& Runways, const TArray<FVector>& Evidence)
	{
		TArray<FRunwayFit> Result;
		for (const ATCOsm::FRunway& Runway : Runways)
		{
			const FVector2D A(Geo.ToMap(Runway.A));
			const FVector2D B(Geo.ToMap(Runway.B));
			const double Length = FVector2D::Distance(A, B);
			const FVector2D Along = (B - A) / Length;
			const FVector2D Side(-Along.Y, Along.X);
			const double HalfWidth = Runway.WidthM * 50.0 + 2000.0;

			TArray<FVector2D> Points;
			for (const FVector& Point : Evidence)
			{
				const FVector2D D = FVector2D(Point) - A;
				const double T = D | Along;
				if (T > 0.0 && T < Length && FMath::Abs(D | Side) < HalfWidth)
				{
					Points.Add(FVector2D(Point));
				}
			}
			if (Points.Num() < 50)
			{
				continue;
			}

			// Principal axis of the evidence
			FVector2D Center = FVector2D::ZeroVector;
			for (const FVector2D& Point : Points)
			{
				Center += Point;
			}
			Center /= Points.Num();
			double XX = 0.0, XY = 0.0, YY = 0.0;
			for (const FVector2D& Point : Points)
			{
				const FVector2D D = Point - Center;
				XX += D.X * D.X;
				XY += D.X * D.Y;
				YY += D.Y * D.Y;
			}
			const double Angle = 0.5 * FMath::Atan2(2.0 * XY, XX - YY);
			FVector2D Axis(FMath::Cos(Angle), FMath::Sin(Angle));
			if ((Axis | Along) < 0.0)
			{
				Axis = -Axis;
			}
			const FVector2D AxisSide(-Axis.Y, Axis.X);
			double Spread = 0.0;
			for (const FVector2D& Point : Points)
			{
				Spread += FMath::Square((Point - Center) | AxisSide);
			}

			FRunwayFit Fit;
			Fit.Ref = Runway.Ref;
			Fit.AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(Along.X * Axis.Y - Along.Y * Axis.X, Along | Axis));
			Fit.OffsetCm = (Center - A) | Side;
			Fit.Normal = Side;
			Fit.Points = Points.Num();
			Fit.SpreadCm = FMath::Sqrt(Spread / Points.Num());
			Fit.LengthCm = Length;
			Result.Add(Fit);
		}
		return Result;
	}

	FGeo FitGeo(FGeo Geo, const TArray<ATCOsm::FRunway>& Runways, const TArray<FVector>& Evidence, bool bRotate)
	{
		for (int32 Iteration = 0; Iteration < 5; ++Iteration)
		{
			if (bRotate)
			{
				// Rotation: weighted mean angle between the OSM runways and the evidence
				double Sum = 0.0, Weight = 0.0;
				for (const FRunwayFit& Fit : MeasureRunways(Geo, Runways, Evidence))
				{
					Sum += Fit.AngleDeg * Fit.LengthCm;
					Weight += Fit.LengthCm;
				}
				if (Weight > 0.0)
				{
					Geo.NorthYawDeg += float(Sum / Weight);
				}
			}

			// Translation: least squares over the sideways offsets (runways in different directions fix both axes;
			// with one runway direction only the sideways offset is known)
			double M00 = 0.0, M01 = 0.0, M11 = 0.0, R0 = 0.0, R1 = 0.0;
			const TArray<FRunwayFit> Fits = MeasureRunways(Geo, Runways, Evidence);
			for (const FRunwayFit& Fit : Fits)
			{
				const double W = Fit.LengthCm;
				M00 += W * Fit.Normal.X * Fit.Normal.X;
				M01 += W * Fit.Normal.X * Fit.Normal.Y;
				M11 += W * Fit.Normal.Y * Fit.Normal.Y;
				R0 += W * Fit.Normal.X * Fit.OffsetCm;
				R1 += W * Fit.Normal.Y * Fit.OffsetCm;
			}
			const double Det = M00 * M11 - M01 * M01;
			FVector2D Shift = FVector2D::ZeroVector;
			if (Fits.Num() > 0 && FMath::Abs(Det) > 1e-6 * FMath::Square(M00 + M11))
			{
				Shift = FVector2D((M11 * R0 - M01 * R1) / Det, (M00 * R1 - M01 * R0) / Det);
			}
			else if (Fits.Num() > 0)
			{
				Shift = Fits[0].Normal * Fits[0].OffsetCm;
			}
			Geo.Origin += FVector(Shift, 0.0);
		}
		return Geo;
	}

	void LogFits(const TCHAR* Label, const FGeo& Geo, const TArray<ATCOsm::FRunway>& Runways, const TArray<FVector>& Evidence)
	{
		UE_LOG(LogATC, Display, TEXT("ATC.GeoFit %s: north yaw %.3f, map origin X=%.1f Y=%.1f m"), Label, Geo.NorthYawDeg, Geo.Origin.X / 100.0, Geo.Origin.Y / 100.0);
		for (const FRunwayFit& Fit : MeasureRunways(Geo, Runways, Evidence))
		{
			UE_LOG(LogATC, Display, TEXT("ATC.GeoFit %s:   runway %-8s %6d points, angle %+.3f deg, sideways %+.1f m, spread %.1f m"), Label, *Fit.Ref, Fit.Points,
				Fit.AngleDeg, Fit.OffsetCm / 100.0, Fit.SpreadCm / 100.0);
		}
	}

	/** ATC.GeoFit <ICAO> <filter> <lat> <lon> <north yaw> [norotate]: fit the OSM runways onto runway paint/lights in the open map. */
	void GeoFitCommand(const TArray<FString>& Args)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World || Args.Num() < 5)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.GeoFit <ICAO> <material or mesh filter> <lat> <lon> <north yaw> [norotate]"));
			return;
		}
		ATCOsm::FData Data;
		if (!Data.Load(Args[0]))
		{
			return;
		}
		const TArray<ATCOsm::FRunway> Runways = ATCOsm::Runways(Data);
		TArray<FVector> Evidence;
		const int32 Components = ATCMapSurvey::SampleMaterial(World, Args[1], 200.f, Evidence);
		UE_LOG(LogATC, Display, TEXT("ATC.GeoFit %s: %d runways in OSM, %d evidence points from %d components (%s)"), *Args[0], Runways.Num(), Evidence.Num(), Components, *Args[1]);

		FGeo Geo;
		Geo.Latitude = FCString::Atod(*Args[2]);
		Geo.Longitude = FCString::Atod(*Args[3]);
		Geo.NorthYawDeg = FCString::Atof(*Args[4]);
		LogFits(TEXT("before"), Geo, Runways, Evidence);
		Geo = FitGeo(Geo, Runways, Evidence, !(Args.Num() > 5 && Args[5] == TEXT("norotate")));
		LogFits(TEXT("after"), Geo, Runways, Evidence);

		// Picture: evidence grey, OSM runways red
		FBox Box(Evidence);
		for (const ATCOsm::FRunway& Runway : Runways)
		{
			Box += Geo.ToMap(Runway.A);
			Box += Geo.ToMap(Runway.B);
		}
		ATCMapSurvey::FPlot Plot(FBox2D(FVector2D(Box.Min), FVector2D(Box.Max)).ExpandBy(20000.0), 2000);
		Plot.Grid(50000.f, FColor(45, 50, 60), true);
		for (const FVector& Point : Evidence)
		{
			Plot.Point(Point, FColor(150, 150, 150), 0);
		}
		for (const ATCOsm::FRunway& Runway : Runways)
		{
			for (int32 Index = 1; Index < Runway.Line.Num(); ++Index)
			{
				Plot.Line(Geo.ToMap(Runway.Line[Index - 1]), Geo.ToMap(Runway.Line[Index]), FColor::Red, 1);
			}
		}
		Plot.Save(FString(TEXT("geofit_")) + Args[0]);
	}

	FAutoConsoleCommand GeoFitConsoleCommand(TEXT("ATC.GeoFit"),
		TEXT("ATC.GeoFit <ICAO> <material or mesh filter> <lat> <lon> <north yaw> [norotate]: fit OpenStreetMap runways onto the open map"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&GeoFitCommand));

	// ---------------------------------------------------------------- Runways in the map

	void FMapRunway::Locate(const FVector& Point, double& OutAlong, double& OutSide) const
	{
		const FVector Dir = Along();
		const FVector D = Point - A;
		OutAlong = D.X * Dir.X + D.Y * Dir.Y;
		OutSide = FMath::Abs(D.X * -Dir.Y + D.Y * Dir.X);
	}

	TArray<FMapRunway> MapRunways(const FGeo& Geo, const TArray<ATCOsm::FRunway>& Runways, const TArray<FVector>& Evidence)
	{
		const TArray<FRunwayFit> Fits = MeasureRunways(Geo, Runways, Evidence);
		TArray<FMapRunway> Result;
		for (const ATCOsm::FRunway& Runway : Runways)
		{
			FMapRunway Mapped;
			Mapped.Ref = Runway.Ref;
			Mapped.A = Geo.ToMap(Runway.A);
			Mapped.B = Geo.ToMap(Runway.B);
			Mapped.HalfWidthCm = Runway.WidthM * 50.0;
			for (const FRunwayFit& Fit : Fits)
			{
				if (!Fit.Ref.IsEmpty() && Fit.Ref == Runway.Ref)
				{
					// OSM is a few metres off; the paint in the map is what the controller sees
					const FVector Shift(Fit.Normal * Fit.OffsetCm, 0.0);
					Mapped.A += Shift;
					Mapped.B += Shift;
				}
			}
			Result.Add(Mapped);
		}
		return Result;
	}

	/** The runway end where a takeoff on Designator starts ("36L": the south end of 18R/36L) and the takeoff direction. */
	bool FindTakeoff(const FSite& Site, const FString& Designator, FVector& OutStart, FVector& OutDirection, int32& OutRunway)
	{
		const double Heading = FCString::Atoi(*Designator) * 10.0;
		for (int32 Index = 0; Index < Site.Runways.Num(); ++Index)
		{
			const FMapRunway& Runway = Site.Runways[Index];
			TArray<FString> Ends;
			Runway.Ref.ParseIntoArray(Ends, TEXT("/"));
			if (!Ends.Contains(Designator))
			{
				continue;
			}
			const FVector Along = Runway.Along();
			const double Bearing = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)) - Site.Geo.NorthYawDeg;
			const bool bFromA = FMath::Abs(FMath::FindDeltaAngleDegrees(Bearing, Heading)) < 90.0;
			OutStart = bFromA ? Runway.A : Runway.B;
			OutDirection = bFromA ? Along : -Along;
			OutRunway = Index;
			return true;
		}
		UE_LOG(LogATC, Error, TEXT("%s: runway %s not in the OSM data"), *Site.ICAO, *Designator);
		return false;
	}

	FString CompassWord(const FSite& Site, const FVector& Direction)
	{
		const double Bearing = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)) - Site.Geo.NorthYawDeg + 720.0, 360.0);
		static const TCHAR* Words[] = { TEXT("North"), TEXT("East"), TEXT("South"), TEXT("West") };
		return Words[FMath::RoundToInt(Bearing / 90.0) % 4];
	}

	// ---------------------------------------------------------------- Stands and departures

	struct FStandSpot
	{
		FVector NoseStop = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		FVector TailEnd = FVector::ZeroVector;
		TSet<int32> LineNodes;
		/** Not a painted or mapped lead-in line: the aircraft is pushed straight back to TailEnd first. */
		bool bSynthetic = false;
	};

	bool FindStand(const FSite& Site, const FString& Ref, FStandSpot& Out)
	{
		const ATCMapSurvey::FNetwork& Network = *Site.Network;

		// "@lat,lon,heading": a stand that is not in the data, nose at that point and heading (true degrees);
		// "#x,y,yaw": the same in map coordinates (cm, degrees), for example under a parked aircraft model of the map
		if (Ref.StartsWith(TEXT("@")) || Ref.StartsWith(TEXT("#")))
		{
			TArray<FString> Parts;
			Ref.RightChop(1).ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() != 3)
			{
				return false;
			}
			const bool bMap = Ref.StartsWith(TEXT("#"));
			const FVector Nose = bMap ? FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), 0.0)
				: Site.Geo.ToMap(FVector2D(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1])));
			const double Yaw = FMath::DegreesToRadians((bMap ? 0.0 : Site.Geo.NorthYawDeg) + FCString::Atod(*Parts[2]));
			Out.Direction = FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0);
			const int32 Near = Network.Nearest(Nose, 20000.f);
			Out.NoseStop = FVector(Nose.X, Nose.Y, Near != INDEX_NONE ? Network.Nodes[Near].Z : 0.0);
			Out.TailEnd = Out.NoseStop - Out.Direction * 4000.0;
			Out.bSynthetic = true;
			UE_LOG(LogATC, Display, TEXT("%s stand %s: placed by hand (not in OSM), nose stop X=%.1f Y=%.1f m, heading %s"), *Site.ICAO, *Ref,
				Out.NoseStop.X / 100.0, Out.NoseStop.Y / 100.0, *CompassWord(Site, Out.Direction));
			return true;
		}

		// The mapped lead-in line; OSM often has the stand twice, as a point and as a line: the line is used
		const ATCOsm::FElement* Element = nullptr;
		for (const ATCOsm::FElement* Candidate : Site.Osm.WithTag(TEXT("aeroway"), TEXT("parking_position")))
		{
			if (Candidate->Tag(TEXT("ref")) == Ref && (!Element || (Candidate->bWay && !Element->bWay)))
			{
				Element = Candidate;
			}
		}
		if (Element && !Element->bWay && Site.OwnedNetwork.IsValid())
		{
			// Without paint the lead-in line has to come from OSM
			UE_LOG(LogATC, Error, TEXT("%s: stand %s is only a point in OSM; give it as @lat,lon,heading"), *Site.ICAO, *Ref);
			return false;
		}
		if (!Element)
		{
			UE_LOG(LogATC, Error, TEXT("%s: stand %s not in the OSM data"), *Site.ICAO, *Ref);
			return false;
		}
		const FVector Center = Site.Geo.ToMap(Element->Center());
		const int32 Near = Network.Nearest(Center, 3000.f);
		if (Near == INDEX_NONE)
		{
			UE_LOG(LogATC, Error, TEXT("%s: no lead-in line near stand %s"), *Site.ICAO, *Ref);
			return false;
		}

		// The lead-in line: its own painted line. A network made from OSM lines is one part; there the stand line is
		// the set of nodes along the OSM parking position way
		TArray<int32> Line;
		if (!Site.OwnedNetwork.IsValid() && Network.LinePart[Near] != Network.MainLinePart)
		{
			Line = Network.LineMembers[Network.LinePart[Near]];
		}
		else
		{
			for (int32 Index = 1; Index < Element->Points.Num(); ++Index)
			{
				const FVector P0 = Site.Geo.ToMap(Element->Points[Index - 1]);
				const FVector P1 = Site.Geo.ToMap(Element->Points[Index]);
				TArray<int32> Nearby;
				Network.Query((P0 + P1) * 0.5, FVector::Dist2D(P0, P1) * 0.5 + 200.f, Nearby);
				for (int32 Node : Nearby)
				{
					if (FMath::PointDistToSegment(Network.Nodes[Node] * FVector(1, 1, 0), P0 * FVector(1, 1, 0), P1 * FVector(1, 1, 0)) < 150.f)
					{
						Line.AddUnique(Node);
					}
				}
			}
		}
		Out.LineNodes = TSet<int32>(Line);
		if (Line.Num() < 2)
		{
			UE_LOG(LogATC, Error, TEXT("%s: no lead-in line found for stand %s"), *Site.ICAO, *Ref);
			return false;
		}

		// Ends of the line along its main direction
		FVector Mean = FVector::ZeroVector;
		for (int32 Node : Line)
		{
			Mean += Network.Nodes[Node];
		}
		Mean /= FMath::Max(Line.Num(), 1);
		double XX = 0.0, XY = 0.0, YY = 0.0;
		for (int32 Node : Line)
		{
			const FVector D = Network.Nodes[Node] - Mean;
			XX += D.X * D.X;
			XY += D.X * D.Y;
			YY += D.Y * D.Y;
		}
		const double Angle = 0.5 * FMath::Atan2(2.0 * XY, XX - YY);
		const FVector Axis(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
		int32 Low = Line[0], High = Line[0];
		for (int32 Node : Line)
		{
			const double T = (Network.Nodes[Node] - Mean) | Axis;
			Low = T < ((Network.Nodes[Low] - Mean) | Axis) ? Node : Low;
			High = T > ((Network.Nodes[High] - Mean) | Axis) ? Node : High;
		}

		// The nose end is the one away from other paint: the other end points at the taxilane
		auto Clearance = [&](int32 End)
		{
			TArray<int32> Nearby;
			Network.Query(Network.Nodes[End], 10000.f, Nearby);
			double Best = 10000.0;
			for (int32 Node : Nearby)
			{
				if (!Out.LineNodes.Contains(Node))
				{
					Best = FMath::Min(Best, FVector::Dist2D(Network.Nodes[Node], Network.Nodes[End]));
				}
			}
			return Best;
		};
		bool bHighIsNose = Clearance(High) > Clearance(Low);

		// On an apron grid from OSM every end touches other lines; there the nose is the end nearest a building
		// (aircraft park nose-in at the terminal)
		if (Site.OwnedNetwork.IsValid())
		{
			auto BuildingDistance = [&Site](const FVector& Point)
			{
				double Best = 20000.0;
				for (const ATCOsm::FElement& Building : Site.Osm.Elements)
				{
					if (!Building.bWay || !Building.IsClosed() || (Building.Tag(TEXT("building")).IsEmpty() && Building.Tag(TEXT("aeroway")) != TEXT("terminal")))
					{
						continue;
					}
					for (int32 Index = 1; Index < Building.Points.Num(); ++Index)
					{
						const FVector A = Site.Geo.ToMap(Building.Points[Index - 1]) * FVector(1, 1, 0);
						const FVector B = Site.Geo.ToMap(Building.Points[Index]) * FVector(1, 1, 0);
						Best = FMath::Min(Best, double(FMath::PointDistToSegment(Point * FVector(1, 1, 0), A, B)));
					}
				}
				return Best;
			};
			const double HighToBuilding = BuildingDistance(Network.Nodes[High]);
			const double LowToBuilding = BuildingDistance(Network.Nodes[Low]);
			if (FMath::Abs(HighToBuilding - LowToBuilding) > 1000.0)
			{
				bHighIsNose = HighToBuilding < LowToBuilding;
			}
		}
		Out.NoseStop = Network.Nodes[bHighIsNose ? High : Low];
		Out.TailEnd = Network.Nodes[bHighIsNose ? Low : High];
		Out.Direction = (Out.NoseStop - Out.TailEnd).GetSafeNormal2D();
		UE_LOG(LogATC, Display, TEXT("%s stand %s: lead-in %d nodes, %.0f m, nose stop X=%.1f Y=%.1f m, heading %s"), *Site.ICAO, *Ref, Line.Num(),
			FVector::Dist2D(Out.NoseStop, Out.TailEnd) / 100.0, Out.NoseStop.X / 100.0, Out.NoseStop.Y / 100.0, *CompassWord(Site, Out.Direction));
		return true;
	}

	/** Point in polygon (ray casting). */
	bool IsInside(const TArray<FVector2D>& Polygon, const FVector2D& Point)
	{
		bool bInside = false;
		for (int32 I = 0, J = Polygon.Num() - 1; I < Polygon.Num(); J = I++)
		{
			const FVector2D& A = Polygon[I];
			const FVector2D& B = Polygon[J];
			if ((A.Y > Point.Y) != (B.Y > Point.Y) && Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	TArray<FVector> ToPoints(const ATCMapSurvey::FNetwork& Network, const TArray<int32>& Path)
	{
		TArray<FVector> Points;
		for (int32 Node : Path)
		{
			Points.Add(Network.Nodes[Node]);
		}
		return Points;
	}

	bool BuildDeparture(const FSite& Site, FDeparture& D)
	{
		const ATCMapSurvey::FNetwork& Network = *Site.Network;
		FStandSpot Stand;
		if (!D.Type || !FindStand(Site, D.StandRef, Stand))
		{
			return false;
		}
		const float Wheelbase = D.Type->WheelbaseM * ATCUnits::MetersToCm;
		const FVector MainGear = Stand.NoseStop - Stand.Direction * Wheelbase;
		D.Parking = FTransform(Stand.Direction.Rotation(), MainGear);

		FVector RunwayStart, RunwayDirection;
		if (!FindTakeoff(Site, D.Runway, RunwayStart, RunwayDirection, D.RunwayIndex))
		{
			return false;
		}
		const FMapRunway& Runway = Site.Runways[D.RunwayIndex];

		// The route starts at the end of the lead-in line that points at the taxilane
		const int32 Tail = Network.Nearest(Stand.TailEnd, Stand.bSynthetic ? 5000.f : 500.f);
		if (Tail == INDEX_NONE)
		{
			UE_LOG(LogATC, Error, TEXT("%s: stand %s is not near any taxi line"), *Site.ICAO, *D.StandRef);
			return false;
		}

		// Goal: the centerline that comes closest to the runway near its start, on the stand's side of the runway. A
		// taxiway around the runway end connects both sides, but lining up from the far side would meet the departures
		// from this side head-on at the holding point
		TArray<int32> Nearby;
		Network.Query(RunwayStart + RunwayDirection * 20000.0, 40000.f, Nearby);
		int32 Goal = INDEX_NONE;
		double BestScore = TNumericLimits<double>::Max();
		const FVector FromStand = MainGear - RunwayStart;
		const double StandSide = FromStand.X * -RunwayDirection.Y + FromStand.Y * RunwayDirection.X;
		for (int32 Node : Nearby)
		{
			const FVector Delta = Network.Nodes[Node] - RunwayStart;
			if (Network.Part[Node] != Network.Part[Tail] || Network.LineCost(Node) > 1.f
				|| (Delta.X * -RunwayDirection.Y + Delta.Y * RunwayDirection.X) * StandSide < 0.0)
			{
				continue;
			}
			const double Along = Delta.X * RunwayDirection.X + Delta.Y * RunwayDirection.Y;
			const double Side = FMath::Abs(Delta.X * -RunwayDirection.Y + Delta.Y * RunwayDirection.X);
			const double Score = Side + 0.25 * FMath::Abs(Along - 3000.0);
			if (Along > -5000.0 && Score < BestScore)
			{
				BestScore = Score;
				Goal = Node;
			}
		}
		if (Goal == INDEX_NONE)
		{
			UE_LOG(LogATC, Error, TEXT("%s: no taxiway paint near the start of runway %s"), *Site.ICAO, *D.Runway);
			return false;
		}

		// First the route from the end of the lead-in: where it joins the taxilane and which way it goes from there
		const TArray<int32> First = Network.FindPath(Tail, Goal);
		if (First.IsEmpty())
		{
			UE_LOG(LogATC, Error, TEXT("%s: no route from stand %s to runway %s (stand line end node %d in part %d, goal in part %d; main part %d)"), *Site.ICAO,
				*D.StandRef, *D.Runway, Tail, Tail != INDEX_NONE ? Network.Part[Tail] : -1, Network.Part[Goal], Network.MainPart);
			return false;
		}
		int32 JoinIndex = 0;
		while (JoinIndex < First.Num() - 1 && Stand.LineNodes.Contains(First[JoinIndex]))
		{
			++JoinIndex;
		}
		// Past the gap between lead-in and taxilane (a bridge), the first painted node of the taxilane
		const FVector Join = Network.Nodes[First[JoinIndex]];
		FVector Ahead = Join;
		double Walked = 0.0;
		for (int32 Index = JoinIndex + 1; Index < First.Num(); ++Index)
		{
			Walked += FVector::Dist2D(Network.Nodes[First[Index]], Network.Nodes[First[Index - 1]]);
			Ahead = Network.Nodes[First[Index]];
			if (Walked >= 4000.0)
			{
				break;
			}
		}
		const FVector TaxiDirection = (Ahead - Join).GetSafeNormal2D();
		D.Face = CompassWord(Site, TaxiDirection);

		// Pushback: tail first the other way along the taxilane, until the aircraft stands straight on it
		int32 PushEnd = Network.Nearest(Join - TaxiDirection * (Wheelbase + 3000.f), 2500.f);
		if (PushEnd == INDEX_NONE)
		{
			PushEnd = First[JoinIndex];
		}
		// A stand placed by hand has no line to follow: straight back to the taxilane first
		TArray<FVector> PushPoints = ToPoints(Network, Network.FindPath(Stand.bSynthetic ? Tail : Network.Nearest(MainGear, 1000.f), PushEnd));
		PushPoints.Insert(MainGear, 0);
		// Simplify: the nodes of painted lines zigzag a few decimetres, which the curve speed limit would read as bends
		D.Push = ATCMapSurvey::RoundCorners(ATCMapSurvey::Simplify(PushPoints, 50.f), 2000.f);

		// Taxi: from the end of the pushback to the runway, then onto the centerline and lined up
		const TArray<int32> TaxiPath = Network.FindPath(PushEnd, Goal);
		TArray<FVector> TaxiPoints = ToPoints(Network, TaxiPath);
		const FVector Last = TaxiPoints.Last();
		const double LastAlong = (Last - RunwayStart) | RunwayDirection;
		FVector OnCenterline = RunwayStart + RunwayDirection * LastAlong;
		OnCenterline.Z = Last.Z;
		if (FVector::Dist2D(OnCenterline, Last) > 500.0)
		{
			TaxiPoints.Add(OnCenterline);
		}
		TaxiPoints.Add(OnCenterline + RunwayDirection * 6000.0);
		D.Taxi = ATCMapSurvey::RoundCorners(ATCMapSurvey::Simplify(TaxiPoints, 80.f), 3500.f);

		// The apron ends where the route leaves the mapped aprons, or (paint) where it reaches the taxiway system
		D.ApronEnd = TaxiPoints[0];
		if (Site.Aprons.Num() > 0)
		{
			for (const FVector& Point : TaxiPoints)
			{
				bool bOnApron = false;
				for (const TArray<FVector2D>& Apron : Site.Aprons)
				{
					bOnApron |= IsInside(Apron, FVector2D(Point));
				}
				if (!bOnApron)
				{
					D.ApronEnd = Point;
					break;
				}
			}
		}
		else
		{
			for (int32 Node : TaxiPath)
			{
				if (Network.LinePart[Node] == Network.MainLinePart)
				{
					D.ApronEnd = Network.Nodes[Node];
					break;
				}
			}
		}

		double Length = 0.0;
		for (int32 Index = 1; Index < D.Taxi.Num(); ++Index)
		{
			Length += FVector::Dist2D(D.Taxi[Index - 1], D.Taxi[Index]);
		}
		UE_LOG(LogATC, Display, TEXT("%s %s -> %s: pushback %d points face %s, taxi %d points %.0f m"), *Site.ICAO, *D.StandRef, *D.Runway, D.Push.Num(),
			*D.Face, D.Taxi.Num(), Length / 100.0);
		return true;
	}

	bool BuildArrival(const FSite& Site, FDeparture& D)
	{
		const ATCMapSurvey::FNetwork& Network = *Site.Network;
		FStandSpot Stand;
		if (D.ArrivalRunway.IsEmpty() || !D.Type || !FindStand(Site, D.StandRef, Stand))
		{
			return false;
		}
		const float Wheelbase = D.Type->WheelbaseM * ATCUnits::MetersToCm;
		const FVector MainGear = Stand.NoseStop - Stand.Direction * Wheelbase;
		const int32 Tail = Network.Nearest(Stand.TailEnd, Stand.bSynthetic ? 5000.f : 500.f);
		const int32 Parked = Network.Nearest(MainGear, 1500.f);
		FVector Threshold, LandingDirection;
		if (Tail == INDEX_NONE || Parked == INDEX_NONE || !FindTakeoff(Site, D.ArrivalRunway, Threshold, LandingDirection, D.ArrivalRunwayIndex))
		{
			UE_LOG(LogATC, Warning, TEXT("%s: no arrival for %s on %s (stand line or runway missing)"), *Site.ICAO, *D.StandRef, *D.ArrivalRunway);
			return false;
		}
		const FMapRunway& Runway = Site.Runways[D.ArrivalRunwayIndex];

		// Exit: a taxiway centreline beside the runway 1.4 to 2.6 km past the threshold, connected to the stand.
		// Paint stops at the runway edge, so the exit is the line node closest to the runway in that stretch
		TArray<int32> Nearby;
		Network.Query(Threshold + LandingDirection * 200000.0, 160000.f, Nearby);
		int32 Exit = INDEX_NONE;
		double ExitAlong = 0.0;
		double BestScore = TNumericLimits<double>::Max();
		for (int32 Node : Nearby)
		{
			if (Network.Part[Node] != Network.Part[Tail] || Network.LineCost(Node) > 1.f)
			{
				continue;
			}
			const FVector Delta = Network.Nodes[Node] - Threshold;
			const double Along = Delta.X * LandingDirection.X + Delta.Y * LandingDirection.Y;
			const double Side = FMath::Abs(Delta.X * -LandingDirection.Y + Delta.Y * LandingDirection.X);
			if (Along < 140000.0 || Along > 260000.0 || Side < Runway.HalfWidthCm + 800.0 || Side > Runway.HalfWidthCm + 15000.0)
			{
				continue;
			}
			const double Score = Side + 0.1 * FMath::Abs(Along - 190000.0);
			if (Score < BestScore)
			{
				BestScore = Score;
				Exit = Node;
				ExitAlong = Along;
			}
		}
		if (Exit == INDEX_NONE)
		{
			UE_LOG(LogATC, Warning, TEXT("%s: no exit off runway %s towards stand %s"), *Site.ICAO, *D.ArrivalRunway, *D.StandRef);
			return false;
		}
		// To the far end of the stand's lead-in line and along it onto the stand: nose-in, also where another painted
		// line passes closer to the main gear than the lead-in does. Into the lead-in only at that far end: coming in
		// from the nose end and turning back would be a U-turn on the stand
		TArray<float> Cost;
		Cost.Init(1.f, Network.Nodes.Num());
		for (int32 Node : Stand.LineNodes)
		{
			Cost[Node] = Node == Tail ? 1.f : 1000.f;
		}
		const TArray<int32> Path = Network.FindPath(Exit, Tail, &Cost);
		if (Path.IsEmpty())
		{
			UE_LOG(LogATC, Warning, TEXT("%s: no taxi route from the %s exit to stand %s"), *Site.ICAO, *D.ArrivalRunway, *D.StandRef);
			return false;
		}

		// Rollout on the centreline, turn off towards the exit, taxi in, last metres straight onto the stand
		const FVector ExitPoint = Network.Nodes[Exit];
		const double ExitSide = FMath::Abs((ExitPoint - Threshold).X * -LandingDirection.Y + (ExitPoint - Threshold).Y * LandingDirection.X);
		auto OnCenterline = [&](double Along)
		{
			FVector Point = Threshold + LandingDirection * Along;
			Point.Z = ExitPoint.Z;
			return Point;
		};
		TArray<FVector> Points = { OnCenterline(ExitAlong - ExitSide * 1.5 - 40000.0), OnCenterline(ExitAlong - ExitSide * 1.5) };
		Points.Append(ToPoints(Network, Path));
		Points.Add(Stand.TailEnd + (MainGear - Stand.TailEnd).GetSafeNormal2D() * 1000.0);
		Points.Add(MainGear);
		D.Arrival = ATCMapSurvey::RoundCorners(ATCMapSurvey::Simplify(Points, 80.f), 3500.f);

		// The apron starts where the path leaves the main taxiway lines (the taxilane and lead-in)
		D.ArrivalApronStart = MainGear;
		for (int32 Index = Path.Num() - 1; Index >= 0; --Index)
		{
			if (Network.LinePart[Path[Index]] == Network.MainLinePart)
			{
				D.ArrivalApronStart = Network.Nodes[Path[Index]];
				break;
			}
		}

		double Length = 0.0;
		for (int32 Index = 1; Index < D.Arrival.Num(); ++Index)
		{
			Length += FVector::Dist2D(D.Arrival[Index - 1], D.Arrival[Index]);
		}
		if (Length > 3000000.0)
		{
			UE_LOG(LogATC, Warning, TEXT("%s %s -> %s: arrival of %.0f km is not a route, left out"), *Site.ICAO, *D.ArrivalRunway, *D.StandRef, Length / 100000.0);
			D.Arrival.Reset();
			return false;
		}
		UE_LOG(LogATC, Display, TEXT("%s %s -> %s: arrival, exit %.0f m past the threshold, %d points %.0f m"), *Site.ICAO, *D.ArrivalRunway, *D.StandRef,
			ExitAlong / 100.0, D.Arrival.Num(), Length / 100.0);
		return true;
	}

	/**
	 * Clearance stops before every runway on the route: crossings, and the holding point of the departure runway.
	 * An arrival starts on its landing runway: that one is skipped and every other runway is a crossing.
	 */
	void AddRunwayStops(const FSite& Site, const FDeparture& D, ATaxiRoute* Route, bool bArrival = false)
	{
		const USplineComponent* Spline = Route->Spline;
		const float Length = Spline->GetSplineLength();
		const float NoseOffset = (D.Type->WheelbaseM + 0.12f * D.Type->LengthM + 3.f) * ATCUnits::MetersToCm;

		// Where the route crosses a painted runway-holding position marking
		TMap<FIntPoint, TArray<FVector>> HoldGrid;
		for (const FVector& Point : Site.HoldMarkings)
		{
			HoldGrid.FindOrAdd(FIntPoint(FMath::FloorToInt(Point.X / 500.0), FMath::FloorToInt(Point.Y / 500.0))).Add(Point);
		}
		TArray<float> Bars;
		float LastHit = -1e9f;
		for (float Distance = 0.f; Distance < Length; Distance += 100.f)
		{
			const FVector P = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
			const FIntPoint Cell(FMath::FloorToInt(P.X / 500.0), FMath::FloorToInt(P.Y / 500.0));
			bool bHit = false;
			for (int32 DX = -1; DX <= 1 && !bHit; ++DX)
			{
				for (int32 DY = -1; DY <= 1 && !bHit; ++DY)
				{
					if (const TArray<FVector>* Points = HoldGrid.Find(Cell + FIntPoint(DX, DY)))
					{
						for (const FVector& Point : *Points)
						{
							bHit |= FVector::DistSquared2D(Point, P) < 150.f * 150.f;
						}
					}
				}
			}
			if (bHit)
			{
				if (Distance - LastHit > 1000.f)
				{
					Bars.Add(Distance);
				}
				LastHit = Distance;
			}
		}

		// Where the route enters a runway
		struct FEntry { float Distance; int32 Runway; };
		TArray<FEntry> Entries;
		for (int32 Index = 0; Index < Site.Runways.Num(); ++Index)
		{
			if (bArrival && Index == D.ArrivalRunwayIndex)
			{
				continue;
			}
			const FMapRunway& Runway = Site.Runways[Index];
			bool bWasInside = false;
			for (float Distance = 0.f; Distance <= Length; Distance += 100.f)
			{
				double Along, Side;
				Runway.Locate(Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World), Along, Side);
				const bool bInside = Along > 0.0 && Along < Runway.Length() && Side < Runway.HalfWidthCm + 1000.0;
				if (bInside && !bWasInside)
				{
					Entries.Add({ Distance, Index });
				}
				bWasInside = bInside;
			}
		}
		Entries.Sort([](const FEntry& A, const FEntry& B) { return A.Distance < B.Distance; });

		for (const FEntry& Entry : Entries)
		{
			const FMapRunway& Runway = Site.Runways[Entry.Runway];
			// The last painted marking before the runway; without paint 90 m from the centerline
			float StopLine = -1.f;
			for (float Bar : Bars)
			{
				if (Bar < Entry.Distance && Bar > Entry.Distance - 25000.f)
				{
					StopLine = FMath::Max(StopLine, Bar);
				}
			}
			if (StopLine < 0.f)
			{
				for (float Distance = Entry.Distance; Distance > 0.f; Distance -= 100.f)
				{
					double Along, Side;
					Runway.Locate(Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World), Along, Side);
					if (Side >= 9000.0)
					{
						StopLine = Distance;
						break;
					}
				}
			}
			const bool bDeparture = !bArrival && Entry.Runway == D.RunwayIndex && Entry.Distance > Length - 50000.f;

			FTaxiStop Stop;
			Stop.Name = FName(bDeparture ? FString::Printf(TEXT("Holding point %s"), *D.Runway) : FString::Printf(TEXT("Runway crossing %s"), *Runway.Ref));
			Stop.Type = ETaxiStopType::HoldingPoint;
			Stop.bRequiresClearance = true;
			Stop.HoldTime = 0.f;
			Stop.Distance = FMath::Max(StopLine - NoseOffset, 0.f);
			Route->Stops.Add(Stop);

			FTaxiWaypoint Waypoint;
			Waypoint.Name = Stop.Name;
			Waypoint.Distance = FMath::Max(StopLine, 0.f);
			Route->Waypoints.Add(Waypoint);
			UE_LOG(LogATC, Display, TEXT("%s %s: %s, stop line at %.0f m (%s), main gear stops at %.0f m"), *Site.ICAO, *D.TaxiRouteTag.ToString(),
				*Stop.Name.ToString(), StopLine / 100.0, Bars.Num() > 0 ? TEXT("holding position marking") : TEXT("90 m rule"), Stop.Distance / 100.0);
		}
	}

	ATaxiRoute* SpawnRoute(UWorld* World, const FName& RouteTag, const TArray<FVector>& Points)
	{
		ATaxiRoute* Route = World->SpawnActor<ATaxiRoute>(Points[0], FRotator::ZeroRotator);
		Route->RouteTag = Tag(RouteTag);
		Route->bShowCenterline = false;   // the map has its own paint
		Route->Spline->ClearSplinePoints(false);
		for (const FVector& Point : Points)
		{
			Route->Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
		}
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			Route->Spline->SetSplinePointType(Index, ESplinePointType::CurveClamped, false);
		}
		Route->Spline->UpdateSpline();
		Route->SetActorLabel(RouteTag.ToString().Replace(TEXT("."), TEXT("_")));
		Route->SetFolderPath(TEXT("ATC"));
		Route->Tags.Add(SetupTag);
		return Route;
	}

	bool SpawnDeparture(UWorld* World, const FSite& Site, FDeparture& D)
	{
		UBlueprint* StandBlueprint = LoadObject<UBlueprint>(nullptr, TEXT("/ATCSim/Blueprints/BP_Stand.BP_Stand"));
		UClass* StandClass = StandBlueprint && StandBlueprint->GeneratedClass ? StandBlueprint->GeneratedClass.Get() : AStand::StaticClass();

		D.PushRoute = SpawnRoute(World, D.PushRouteTag, D.Push);
		D.PushRoute->bShowLights = false;
		D.TaxiRoute = SpawnRoute(World, D.TaxiRouteTag, D.Taxi);

		FSpeedZone Apron;
		Apron.StartDistance = 0.f;
		Apron.EndDistance = UAircraftMovementComponent::GetDistanceClosestTo(D.TaxiRoute->Spline, D.ApronEnd);
		Apron.MaxSpeedKt = 10.f;
		D.TaxiRoute->SpeedZones = { Apron };
		AddRunwayStops(Site, D, D.TaxiRoute);
		D.PushRoute->RerunConstructionScripts();
		D.TaxiRoute->RerunConstructionScripts();

		if (D.Arrival.Num() > 1 && !D.ArrivalRouteTag.IsNone())
		{
			D.ArrivalRoute = SpawnRoute(World, D.ArrivalRouteTag, D.Arrival);
			FSpeedZone ArrivalApron;
			ArrivalApron.StartDistance = UAircraftMovementComponent::GetDistanceClosestTo(D.ArrivalRoute->Spline, D.ArrivalApronStart);
			ArrivalApron.EndDistance = D.ArrivalRoute->GetLength();
			ArrivalApron.MaxSpeedKt = 10.f;
			D.ArrivalRoute->SpeedZones = { ArrivalApron };
			AddRunwayStops(Site, D, D.ArrivalRoute, true);
			D.ArrivalRoute->RerunConstructionScripts();
		}

		D.Stand = World->SpawnActor<AStand>(StandClass, D.Parking);
		D.Stand->StandTag = Tag(D.StandTag);
		FPushbackOption Option;
		Option.OptionTag = Tag(FName(FString(TEXT("Pushback.Face")) + D.Face));
		Option.Route = D.PushRoute;
		Option.Phraseology = FText::FromString(FString::Printf(TEXT("push back approved, face %s"), *D.Face.ToLower()));
		D.Stand->PushbackOptions = { Option };
		D.Stand->SetActorLabel(FString(TEXT("Stand_")) + D.StandRef);
		D.Stand->SetFolderPath(TEXT("ATC"));
		D.Stand->Tags.Add(SetupTag);
		return true;
	}

	void RemoveSetupActors(UWorld* World)
	{
		TArray<AActor*> Old;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(SetupTag))
			{
				Old.Add(*It);
			}
		}
		for (AActor* Actor : Old)
		{
			World->EditorDestroyActor(Actor, true);
		}
		if (Old.Num() > 0)
		{
			UE_LOG(LogATC, Display, TEXT("Removed %d actors of an earlier setup"), Old.Num());
		}
	}

	void PlotSite(const FSite& Site, const TArray<FDeparture>& Departures, const FString& Name)
	{
		FBox Box(ForceInit);
		for (const FDeparture& D : Departures)
		{
			for (const FVector& Point : D.Taxi)
			{
				Box += Point;
			}
		}
		if (!Box.IsValid)
		{
			return;
		}
		Box = Box.ExpandBy(FVector(30000.0, 30000.0, 0.0));
		ATCMapSurvey::FPlot Plot(FBox2D(FVector2D(Box.Min), FVector2D(Box.Max)), 2200);
		Plot.Grid(50000.f, FColor(45, 50, 60), true);
		if (Site.Network)
		{
			for (const FVector& Node : Site.Network->Nodes)
			{
				Plot.Point(Node, FColor(110, 95, 40), 0);
			}
		}
		for (const FMapRunway& Runway : Site.Runways)
		{
			Plot.Line(Runway.A, Runway.B, FColor(200, 200, 200), 3);
		}
		for (const FVector& Point : Site.HoldMarkings)
		{
			Plot.Point(Point, FColor(255, 120, 0), 0);
		}
		for (const FDeparture& D : Departures)
		{
			for (int32 Index = 1; Index < D.Taxi.Num(); ++Index)
			{
				Plot.Line(D.Taxi[Index - 1], D.Taxi[Index], FColor::Red, 3);
			}
			for (int32 Index = 1; Index < D.Push.Num(); ++Index)
			{
				Plot.Line(D.Push[Index - 1], D.Push[Index], FColor::Cyan, 3);
			}
			for (int32 Index = 1; Index < D.Arrival.Num(); ++Index)
			{
				Plot.Line(D.Arrival[Index - 1], D.Arrival[Index], FColor(60, 140, 255), 2);
			}
			Plot.Point(D.Parking.GetLocation(), FColor::Green, 6);
			Plot.Label(D.Parking.GetLocation(), D.StandRef, FColor::Green, 2);
			if (D.TaxiRoute)
			{
				for (const FTaxiStop& Stop : D.TaxiRoute->Stops)
				{
					Plot.Point(D.TaxiRoute->Spline->GetLocationAtDistanceAlongSpline(Stop.Distance, ESplineCoordinateSpace::World), FColor::Magenta, 7);
				}
			}
		}
		Plot.Save(Name);
	}

	// ---------------------------------------------------------------- OSM-based sites (no painted lines)

	void AddOsmHoldingAndAprons(FSite& Site)
	{
		for (const ATCOsm::FElement* Element : Site.Osm.WithTag(TEXT("aeroway"), TEXT("holding_position")))
		{
			if (!Element->bWay)
			{
				Site.HoldMarkings.Add(Site.Geo.ToMap(Element->Points[0]));
			}
		}
		for (const ATCOsm::FElement* Element : Site.Osm.WithTag(TEXT("aeroway"), TEXT("apron")))
		{
			if (Element->IsClosed())
			{
				TArray<FVector2D>& Outline = Site.Aprons.AddDefaulted_GetRef();
				for (const FVector2D& Point : Element->Points)
				{
					Outline.Add(FVector2D(Site.Geo.ToMap(Point)));
				}
			}
		}
	}

	void BuildOsmNetwork(FSite& Site, TFunctionRef<double(const FVector&)> GroundZ)
	{
		// Every metre along the taxiway, taxilane and stand lines; OSM lines share their crossing points, so they connect
		TArray<FVector> Points;
		for (const ATCOsm::FElement& Element : Site.Osm.Elements)
		{
			const FString Kind = Element.Tag(TEXT("aeroway"));
			if (!Element.bWay || Element.Tag(TEXT("area")) == TEXT("yes")
				|| (Kind != TEXT("taxiway") && Kind != TEXT("taxilane") && Kind != TEXT("parking_position")))
			{
				continue;
			}
			for (int32 Index = 1; Index < Element.Points.Num(); ++Index)
			{
				const FVector A = Site.Geo.ToMap(Element.Points[Index - 1]);
				const FVector B = Site.Geo.ToMap(Element.Points[Index]);
				const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(A, B) / 100.0));
				for (int32 Step = 0; Step <= Steps; ++Step)
				{
					FVector Point = FMath::Lerp(A, B, double(Step) / Steps);
					Point.Z = GroundZ(Point);
					Points.Add(Point);
				}
			}
		}
		Site.OwnedNetwork = MakeShared<ATCMapSurvey::FNetwork>();
		Site.OwnedNetwork->Build(Points, 75.f, 220.f);
		Site.Network = Site.OwnedNetwork.Get();
		UE_LOG(LogATC, Display, TEXT("%s: OSM network %d nodes from %d points"), *Site.ICAO, Site.Network->Nodes.Num(), Points.Num());
	}

	UAirportData* UpdateAirport(const FSite& Site, const FString& Name, const FString& MapPackage, const TArray<FTowerPosition>& Towers, float ElevationFt)
	{
		UAirportData* Airport = GetOrCreateAsset<UAirportData>(FString::Printf(TEXT("/Game/Airports/%s/Data"), *Site.ICAO), TEXT("DA_Airport_") + Site.ICAO);
		Airport->ICAO = FName(*Site.ICAO);
		Airport->DisplayName = FText::FromString(Name);
		Airport->Latitude = Site.Geo.Latitude;
		Airport->Longitude = Site.Geo.Longitude;
		Airport->MapOrigin = Site.Geo.Origin;
		Airport->NorthOffsetDeg = Site.Geo.NorthYawDeg;
		Airport->ElevationFt = ElevationFt;
		const FString MapName = FPackageName::GetLongPackageAssetName(MapPackage);
		Airport->Map = TSoftObjectPtr<UWorld>(FSoftObjectPath(MapPackage + TEXT(".") + MapName));
		Airport->TowerPositions = Towers;
		Airport->Runways.Reset();
		for (const FMapRunway& Runway : Site.Runways)
		{
			FRunwayInfo Info;
			Info.Designator = Runway.Ref;
			Info.LengthM = Runway.Length() / 100.0;
			Airport->Runways.Add(Info);
		}
		Airport->DefaultWeather = TSoftObjectPtr<UWeatherPresetData>(FSoftObjectPath(TEXT("/Game/Environment/Weather/DA_Weather_Clear.DA_Weather_Clear")));
		UEditorLoadingAndSavingUtils::SavePackages({ Airport->GetPackage() }, false);
		return Airport;
	}

	UScenarioData* MakeDepartureScenario(const FSite& Site, const FDeparture& D, const TCHAR* Callsign, const FGameplayTag& Tower, const FString& Title)
	{
		UBlueprint* AircraftBlueprint = LoadObject<UBlueprint>(nullptr, TEXT("/ATCSim/Blueprints/BP_Aircraft_Base.BP_Aircraft_Base"));
		UScenarioData* Scenario = GetOrCreateAsset<UScenarioData>(TEXT("/Game/Scenarios"), FString::Printf(TEXT("DA_Scenario_%s_Vertrek"), *Site.ICAO));
		Scenario->Title = FText::FromString(Title);
		Scenario->Airport = TSoftObjectPtr<UAirportData>(FSoftObjectPath(FString::Printf(TEXT("/Game/Airports/%s/Data/DA_Airport_%s.DA_Airport_%s"), *Site.ICAO, *Site.ICAO, *Site.ICAO)));
		Scenario->TowerPosition = Tower;
		Scenario->StartTimeUTC = FDateTime(2026, 10, 13, 8, 0, 0);
		Scenario->Weather = TSoftObjectPtr<UWeatherPresetData>(FSoftObjectPath(TEXT("/Game/Environment/Weather/DA_Weather_Clear.DA_Weather_Clear")));
		FScenarioAircraft Aircraft;
		Aircraft.Callsign = Callsign;
		Aircraft.Type = D.Type;
		Aircraft.AircraftClass = TSoftClassPtr<AATCAircraft>(FSoftObjectPath(AircraftBlueprint ? AircraftBlueprint->GeneratedClass.Get() : AATCAircraft::StaticClass()));
		Aircraft.Stand = Tag(D.StandTag);
		Aircraft.TaxiRoute = Tag(D.TaxiRouteTag);
		Scenario->Aircraft = { Aircraft };
		Scenario->Events = { Event(5.f, Callsign, TEXT("Cmd.Pushback"), FString(TEXT("Pushback.Face")) + D.Face) };
		UEditorLoadingAndSavingUtils::SavePackages({ Scenario->GetPackage() }, false);
		return Scenario;
	}

	// ---------------------------------------------------------------- Schiphol

	/** Saves a copy of a map file once, before the first setup run changes it. */
	void BackupMapOnce(const FString& PackageName)
	{
		const FString MapFile = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetMapPackageExtension());
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("Backup_before_ATCSetup") / FPaths::GetCleanFilename(MapFile);
		if (!FPaths::FileExists(Backup))
		{
			IFileManager::Get().Copy(*Backup, *MapFile);
			UE_LOG(LogATC, Display, TEXT("Map backup: %s"), *Backup);
		}
	}

	template<typename T>
	T* GetOrCreateAsset(const FString& Folder, const FString& Name)
	{
		const FString PackageName = Folder / Name;
		T* Asset = FPackageName::DoesPackageExist(PackageName) ? LoadObject<T>(nullptr, *(PackageName + TEXT(".") + Name)) : nullptr;
		if (!Asset)
		{
			Asset = NewObject<T>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Asset);
		}
		Asset->Modify();
		Asset->MarkPackageDirty();
		return Asset;
	}

	FScenarioEvent Event(float Seconds, const TCHAR* Callsign, const TCHAR* Command, const FString& Parameter)
	{
		FScenarioEvent Result;
		Result.TimeOffsetSeconds = Seconds;
		Result.Callsign = Callsign;
		Result.Command = Tag(Command);
		if (!Parameter.IsEmpty())
		{
			Result.Parameter = Tag(FName(*Parameter));
		}
		return Result;
	}

	// ---------------------------------------------------------------- Schiphol traffic

	const FName TrafficTag(TEXT("ATCTraffic"));

	/**
	 * The Schiphol gates in the simulation (north flow: departures 36L and 24, arrivals 36C and 36R). The pier decides
	 * the aircraft size the routes are built for: E, F and G are the wide-body piers. Checked with ATC.ProbeEHAMGates.
	 */
	struct FGatePlan
	{
		const TCHAR* Ref;
		const TCHAR* Departure;
		const TCHAR* Arrival;
		bool bWideBody;
	};

	const FGatePlan EHAMGates[] = {
		// G7 and G4 carry the morning scenario
		{ TEXT("G7"), TEXT("36L"), TEXT("36C"), true },
		{ TEXT("G4"), TEXT("24"), TEXT("36C"), false },
		// B and C: Schengen narrow-body piers, Kaagbaan departures
		{ TEXT("B15"), TEXT("24"), TEXT("36R"), false }, { TEXT("B23"), TEXT("24"), TEXT("36R"), false }, { TEXT("B31"), TEXT("24"), TEXT("36R"), false },
		{ TEXT("C5"), TEXT("24"), TEXT("36C"), false }, { TEXT("C9"), TEXT("24"), TEXT("36C"), false }, { TEXT("C14"), TEXT("24"), TEXT("36C"), false },
		// D: narrow-body low numbers, wide-body high numbers, Polderbaan departures
		{ TEXT("D2"), TEXT("36L"), TEXT("36R"), false }, { TEXT("D12"), TEXT("36L"), TEXT("36R"), false }, { TEXT("D16"), TEXT("36L"), TEXT("36R"), false },
		{ TEXT("D7"), TEXT("36L"), TEXT("36C"), true }, { TEXT("D43"), TEXT("36L"), TEXT("36C"), true }, { TEXT("D53"), TEXT("36L"), TEXT("36C"), true },
		// E, F, G: intercontinental wide-body piers
		{ TEXT("E2"), TEXT("36L"), TEXT("36C"), true }, { TEXT("E6"), TEXT("36L"), TEXT("36C"), true }, { TEXT("E18"), TEXT("36L"), TEXT("36C"), true },
		{ TEXT("E22"), TEXT("36L"), TEXT("36C"), true }, { TEXT("F3"), TEXT("36L"), TEXT("36C"), true }, { TEXT("F7"), TEXT("36L"), TEXT("36C"), true },
		{ TEXT("G2"), TEXT("36L"), TEXT("36C"), true }, { TEXT("G6"), TEXT("36L"), TEXT("36C"), true }, { TEXT("G9"), TEXT("36L"), TEXT("36C"), true },
		// H: low-cost narrow-body pier, Kaagbaan departures
		{ TEXT("H1"), TEXT("24"), TEXT("36R"), false }, { TEXT("H3"), TEXT("24"), TEXT("36R"), false }, { TEXT("H6"), TEXT("24"), TEXT("36R"), false },
	};

	ATaxiRoute* FindRouteInWorld(UWorld* World, const TCHAR* RouteTag)
	{
		for (TActorIterator<ATaxiRoute> It(World); It; ++It)
		{
			if (It->RouteTag == Tag(RouteTag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/**
	 * Spawn point on a route, BeforeHoldM before its holding point (the first stop that needs clearance), but never on
	 * the apron part. Traffic from here taxis to the holding point and queues there.
	 */
	AAircraftSpawnPoint* AddSpawnPoint(UWorld* World, UClass* Class, ATaxiRoute* Route, const TCHAR* SpawnTag, float BeforeHoldM, UTrafficMixData* Mix)
	{
		Route->Rebuild();
		float Hold = Route->GetLength();
		for (const FTaxiStop& Stop : Route->GetAllStops())
		{
			if (Stop.bRequiresClearance)
			{
				Hold = Stop.Distance;
				break;
			}
		}
		float ApronEnd = 0.f;
		for (const FSpeedZone& Zone : Route->SpeedZones)
		{
			ApronEnd = FMath::Max(ApronEnd, Zone.EndDistance);
		}
		const float Distance = FMath::Max(Hold - BeforeHoldM * ATCUnits::MetersToCm, ApronEnd + 10000.f);
		const FVector Location = Route->Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

		AAircraftSpawnPoint* Point = World->SpawnActor<AAircraftSpawnPoint>(Class, Location, Route->GetDirectionAt(Distance).Rotation());
		Point->SpawnPointTag = Tag(SpawnTag);
		Point->Route = Route;
		Point->TrafficMix = Mix;
		Point->RerunConstructionScripts();
		Point->SetActorLabel(FString(SpawnTag).Replace(TEXT("."), TEXT("_")));
		Point->SetFolderPath(TEXT("ATC"));
		Point->Tags.Add(SetupTag);
		Point->Tags.Add(TrafficTag);
		UE_LOG(LogATC, Display, TEXT("%s: on %s at %.0f m, %.0f m before the holding point"), SpawnTag, *Route->RouteTag.ToString(),
			Point->StartDistanceM, (Hold - Distance) / 100.0);
		return Point;
	}

	/** A preset that looks from From at Target; pan relative to the camera mount, which faces MountYawDeg. */
	FCameraPreset AimAt(const FVector& From, const FVector& Target, const TCHAR* TagName, float FocalMm, float MountYawDeg = 0.f)
	{
		const FVector Delta = Target - From;
		FCameraPreset Preset;
		Preset.Tag = Tag(TagName);
		Preset.PanDeg = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)) - MountYawDeg);
		Preset.TiltDeg = FMath::RadiansToDegrees(FMath::Atan2(Delta.Z, FVector2D(Delta.X, Delta.Y).Size()));
		Preset.FocalLengthMm = FocalMm;
		return Preset;
	}

	/** Top of the roof over a tower cab: the middle of the highest 1 m slice of the meshes there that is at least 6 m across. */
	FVector TowerRoof(UWorld* World, const FVector& CabCenter, double FloorZ)
	{
		TMap<int32, FBox2D> Slices;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				const FMeshDescription* Description = Component->GetStaticMesh() ? Component->GetStaticMesh()->GetMeshDescription(0) : nullptr;
				const FBox Box = Component->Bounds.GetBox();
				if (!Description || !Component->IsVisible() || Box.Max.Z < FloorZ
					|| Box.ComputeSquaredDistanceToPoint(FVector(CabCenter.X, CabCenter.Y, FloorZ)) > FMath::Square(3000.0))
				{
					continue;
				}
				FStaticMeshConstAttributes Attributes(*Description);
				const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
				const FTransform Transform = Component->GetComponentTransform();
				for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
				{
					const FVector Point = Transform.TransformPosition(FVector(Positions[Vertex]));
					if (Point.Z >= FloorZ && FVector::Dist2D(Point, CabCenter) < 3000.0)
					{
						Slices.FindOrAdd(FMath::FloorToInt(Point.Z / 100.0), FBox2D(ForceInit)) += FVector2D(Point);
					}
				}
			}
		}
		FVector Roof(CabCenter.X, CabCenter.Y, FloorZ + 500.0);
		for (const TPair<int32, FBox2D>& Slice : Slices)
		{
			// Antennas are thin, the roof is wide
			if (Slice.Value.GetSize().GetMax() >= 600.0 && (Slice.Key + 1) * 100.0 > Roof.Z)
			{
				Roof = FVector(Slice.Value.GetCenter(), (Slice.Key + 1) * 100.0);
			}
		}
		return Roof;
	}

	/** The airport boundary as a camera at From sees it: the farthest runway end plus 500 m. */
	float BoundaryDistanceM(const FSite& Site, const FVector& From)
	{
		double Farthest = 0.0;
		for (const FMapRunway& Runway : Site.Runways)
		{
			Farthest = FMath::Max3(Farthest, FVector::Dist2D(From, Runway.A), FVector::Dist2D(From, Runway.B));
		}
		return float(Farthest / 100.0 + 500.0);
	}

	/** Runway ends in the order the traffic uses them (south flow first): presets 1 to 9 of the tower camera. */
	const TCHAR* const EHAMThresholds[] = { TEXT("36L"), TEXT("24"), TEXT("36C"), TEXT("36R"), TEXT("18R"), TEXT("06"), TEXT("18C"), TEXT("18L"),
		TEXT("27"), TEXT("09"), TEXT("22"), TEXT("04") };

	/**
	 * Screens of the Tower Centrum model that show the cameras, in camera order (ATC.TowerScreens lists them all). The
	 * console in front of the controller keeps its radar and strips; its right display, where the flight list was,
	 * shows the camera the joystick moves. The console on the right becomes the camera desk with the four apron cameras.
	 */
	const TCHAR* const EHAMCameraScreens[] = { TEXT("SketchUp_Model13/TWR_Rechts_Mat"), TEXT("SketchUp_Model15/CCIS_Mat"),
		TEXT("SketchUp_Model15/TWR_Midden_Mat"), TEXT("SketchUp_Model15/TWR_Rechts_Mat"), TEXT("SketchUp_Model15/EFSS_Mat") };

	bool MakeEHAMSite(UWorld* World, FSite& Site);

	/**
	 * The GMI cameras of Schiphol, on the screens of the Tower Centrum model. GMI-TWR is a dome camera on the tower roof
	 * that turns all the way round, with a preset for every runway threshold (Cam.EHAM.Threshold36L ...): the far
	 * runway ends, which the tower sees only as a line. Four apron cameras hang on the terminal side of their gate,
	 * 22 m up, looking over the aircraft at the taxilane behind it, like the real apron cameras. Every camera draws as
	 * far as the airport boundary seen from it. ScreenKeys: which screens get the cameras, in order (empty =
	 * EHAMCameraScreens; when those are not in the map, the working screens nearest the eye).
	 */
	void AddEHAMCameras(UWorld* World, const FSite& Site, const TArray<FString>& ScreenKeys)
	{
		UClass* CameraClass = ATCExample::MakeBlueprint(AGMICamera::StaticClass(), TEXT("/ATCSim/Blueprints"), TEXT("BP_GMICamera"));
		UClass* MonitorClass = ATCExample::MakeBlueprint(AMonitorDisplay::StaticClass(), TEXT("/ATCSim/Blueprints"), TEXT("BP_MonitorDisplay"));
		ULensSetData* Lenses = ATCExample::MakeLensSet();
		const UAirportData* Airport = LoadObject<UAirportData>(nullptr, TEXT("/Game/Airports/EHAM/Data/DA_Airport_EHAM.DA_Airport_EHAM"));
		const FTowerPosition* Tower = Airport ? Airport->FindTower(Tag(TEXT("Tower.EHAM.Centrum"))) : nullptr;
		if (!Tower)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.SetupGMI: Tower Centrum not found, no cameras"));
			return;
		}
		const FVector Eye = Tower->Transform.GetLocation() + FVector(0.f, 0.f, Tower->EyeHeightM * ATCUnits::MetersToCm);
		const float ViewYaw = Tower->Transform.Rotator().Yaw;

		TArray<AGMICamera*> Cameras;
		auto Spawn = [&](const FVector& Location, const FString& Id, float MountYawDeg = 0.f)
		{
			AGMICamera* Camera = World->SpawnActor<AGMICamera>(CameraClass, Location, FRotator(0.f, MountYawDeg, 0.f));
			Camera->CameraId = FName(*Id);
			Camera->RenderTargetSize = FIntPoint(1280, 720);
			Camera->CaptureRateHz = 20.f;
			Camera->MaxViewDistanceM = BoundaryDistanceM(Site, Location);
			Camera->PTZ->LensSet = Lenses;
			Camera->SetActorLabel(Id.Replace(TEXT("-"), TEXT("_")));
			Camera->SetFolderPath(TEXT("ATC"));
			Camera->Tags.Add(SetupTag);
			Camera->Tags.Add(TrafficTag);
			Cameras.Add(Camera);
			return Camera;
		};

		// Tower cab: the tower position stands 3 m in front of the middle of the cab
		const FVector CabCenter = Tower->Transform.GetLocation() - FRotator(0.f, ViewYaw, 0.f).Vector() * 300.f;
		const FVector Roof = TowerRoof(World, CabCenter, Tower->Transform.GetLocation().Z);
		AGMICamera* Dome = Spawn(Roof + FVector(0.f, 0.f, 300.f), TEXT("GMI-TWR"));
		Dome->PTZ->PanLimitsDeg = FVector2D(-180.f, 180.f);
		for (const TCHAR* Designator : EHAMThresholds)
		{
			FVector Start, Direction;
			int32 Runway = INDEX_NONE;
			if (FindTakeoff(Site, Designator, Start, Direction, Runway))
			{
				// 300 m past the threshold, about 600 m of it in the picture
				const FVector Target = Start + Direction * 30000.0 + FVector(0.f, 0.f, 200.f);
				const float FocalMm = FMath::Clamp(36.f * float(FVector::Dist(Dome->GetActorLocation(), Target)) / 60000.f, 24.f, 400.f);
				Dome->PTZ->Presets.Add(AimAt(Dome->GetActorLocation(), Target, *FString::Printf(TEXT("Cam.EHAM.Threshold%s"), Designator), FocalMm));
			}
		}
		UE_LOG(LogATC, Display, TEXT("ATC.SetupGMI: GMI-TWR on the tower roof at Z=%.0f m with %d runway presets, draws %.0f m far"),
			Dome->GetActorLocation().Z / 100.0, Dome->PTZ->Presets.Num(), Dome->MaxViewDistanceM);

		for (const TCHAR* Ref : { TEXT("G7"), TEXT("E6"), TEXT("D43"), TEXT("C9") })
		{
			const AStand* Stand = nullptr;
			for (TActorIterator<AStand> It(World); It; ++It)
			{
				Stand = It->StandTag == Tag(FName(*FString::Printf(TEXT("Stand.EHAM.%s"), Ref))) ? *It : Stand;
			}
			if (!Stand)
			{
				continue;
			}
			const FTransform Parking = Stand->GetParkingTransform();
			const FVector Nose = Parking.GetRotation().GetForwardVector().GetSafeNormal2D();
			const FVector Location = Parking.GetLocation() + Nose * 5000.f + FVector(0.f, 0.f, 2200.f);
			const FVector Aircraft = Parking.GetLocation() - Nose * 1000.f;
			// Mounted facing its gate: both presets lie well inside the cable stop at +-170 degrees
			const float MountYaw = (-Nose).Rotation().Yaw;
			AGMICamera* Camera = Spawn(Location, FString::Printf(TEXT("GMI-%s"), Ref), MountYaw);
			Camera->PTZ->Presets = { AimAt(Location, Aircraft, *FString::Printf(TEXT("Cam.EHAM.%s"), Ref), 24.f, MountYaw),
				AimAt(Location, Aircraft - Nose * 9000.f, TEXT("Cam.EHAM.Apron"), 35.f, MountYaw) };
		}

		// Their screens in the tower model, one camera each
		TArray<FString> Keys = ScreenKeys;
		if (Keys.Num() == 0)
		{
			for (const TCHAR* Key : EHAMCameraScreens)
			{
				Keys.Add(Key);
			}
		}
		TArray<ATCTowerScreens::FScreenFace> Faces = ATCTowerScreens::ScreensByKey(World, Eye, Keys);
		if (Faces.Num() == 0)
		{
			Faces = ATCTowerScreens::WorkingScreens(World, Eye, ViewYaw);
		}
		for (int32 Index = 0; Index < FMath::Min(Faces.Num(), Cameras.Num()); ++Index)
		{
			AMonitorDisplay* Monitor = ATCTowerScreens::PlaceMonitor(World, MonitorClass, Faces[Index], Cameras[Index], Index + 1);
			Monitor->Tags.Add(SetupTag);
			Monitor->Tags.Add(TrafficTag);
			UE_LOG(LogATC, Display, TEXT("ATC.SetupGMI: screen %d (%s, %.0fx%.0f cm) shows %s"), Index + 1, *Faces[Index].Key(), Faces[Index].WidthCm,
				Faces[Index].HeightCm, *Cameras[Index]->CameraId.ToString());
		}
		if (Faces.Num() < Cameras.Num())
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.SetupGMI: %d screens for %d cameras; the others only with ATC.Monitor <screen> <camera>"), Faces.Num(), Cameras.Num());
		}
		UEditorLoadingAndSavingUtils::SavePackages({ CameraClass->GetOutermost(), MonitorClass->GetOutermost() }, false);
	}

	/** GMI cameras and tower screens, plus the radio log on the other side of the view. */
	void AddEHAMCameras(UWorld* World)
	{
		FSite Site;
		if (MakeEHAMSite(World, Site))
		{
			AddEHAMCameras(World, Site, TArray<FString>());
		}
		const UAirportData* Airport = LoadObject<UAirportData>(nullptr, TEXT("/Game/Airports/EHAM/Data/DA_Airport_EHAM.DA_Airport_EHAM"));
		const FTowerPosition* Tower = Airport ? Airport->FindTower(Tag(TEXT("Tower.EHAM.Centrum"))) : nullptr;
		if (!Tower)
		{
			return;
		}
		const FVector Eye = Tower->Transform.GetLocation() + FVector(0.f, 0.f, Tower->EyeHeightM * ATCUnits::MetersToCm);
		// Radio log left of the main view
		const float LogYaw = Tower->Transform.Rotator().Yaw - 36.f;
		const FVector LogLocation = Eye + FRotator(0.f, LogYaw, 0.f).Vector() * 140.f + FVector(0.f, 0.f, -15.f);
		UClass* RadioLogClass = ATCExample::MakeBlueprint(ARadioLogDisplay::StaticClass(), TEXT("/ATCSim/Blueprints"), TEXT("BP_RadioLogDisplay"));
		ARadioLogDisplay* Log = World->SpawnActor<ARadioLogDisplay>(RadioLogClass, LogLocation, FRotator(0.f, LogYaw + 180.f, 0.f));
		Log->WidthCm = 55.f;
		Log->RerunConstructionScripts();
		Log->SetActorLabel(TEXT("TowerRadioLog"));
		Log->SetFolderPath(TEXT("ATC"));
		Log->Tags.Add(SetupTag);
		Log->Tags.Add(TrafficTag);

		// Blueprints made here are new packages: without saving them the map would refer to classes that do not exist
		UEditorLoadingAndSavingUtils::SavePackages({ RadioLogClass->GetOutermost() }, false);
	}

	/**
	 * ATC.SetupGMI [screens]: the GMI cameras of Schiphol again, on the screens of the Tower Centrum model. A screen is
	 * "actor label/material" as ATC.TowerScreens lists it; without screens EHAMCameraScreens. Only cameras and their
	 * screens change; the map is saved, with a copy first in Saved/Backup_before_GMI.
	 */
	void SetupGMI(const TArray<FString>& Args)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const FString MapPackage = TEXT("/Game/EHAM/EHAM");
		if (!World || World->GetOutermost()->GetName() != MapPackage)
		{
			UE_LOG(LogATC, Error, TEXT("ATC.SetupGMI: open %s first"), *MapPackage);
			return;
		}
		FSite Site;
		if (!MakeEHAMSite(World, Site))
		{
			return;
		}
		const FString MapFile = FPackageName::LongPackageNameToFilename(MapPackage, FPackageName::GetMapPackageExtension());
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("Backup_before_GMI") / FPaths::GetCleanFilename(MapFile);
		if (!FPaths::FileExists(Backup))
		{
			IFileManager::Get().Copy(*Backup, *MapFile);
		}

		TArray<AActor*> Old;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsA<AGMICamera>() || It->IsA<AMonitorDisplay>())
			{
				Old.Add(*It);
			}
		}
		for (AActor* Actor : Old)
		{
			World->EditorDestroyActor(Actor, true);
		}
		AddEHAMCameras(World, Site, Args);
		UEditorLoadingAndSavingUtils::SavePackages({ World->GetOutermost() }, false);
		UE_LOG(LogATC, Display, TEXT("ATC.SetupGMI: %d old cameras and screens replaced, map saved"), Old.Num());
	}

	FAutoConsoleCommand SetupGMICommand(TEXT("ATC.SetupGMI"),
		TEXT("ATC.SetupGMI [actor/material ...]: GMI cameras of Schiphol (tower roof dome with runway presets, four apron cameras) on the screens of Tower Centrum; saves the map"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetupGMI));

	/**
	 * Plain Play as a live airport (bOn): the traffic directors start and the player stands in the airport's first tower.
	 * Off: plain Play keeps the map as it is and traffic only comes from a scenario. Adds the changed packages to Packages.
	 */
	int32 SetLivePlay(UWorld* World, bool bOn, TArray<UPackage*>& Packages)
	{
		int32 Count = 0;
		for (TActorIterator<ATrafficDirector> It(World); It; ++It, ++Count)
		{
			It->Modify();
			It->bStartOnBeginPlay = bOn;
			if (UPackage* External = It->GetExternalPackage())
			{
				Packages.AddUnique(External);
			}
		}
		if (UAirportData* Airport = UAirportData::FindForWorld(World))
		{
			Airport->Modify();
			Airport->PlayTower = bOn && Airport->TowerPositions.Num() > 0 ? Airport->TowerPositions[0].Tag : FGameplayTag();
			Packages.AddUnique(Airport->GetPackage());
		}
		return Count;
	}

	/** Spawn points on both EHAM routes, the Schiphol traffic mix and the busy scenario. The map is saved. */
	bool AddEHAMTraffic(UWorld* World)
	{
		ATaxiRoute* To36L = FindRouteInWorld(World, TEXT("Route.EHAM.G7.Rwy36L"));
		ATaxiRoute* To24 = FindRouteInWorld(World, TEXT("Route.EHAM.G4.Rwy24"));
		if (!To36L || !To24)
		{
			UE_LOG(LogATC, Error, TEXT("ATC.SetupEHAMTraffic: routes not in the map; run ATC.SetupEHAM first"));
			return false;
		}
		TArray<AActor*> Old;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(TrafficTag))
			{
				Old.Add(*It);
			}
		}
		for (AActor* Actor : Old)
		{
			World->EditorDestroyActor(Actor, true);
		}

		// Traffic mix: only types with a model, so the spawned traffic fits the look of the map
		UTrafficMixData* Mix = GetOrCreateAsset<UTrafficMixData>(TEXT("/Game/Airports/EHAM/Data"), TEXT("DA_TrafficMix_EHAM"));
		ATCExample::FillEHAMTrafficMix(Mix);
		UBlueprint* AircraftBlueprint = LoadObject<UBlueprint>(nullptr, TEXT("/ATCSim/Blueprints/BP_Aircraft_Base.BP_Aircraft_Base"));
		Mix->AircraftClass = TSoftClassPtr<AATCAircraft>(FSoftObjectPath(AircraftBlueprint ? AircraftBlueprint->GeneratedClass.Get() : AATCAircraft::StaticClass()));

		UClass* PointClass = ATCExample::MakeBlueprint(AAircraftSpawnPoint::StaticClass(), TEXT("/ATCSim/Blueprints"), TEXT("BP_AircraftSpawnPoint"));
		AddSpawnPoint(World, PointClass, To36L, TEXT("Spawn.EHAM.Rwy36L"), 1800.f, Mix);
		AddSpawnPoint(World, PointClass, To24, TEXT("Spawn.EHAM.Rwy24"), 1000.f, Mix);

		// Traffic director: every stand of the setup with its departure route and (if built) arrival route
		ATrafficDirector* Director = World->SpawnActor<ATrafficDirector>(FVector(0.f, 0.f, 20000.f), FRotator::ZeroRotator);
		Director->TrafficMix = Mix;
		for (TActorIterator<AStand> It(World); It; ++It)
		{
			const FString Ref = It->StandTag.ToString().RightChop(FString(TEXT("Stand.EHAM.")).Len());
			if (!It->StandTag.ToString().StartsWith(TEXT("Stand.EHAM.")) || It->PushbackOptions.Num() == 0)
			{
				continue;
			}
			FTrafficGate Gate;
			Gate.Stand = It->StandTag;
			Gate.PushbackOption = It->PushbackOptions[0].OptionTag;
			for (TActorIterator<ATaxiRoute> RouteIt(World); RouteIt; ++RouteIt)
			{
				const FString RouteTag = RouteIt->RouteTag.ToString();
				if (RouteTag.StartsWith(FString::Printf(TEXT("Route.EHAM.%s.Rwy"), *Ref)))
				{
					Gate.DepartureRoute = RouteIt->RouteTag;
				}
				else if (RouteTag.StartsWith(FString::Printf(TEXT("Route.EHAM.%s.Arr"), *Ref)))
				{
					Gate.ArrivalRoute = RouteIt->RouteTag;
				}
			}
			for (const FGatePlan& Plan : EHAMGates)
			{
				Gate.bWideBody |= Ref == Plan.Ref && Plan.bWideBody;
			}
			if (Gate.DepartureRoute.IsValid())
			{
				Director->Gates.Add(Gate);
			}
		}
		Director->SetActorLabel(TEXT("TrafficDirector_EHAM"));
		Director->SetFolderPath(TEXT("ATC"));
		Director->Tags.Add(SetupTag);
		Director->Tags.Add(TrafficTag);
		UE_LOG(LogATC, Display, TEXT("ATC.SetupEHAMTraffic: traffic director with %d gates"), Director->Gates.Num());
		AddEHAMCameras(World);

		// Plain Play stays the same: the spawn points only send traffic when a scenario asks for it
		UScenarioData* Base = LoadObject<UScenarioData>(nullptr, TEXT("/Game/Scenarios/DA_Scenario_EHAM_Ochtendpiek.DA_Scenario_EHAM_Ochtendpiek"));
		UScenarioData* Scenario = GetOrCreateAsset<UScenarioData>(TEXT("/Game/Scenarios"), TEXT("DA_Scenario_EHAM_Verkeer"));
		Scenario->Title = FText::FromString(TEXT("Schiphol druk: rijen voor de Polderbaan en de Kaagbaan"));
		Scenario->Airport = TSoftObjectPtr<UAirportData>(FSoftObjectPath(TEXT("/Game/Airports/EHAM/Data/DA_Airport_EHAM.DA_Airport_EHAM")));
		Scenario->TowerPosition = Tag(TEXT("Tower.EHAM.Centrum"));
		Scenario->StartTimeUTC = FDateTime(2026, 10, 13, 7, 10, 0);
		Scenario->Weather = TSoftObjectPtr<UWeatherPresetData>(FSoftObjectPath(TEXT("/Game/Environment/Weather/DA_Weather_Clear.DA_Weather_Clear")));
		Scenario->Aircraft = Base ? Base->Aircraft : TArray<FScenarioAircraft>();
		for (FScenarioAircraft& Entry : Scenario->Aircraft)
		{
			Entry.bRemoveAtRouteEnd = true;
		}
		Scenario->Events = Base ? Base->Events : TArray<FScenarioEvent>();
		auto Stream = [](float Seconds, const TCHAR* SpawnTag, int32 Count, float Interval)
		{
			FScenarioSpawn Spawn;
			Spawn.TimeOffsetSeconds = Seconds;
			Spawn.SpawnPoint = Tag(SpawnTag);
			Spawn.Count = Count;
			Spawn.IntervalSeconds = Interval;
			return Spawn;
		};
		Scenario->Spawns = { Stream(0.f, TEXT("Spawn.EHAM.Rwy36L"), 3, 75.f), Stream(15.f, TEXT("Spawn.EHAM.Rwy24"), 2, 90.f) };

		// A real day: the traffic director runs arrivals, turnarounds and departures, a colleague gives the clearances
		UScenarioData* Day = GetOrCreateAsset<UScenarioData>(TEXT("/Game/Scenarios"), TEXT("DA_Scenario_EHAM_Dag"));
		Day->Title = FText::FromString(TEXT("Schiphol, een gewone drukke dag: aankomsten, turnarounds en vertrekken"));
		Day->Airport = Scenario->Airport;
		Day->TowerPosition = Scenario->TowerPosition;
		Day->StartTimeUTC = FDateTime(2026, 10, 13, 7, 30, 0);
		Day->Weather = Scenario->Weather;
		Day->Aircraft.Reset();
		Day->Events.Reset();
		Day->Spawns.Reset();
		Day->bStartTrafficDirector = true;
		Day->bAutoClearances = true;

		// The same day, but every crossing and line-up clearance is the trainee's
		UScenarioData* Training = GetOrCreateAsset<UScenarioData>(TEXT("/Game/Scenarios"), TEXT("DA_Scenario_EHAM_DagZelf"));
		Training->Title = FText::FromString(TEXT("Schiphol drukke dag, alle klaringen zelf geven"));
		Training->Airport = Day->Airport;
		Training->TowerPosition = Day->TowerPosition;
		Training->StartTimeUTC = Day->StartTimeUTC;
		Training->Weather = Day->Weather;
		Training->Aircraft.Reset();
		Training->Events.Reset();
		Training->Spawns.Reset();
		Training->bStartTrafficDirector = true;
		Training->bAutoClearances = false;
		Training->ArrivalsPerHour = 24.f;

		TArray<UPackage*> Packages = { World->GetOutermost(), Mix->GetPackage(), Scenario->GetPackage(), PointClass->GetOutermost(),
			Day->GetPackage(), Training->GetPackage() };
		SetLivePlay(World, true, Packages);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(TrafficTag))
			{
				if (UPackage* External = It->GetExternalPackage())
				{
					Packages.AddUnique(External);
				}
			}
		}
		UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
		UE_LOG(LogATC, Display, TEXT("ATC.SetupEHAMTraffic: done (2 spawn points, %s, %s)"), *Mix->GetName(), *Scenario->GetName());
		return true;
	}

	/** ATC.SetupEHAMTraffic: spawn points, traffic mix and the busy scenario in the open EHAM map (the routes stay as they are). */
	void SetupEHAMTraffic(const TArray<FString>& Args)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const FString MapPackage = TEXT("/Game/EHAM/EHAM");
		if (!World || World->GetOutermost()->GetName() != MapPackage)
		{
			UE_LOG(LogATC, Error, TEXT("ATC.SetupEHAMTraffic: open %s first"), *MapPackage);
			return;
		}
		ATCMapSurvey::LoadWholeWorld(World);
		AddEHAMTraffic(World);
	}

	FAutoConsoleCommand SetupEHAMTrafficCommand(TEXT("ATC.SetupEHAMTraffic"),
		TEXT("Add the spawn points (36L and 24 queues), the Schiphol traffic mix and the EHAM_Verkeer scenario to the open EHAM map"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetupEHAMTraffic));

	/** ATC.TrafficOnPlay [0|1]: plain Play as a live airport, traffic and tower view (1), or the map as it is (0); saves map and airport. */
	void TrafficOnPlay(const TArray<FString>& Args)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return;
		}
		const bool bOn = Args.Num() == 0 || Args[0] != TEXT("0");
		TArray<UPackage*> Packages = { World->GetOutermost() };
		const int32 Count = SetLivePlay(World, bOn, Packages);
		if (Count == 0)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.TrafficOnPlay: %s has no traffic director (ATC.SetupEHAMTraffic adds one to EHAM)"), *World->GetOutermost()->GetName());
			return;
		}
		UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
		UE_LOG(LogATC, Display, TEXT("ATC.TrafficOnPlay: %d traffic director(s) %s, tower on Play %s, saved"), Count,
			bOn ? TEXT("start on Play") : TEXT("wait for a scenario"), bOn ? TEXT("on") : TEXT("off"));
	}

	FAutoConsoleCommand TrafficOnPlayCommand(TEXT("ATC.TrafficOnPlay"),
		TEXT("ATC.TrafficOnPlay [0|1]: plain Play starts the map's traffic and puts you in the first tower, or keeps the map as it is"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&TrafficOnPlay));

	/** The Schiphol map as a site: OSM data, the painted lines, runways and hold markings (loads the whole map). */
	bool MakeEHAMSite(UWorld* World, FSite& Site)
	{
		// The EHAM map is made around the Schiphol Centre tower with +X east and +Y south (checked with ATC.GeoFit:
		// the OSM runways lie within 0.1 degree and 10 m of the runway paint)
		Site.ICAO = TEXT("EHAM");
		Site.Geo.Latitude = 52.3075170;
		Site.Geo.Longitude = 4.7624393;
		Site.Geo.NorthYawDeg = -90.f;
		if (!Site.Osm.Load(Site.ICAO))
		{
			return false;
		}
		Site.Network = &ATCMapSurvey::PaintNetwork(World);
		Site.Runways = MapRunways(Site.Geo, ATCOsm::Runways(Site.Osm), ATCMapSurvey::RunwayMarkingPoints(World));
		Site.HoldMarkings = ATCMapSurvey::HoldMarkingPoints(World);
		return true;
	}

	/** Gate with its tags: Stand.EHAM.<ref>, Route.EHAM.<ref>.Pushback / .Rwy<departure> / .Arr<arrival>. */
	FDeparture MakeGate(const FString& Ref, const FString& Departure, const FString& Arrival, UAircraftTypeData* Type)
	{
		FDeparture D;
		D.StandRef = Ref;
		D.Runway = Departure;
		D.Type = Type;
		D.StandTag = FName(*FString::Printf(TEXT("Stand.EHAM.%s"), *Ref));
		D.PushRouteTag = FName(*FString::Printf(TEXT("Route.EHAM.%s.Pushback"), *Ref));
		D.TaxiRouteTag = FName(*FString::Printf(TEXT("Route.EHAM.%s.Rwy%s"), *Ref, *Departure));
		D.ArrivalRunway = Arrival;
		if (!Arrival.IsEmpty())
		{
			D.ArrivalRouteTag = FName(*FString::Printf(TEXT("Route.EHAM.%s.Arr%s"), *Ref, *Arrival));
		}
		return D;
	}

	UAircraftTypeData* LoadType(const TCHAR* ICAO)
	{
		return LoadObject<UAircraftTypeData>(nullptr, *FString::Printf(TEXT("/Game/Aircraft/%s/DA_AircraftType_%s.DA_AircraftType_%s"), ICAO, ICAO, ICAO));
	}

	/**
	 * ATC.ProbeEHAMGates <departure runways> <arrival runways> <ref>...: tries the departure and arrival routes of
	 * these stands without placing anything, logs which work and how long they are, and plots them (Saved/ATC/eham_probe.png).
	 * Example: ATC.ProbeEHAMGates 36L,24 36C,36R D7 E18 F5
	 */
	void ProbeEHAMGates(const TArray<FString>& Args)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		FSite Site;
		if (Args.Num() < 3 || !World || !MakeEHAMSite(World, Site))
		{
			UE_LOG(LogATC, Error, TEXT("ATC.ProbeEHAMGates <departure runways, comma separated> <arrival runways> <stand refs...> (EHAM map open)"));
			return;
		}
		TArray<FString> DepartureRunways, ArrivalRunways;
		Args[0].ParseIntoArray(DepartureRunways, TEXT(","));
		Args[1].ParseIntoArray(ArrivalRunways, TEXT(","));
		UAircraftTypeData* Type = LoadType(TEXT("A320"));

		auto RouteLength = [](const TArray<FVector>& Points)
		{
			double Length = 0.0;
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				Length += FVector::Dist2D(Points[Index - 1], Points[Index]);
			}
			return Length / 100.0;
		};
		TArray<FDeparture> Plotted;
		for (int32 Index = 2; Index < Args.Num(); ++Index)
		{
			FString Line = Args[Index] + TEXT(":");
			FDeparture Best;
			double BestDeparture = 0.0, BestArrival = 0.0;
			for (const FString& Runway : DepartureRunways)
			{
				FDeparture D = MakeGate(Args[Index], Runway, FString(), Type);
				const bool bOk = BuildDeparture(Site, D);
				const double Length = bOk ? RouteLength(D.Taxi) : 0.0;
				Line += bOk ? FString::Printf(TEXT(" dep %s %.0f m"), *Runway, Length) : FString::Printf(TEXT(" dep %s FAIL"), *Runway);
				if (bOk && (BestDeparture == 0.0 || Length < BestDeparture))
				{
					BestDeparture = Length;
					Best = D;
				}
			}
			for (const FString& Runway : ArrivalRunways)
			{
				FDeparture D = MakeGate(Args[Index], DepartureRunways[0], Runway, Type);
				const bool bOk = BuildArrival(Site, D);
				const double Length = bOk ? RouteLength(D.Arrival) : 0.0;
				Line += bOk ? FString::Printf(TEXT(" | arr %s %.0f m"), *Runway, Length) : FString::Printf(TEXT(" | arr %s FAIL"), *Runway);
				if (bOk && (BestArrival == 0.0 || Length < BestArrival))
				{
					BestArrival = Length;
					Best.Arrival = D.Arrival;
				}
			}
			UE_LOG(LogATC, Display, TEXT("ATC.ProbeEHAMGates %s"), *Line);
			if (BestDeparture > 0.0)
			{
				Plotted.Add(Best);
			}
		}
		PlotSite(Site, Plotted, TEXT("eham_probe"));
		UE_LOG(LogATC, Display, TEXT("ATC.ProbeEHAMGates: done"));
	}

	FAutoConsoleCommand ProbeEHAMGatesCommand(TEXT("ATC.ProbeEHAMGates"),
		TEXT("ATC.ProbeEHAMGates <departure runways> <arrival runways> <stand refs...>: try routes for stands without placing anything"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ProbeEHAMGates));

	/**
	 * ATC.CheckRoutes: which taxi routes of the open EHAM map leave the pavement. A point counts as paved on the painted
	 * lines, an OSM apron, a runway, or within half the width (plus a 7.5 m shoulder) of an OSM taxiway or taxilane.
	 * Logs every stretch of 20 m or more off the pavement and plots the routes (Saved/ATC/route_check.png, off = red).
	 * Also stores on every arrival route how much runway lies behind its start (where landings touch down); saves the map.
	 */
	void CheckRoutes(const TArray<FString>& Args)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		FSite Site;
		if (!World)
		{
			return;
		}
		ATCMapSurvey::LoadWholeWorld(World);
		if (!MakeEHAMSite(World, Site))
		{
			UE_LOG(LogATC, Error, TEXT("ATC.CheckRoutes: open the EHAM map first"));
			return;
		}
		AddOsmHoldingAndAprons(Site);

		struct FSegment
		{
			FVector A;
			FVector B;
			double Reach = 0.0;
		};
		TArray<FSegment> Taxiways;
		for (const ATCOsm::FElement& Element : Site.Osm.Elements)
		{
			const FString Kind = Element.Tag(TEXT("aeroway"));
			if (!Element.bWay || (Kind != TEXT("taxiway") && Kind != TEXT("taxilane")))
			{
				continue;
			}
			const FString WidthTag = Element.Tag(TEXT("width"));
			const double Width = WidthTag.IsEmpty() ? (Kind == TEXT("taxilane") ? 15.0 : 23.0) : FCString::Atod(*WidthTag);
			for (int32 Index = 1; Index < Element.Points.Num(); ++Index)
			{
				FSegment Segment;
				Segment.A = Site.Geo.ToMap(Element.Points[Index - 1]);
				Segment.B = Site.Geo.ToMap(Element.Points[Index]);
				Segment.A.Z = Segment.B.Z = 0.0;
				Segment.Reach = (Width * 0.5 + 7.5) * 100.0;
				Taxiways.Add(Segment);
			}
		}
		const ATCMapSurvey::FNetwork& Paint = *Site.Network;
		auto IsPaved = [&](const FVector& Point)
		{
			if (Paint.Nearest(Point, 800.f) != INDEX_NONE)
			{
				return true;
			}
			const FVector Flat(Point.X, Point.Y, 0.0);
			for (const TArray<FVector2D>& Apron : Site.Aprons)
			{
				if (IsInside(Apron, FVector2D(Flat)))
				{
					return true;
				}
			}
			for (const FMapRunway& Runway : Site.Runways)
			{
				double Along = 0.0, Side = 0.0;
				Runway.Locate(Point, Along, Side);
				if (Along > -3000.0 && Along < Runway.Length() + 3000.0 && Side < Runway.HalfWidthCm + 750.0)
				{
					return true;
				}
			}
			for (const FSegment& Segment : Taxiways)
			{
				if (FMath::PointDistToSegment(Flat, Segment.A, Segment.B) < Segment.Reach)
				{
					return true;
				}
			}
			return false;
		};

		FBox2D Area(ForceInit);
		for (TActorIterator<ATaxiRoute> It(World); It; ++It)
		{
			for (float D = 0.f; D <= It->GetLength(); D += 5000.f)
			{
				Area += FVector2D(It->Spline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World));
			}
		}
		Area = Area.ExpandBy(20000.0);
		ATCMapSurvey::FPlot Plot(Area, 2400);
		for (const TArray<FVector2D>& Apron : Site.Aprons)
		{
			for (int32 Index = 1; Index < Apron.Num(); ++Index)
			{
				Plot.Line(FVector(Apron[Index - 1], 0.0), FVector(Apron[Index], 0.0), FColor(70, 70, 100), 1);
			}
		}
		for (const FSegment& Segment : Taxiways)
		{
			Plot.Line(Segment.A, Segment.B, FColor(80, 80, 80), 1);
		}
		for (const FMapRunway& Runway : Site.Runways)
		{
			Plot.Line(Runway.A, Runway.B, FColor(160, 160, 160), 3);
		}

		int32 OffRoutes = 0, Arrivals = 0;
		TArray<UPackage*> Packages = { World->GetOutermost() };
		for (TActorIterator<ATaxiRoute> It(World); It; ++It)
		{
			ATaxiRoute* Route = *It;
			const float Length = Route->GetLength();
			TArray<FString> Stretches;
			float OffStart = -1.f, Total = 0.f;
			FVector Worst = FVector::ZeroVector;
			for (float D = 0.f; D <= Length + 1.f; D += 500.f)
			{
				const FVector Point = Route->Spline->GetLocationAtDistanceAlongSpline(FMath::Min(D, Length), ESplineCoordinateSpace::World);
				const bool bPaved = IsPaved(Point);
				Plot.Point(Point, bPaved ? FColor(70, 190, 90) : FColor(255, 40, 40), bPaved ? 0 : 2);
				if (!bPaved && OffStart < 0.f)
				{
					OffStart = D;
					Worst = Point;
				}
				if ((bPaved || D + 500.f > Length) && OffStart >= 0.f)
				{
					if (D - OffStart >= 2000.f)
					{
						Stretches.Add(FString::Printf(TEXT("%.0f-%.0f m"), OffStart / 100.f, D / 100.f));
						Total += D - OffStart;
					}
					OffStart = -1.f;
				}
			}
			if (Stretches.Num() > 0)
			{
				++OffRoutes;
				Plot.Label(Worst, Route->RouteTag.ToString().RightChop(11), FColor(255, 120, 120), 1);
				UE_LOG(LogATC, Display, TEXT("ATC.CheckRoutes: OFF %s %.0f m of %.0f m: %s"), *Route->RouteTag.ToString(), Total / 100.f, Length / 100.f,
					*FString::Join(Stretches, TEXT(", ")));
			}

			// An arrival route starts on its landing runway: the runway behind the start is where landings touch down
			if (Route->RouteTag.ToString().Contains(TEXT(".Arr")))
			{
				// The direction from two points: the spline tangent at its very first point can be zero
				const FVector Start = Route->Spline->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
				const FVector Direction = (Route->Spline->GetLocationAtDistanceAlongSpline(FMath::Min(2000.f, Length), ESplineCoordinateSpace::World) - Start).GetSafeNormal2D();
				for (const FMapRunway& Runway : Site.Runways)
				{
					double Along = 0.0, Side = 0.0;
					Runway.Locate(Start, Along, Side);
					if (Side < Runway.HalfWidthCm && Along > 0.0 && Along < Runway.Length()
						&& FMath::Abs(FVector::DotProduct(Direction, Runway.Along())) > 0.95)
					{
						Route->Modify();
						Route->RunwayBeforeStartCm = FVector::DotProduct(Direction, Runway.Along()) > 0.0 ? Along : Runway.Length() - Along;
						if (UPackage* External = Route->GetExternalPackage())
						{
							Packages.AddUnique(External);
						}
						UE_LOG(LogATC, Display, TEXT("ATC.CheckRoutes: %s starts %.0f m down runway %s"), *Route->RouteTag.ToString(),
							Route->RunwayBeforeStartCm / 100.f, *Runway.Ref);
						++Arrivals;
						break;
					}
				}
				// And it ends nose-in on its stand, along the lead-in line, not across the stand (the direction of the last
				// two metres: the tangent at the end point is zero)
				FString Ref = Route->RouteTag.ToString().RightChop(FString(TEXT("Route.EHAM.")).Len());
				Ref.Split(TEXT("."), &Ref, nullptr);
				const FVector EndDirection = (Route->GetLocationAt(Length) - Route->GetLocationAt(Length - 200.f)).GetSafeNormal2D();
				for (TActorIterator<AStand> StandIt(World); StandIt; ++StandIt)
				{
					const double Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(double(EndDirection
						| StandIt->GetActorForwardVector().GetSafeNormal2D()), -1.0, 1.0)));
					if (StandIt->StandTag.ToString() == TEXT("Stand.EHAM.") + Ref && Off > 20.0)
					{
						UE_LOG(LogATC, Warning, TEXT("ATC.CheckRoutes: %s ends %.0f degrees off the direction of its stand"), *Route->RouteTag.ToString(), Off);
					}
				}
			}
		}
		Plot.Save(TEXT("route_check"));
		UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
		UE_LOG(LogATC, Display, TEXT("ATC.CheckRoutes: %d routes leave the pavement, %d arrival routes got their runway, map saved"), OffRoutes, Arrivals);
	}

	FAutoConsoleCommand CheckRoutesCommand(TEXT("ATC.CheckRoutes"),
		TEXT("Find taxi routes of the open EHAM map that leave the pavement (log and Saved/ATC/route_check.png); stores the runway behind arrival routes"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CheckRoutes));

	const FName ParallelTag(TEXT("ATCParallel"));

	/**
	 * ATC.AddParallelRoutes: every departure and arrival route of the open EHAM map gets, where Schiphol has one, the same
	 * trip over the parallel taxiway as its Alternative: A* over the painted lines that stays off the middle of the route.
	 * Kept when it runs 30-150 m beside the route for at least 250 m, is at most 25 % longer and enters no runway the route
	 * does not enter. Same stops (crossings, holding point), apron speed zone and runway data as its route. Running it
	 * again replaces them; saves the map (copy first in Saved/Backup_before_ParallelRoutes) and plots Saved/ATC/parallel_routes.png.
	 */
	void AddParallelRoutes(const TArray<FString>& Args)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const FString MapPackage = TEXT("/Game/EHAM/EHAM");
		FSite Site;
		if (!World || World->GetOutermost()->GetName() != MapPackage)
		{
			UE_LOG(LogATC, Error, TEXT("ATC.AddParallelRoutes: open %s first"), *MapPackage);
			return;
		}
		const FString MapFile = FPackageName::LongPackageNameToFilename(MapPackage, FPackageName::GetMapPackageExtension());
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("Backup_before_ParallelRoutes") / FPaths::GetCleanFilename(MapFile);
		if (!FPaths::FileExists(Backup))
		{
			IFileManager::Get().Copy(*Backup, *MapFile);
		}
		ATCMapSurvey::LoadWholeWorld(World);
		if (!MakeEHAMSite(World, Site))
		{
			return;
		}
		const ATCMapSurvey::FNetwork& Network = *Site.Network;

		// An earlier run: its routes go, the routes they belonged to forget them
		TArray<AActor*> Old;
		TArray<ATaxiRoute*> Routes;
		for (TActorIterator<ATaxiRoute> It(World); It; ++It)
		{
			if (It->Tags.Contains(ParallelTag))
			{
				Old.Add(*It);
			}
			else if (It->RouteTag.ToString().Contains(TEXT(".Rwy")) || It->RouteTag.ToString().Contains(TEXT(".Arr")))
			{
				Routes.Add(*It);
			}
		}
		for (AActor* Actor : Old)
		{
			World->EditorDestroyActor(Actor, true);
		}
		Routes.Sort([](const ATaxiRoute& A, const ATaxiRoute& B) { return A.RouteTag.ToString() < B.RouteTag.ToString(); });

		auto PointsOf = [](const ATaxiRoute* Route)
		{
			TArray<FVector> Points;
			for (float Distance = 0.f; Distance <= Route->GetLength(); Distance += 100.f)
			{
				Points.Add(Route->Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World));
			}
			return Points;
		};
		auto LengthOf = [](const TArray<FVector>& Points)
		{
			double Length = 0.0;
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				Length += FVector::Dist2D(Points[Index - 1], Points[Index]);
			}
			return Length;
		};
		// Runways a line enters (a bit wider than the paved runway, like the crossing stops)
		auto RunwaysOf = [&Site](const TArray<FVector>& Points)
		{
			TSet<int32> Entered;
			for (int32 Index = 0; Index < Site.Runways.Num(); ++Index)
			{
				for (const FVector& Point : Points)
				{
					double Along, Side;
					Site.Runways[Index].Locate(Point, Along, Side);
					if (Along > 0.0 && Along < Site.Runways[Index].Length() && Side < Site.Runways[Index].HalfWidthCm + 1000.0)
					{
						Entered.Add(Index);
						break;
					}
				}
			}
			return Entered;
		};

		UAircraftTypeData* WideBody = LoadType(TEXT("B789"));
		UAircraftTypeData* NarrowBody = LoadType(TEXT("A320"));
		// A gate of a parked model (ATC.AddParkedGates) is built for the model's type
		TMap<FString, UAircraftTypeData*> ParkedTypes;
		for (TActorIterator<ATrafficDirector> It(World); It; ++It)
		{
			for (const FTrafficGate& Gate : It->Gates)
			{
				const FString Ref = Gate.Stand.ToString().RightChop(FString(TEXT("Stand.EHAM.")).Len());
				if (!Gate.ParkedType.IsNull() && !Algo::FindByPredicate(EHAMGates, [&Ref](const FGatePlan& Plan) { return Ref == Plan.Ref; }))
				{
					ParkedTypes.Add(Ref, Gate.ParkedType.LoadSynchronous());
				}
			}
		}
		TArray<TPair<TArray<FVector>, TArray<FVector>>> Plotted;
		int32 Added = 0;
		for (ATaxiRoute* Route : Routes)
		{
			const FString Tag = Route->RouteTag.ToString();
			const TArray<FVector> Original = PointsOf(Route);

			// The part over the painted lines: from the first to the last point on them. Before it lies the runway of an
			// arrival, after it the line-up of a departure or the last metres onto a stand; those stay as they are
			int32 FirstOn = INDEX_NONE, LastOn = INDEX_NONE;
			for (int32 Index = 0; Index < Original.Num(); ++Index)
			{
				if (Network.Nearest(Original[Index], 150.f) != INDEX_NONE)
				{
					FirstOn = FirstOn == INDEX_NONE ? Index : FirstOn;
					LastOn = Index;
				}
			}
			if (FirstOn == INDEX_NONE || LastOn - FirstOn < 300)
			{
				continue;   // points every metre: shorter than 300 m on the lines
			}
			const int32 Start = Network.Nearest(Original[FirstOn], 150.f);
			const int32 Goal = Network.Nearest(Original[LastOn], 150.f);

			// Apron and stand lines are for the route's own start and end only (no short cuts between stands); the
			// route's middle, 150 m from where it starts and ends, costs double: where a parallel taxiway runs close
			// by, that one is cheaper, elsewhere the route stays as it is
			TArray<float> Cost;
			Cost.Init(1.f, Network.Nodes.Num());
			for (int32 Node = 0; Node < Network.Nodes.Num(); ++Node)
			{
				Cost[Node] = Network.LinePart[Node] == Network.MainLinePart ? 1.f : 10.f;
			}
			for (int32 Index = 0; Index < Original.Num(); Index += 5)
			{
				TArray<int32> Near;
				Network.Query(Original[Index], 3000.f, Near);
				for (int32 Node : Near)
				{
					Cost[Node] = 1.f;
				}
			}
			for (int32 Index = FirstOn + 150; Index < LastOn - 150; Index += 5)
			{
				TArray<int32> Near;
				Network.Query(Original[Index], 1500.f, Near);
				for (int32 Node : Near)
				{
					Cost[Node] = 2.f;
				}
			}
			const TArray<int32> Path = Network.FindPath(Start, Goal, &Cost);
			if (Path.Num() < 2)
			{
				continue;
			}
			TArray<FVector> Points(Original.GetData(), FirstOn);
			Points.Append(ToPoints(Network, Path));
			Points.Append(Original.GetData() + LastOn + 1, Original.Num() - LastOn - 1);
			Points = ATCMapSurvey::RoundCorners(ATCMapSurvey::Simplify(Points, 80.f), 3500.f);

			// A real parallel lane: beside the route for a while, not far from it, not much longer, no other runways
			double Beside = 0.0, Farthest = 0.0;
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				const FVector Mid = (Points[Index - 1] + Points[Index]) * 0.5f;
				const double Away = FVector::Dist2D(Mid, Route->Spline->FindLocationClosestToWorldLocation(Mid, ESplineCoordinateSpace::World));
				Farthest = FMath::Max(Farthest, Away);
				Beside += Away > 3000.0 ? FVector::Dist2D(Points[Index - 1], Points[Index]) : 0.0;
			}
			const double Length = LengthOf(Points);
			const double RouteLength = LengthOf(Original);
			const bool bNewRunway = RunwaysOf(Points).Difference(RunwaysOf(Original)).Num() > 0;
			if (Beside < 25000.0 || Farthest > 15000.0 || Length > RouteLength * 1.25 + 30000.0 || bNewRunway)
			{
				UE_LOG(LogATC, Display, TEXT("ATC.AddParallelRoutes: %s: no parallel taxiway (%.0f m beside, up to %.0f m away, %.0f vs %.0f m%s)"), *Tag,
					Beside / 100.0, Farthest / 100.0, Length / 100.0, RouteLength / 100.0, bNewRunway ? TEXT(", crosses another runway") : TEXT(""));
				continue;
			}

			// Route.EHAM.D7.Rwy36L -> Route.EHAM.D7.Alt.Rwy36L, with the stops, zone and runway data of its route
			FString Prefix, Trip;
			Tag.Split(TEXT("."), &Prefix, &Trip, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			const FName AltTag(*FString::Printf(TEXT("%s.Alt.%s"), *Prefix, *Trip));
			if (!UGameplayTagsManager::Get().RequestGameplayTag(AltTag, false).IsValid())
			{
				UE_LOG(LogATC, Warning, TEXT("ATC.AddParallelRoutes: tag %s is not in Config/Tags/ATCSimTags.ini, %s skipped"), *AltTag.ToString(), *Tag);
				continue;
			}
			ATaxiRoute* Parallel = SpawnRoute(World, AltTag, Points);
			Parallel->Tags.Add(ParallelTag);
			Parallel->RunwayBeforeStartCm = Route->RunwayBeforeStartCm;
			for (const FSpeedZone& Zone : Route->SpeedZones)
			{
				FSpeedZone& Copy = Parallel->SpeedZones.Add_GetRef(Zone);
				Copy.StartDistance = UAircraftMovementComponent::GetDistanceClosestTo(Parallel->Spline, Route->GetLocationAt(Zone.StartDistance));
				Copy.EndDistance = Zone.EndDistance >= Route->GetLength() - 1.f ? Parallel->GetLength()
					: UAircraftMovementComponent::GetDistanceClosestTo(Parallel->Spline, Route->GetLocationAt(Zone.EndDistance));
			}
			const bool bArrival = Trip.StartsWith(TEXT("Arr"));
			const FString Ref = Prefix.RightChop(FString(TEXT("Route.EHAM.")).Len());
			FDeparture D = MakeGate(Ref, bArrival ? FString() : Trip.RightChop(3), bArrival ? Trip.RightChop(3) : FString(), NarrowBody);
			for (const FGatePlan& Plan : EHAMGates)
			{
				D.Type = Ref == Plan.Ref && Plan.bWideBody ? WideBody : D.Type;
			}
			D.Type = ParkedTypes.Contains(Ref) && ParkedTypes[Ref] ? ParkedTypes[Ref] : D.Type;
			D.TaxiRouteTag = AltTag;
			FVector RunwayStart, RunwayDirection;
			if (bArrival)
			{
				FindTakeoff(Site, D.ArrivalRunway, RunwayStart, RunwayDirection, D.ArrivalRunwayIndex);
			}
			else
			{
				FindTakeoff(Site, D.Runway, RunwayStart, RunwayDirection, D.RunwayIndex);
			}
			AddRunwayStops(Site, D, Parallel, bArrival);
			Parallel->RerunConstructionScripts();
			Route->Modify();
			Route->Alternatives = { Parallel };
			Plotted.Add({ Original, Points });
			++Added;
			UE_LOG(LogATC, Display, TEXT("ATC.AddParallelRoutes: %s -> %s: %.0f m (route %.0f m), %.0f m on the parallel taxiway, up to %.0f m beside it"),
				*Tag, *AltTag.ToString(), Length / 100.0, RouteLength / 100.0, Beside / 100.0, Farthest / 100.0);
		}

		// Check picture: routes grey, their parallel lanes cyan
		FBox2D Area(ForceInit);
		for (const ATaxiRoute* Route : Routes)
		{
			for (const FVector& Point : PointsOf(Route))
			{
				Area += FVector2D(Point);
			}
		}
		ATCMapSurvey::FPlot Plot(Area.ExpandBy(20000.0), 2400);
		for (const ATaxiRoute* Route : Routes)
		{
			const TArray<FVector> Points = PointsOf(Route);
			for (int32 Index = 10; Index < Points.Num(); Index += 10)
			{
				Plot.Line(Points[Index - 10], Points[Index], FColor(90, 90, 90), 1);
			}
		}
		for (const TPair<TArray<FVector>, TArray<FVector>>& Pair : Plotted)
		{
			for (int32 Index = 1; Index < Pair.Value.Num(); ++Index)
			{
				Plot.Line(Pair.Value[Index - 1], Pair.Value[Index], FColor(60, 220, 255), 2);
			}
		}
		Plot.Save(TEXT("parallel_routes"));
		UEditorLoadingAndSavingUtils::SavePackages({ World->GetOutermost() }, false);
		UE_LOG(LogATC, Display, TEXT("ATC.AddParallelRoutes: %d of %d routes got a parallel lane, map saved"), Added, Routes.Num());
	}

	FAutoConsoleCommand AddParallelRoutesCommand(TEXT("ATC.AddParallelRoutes"),
		TEXT("Give every EHAM departure and arrival route a parallel lane over the other taxiway where there is one (its Alternative); saves the map"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&AddParallelRoutes));

	// ---------------------------------------------------------------- The map's own parked aircraft

	const FName ParkedGateTag(TEXT("ATCParkedGate"));

	/** Type and airline of the aircraft models (Content/Datasmith/Vliegtuigen) that stand parked in the EHAM map. */
	struct FParkedModelKind
	{
		const TCHAR* Model;
		const TCHAR* ICAO;
		const TCHAR* Airline;
	};

	const FParkedModelKind ParkedModelKinds[] = {
		{ TEXT("737_TUI"), TEXT("B738"), TEXT("TFL") }, { TEXT("737-8_Transavia"), TEXT("B738"), TEXT("TRA") },
		{ TEXT("A320_Easyjet"), TEXT("A320"), TEXT("EZY") }, { TEXT("A320_Vueling"), TEXT("A320"), TEXT("VLG") },
		{ TEXT("KLM_Cityhopper"), TEXT("E190"), TEXT("KLC") },
		{ TEXT("787-9_KLM"), TEXT("B789"), TEXT("KLM") }, { TEXT("KLM_Boeing_787_9_Dreamliner"), TEXT("B789"), TEXT("KLM") },
		{ TEXT("787-10_KLM"), TEXT("B78X"), TEXT("KLM") },
		{ TEXT("777-306_KLM_orange_pride"), TEXT("B77W"), TEXT("KLM") }, { TEXT("KLM_Boeing_777_306_ER"), TEXT("B77W"), TEXT("KLM") },
		{ TEXT("Air_New_Zealand_Boeing_777"), TEXT("B77W"), TEXT("ANZ") }, { TEXT("American_Airlines_Boeing_777_323_ER_2013"), TEXT("B77W"), TEXT("AAL") },
		{ TEXT("Korean_Air_Korean_Air_Boeing_777"), TEXT("B77W"), TEXT("KAL") }, { TEXT("FedEx_Express_Boeing_777_"), TEXT("B77L"), TEXT("FDX") },
		{ TEXT("Norwegian_Long_Haul_Boeing_787_8_Dreamliner"), TEXT("B788"), TEXT("NAX") },
		{ TEXT("Delta_Air_Lines_Airbus_A330_"), TEXT("A333"), TEXT("DAL") },
		{ TEXT("Lufthansa_Airbus_A350_900XWB"), TEXT("A359"), TEXT("DLH") }, { TEXT("Singapore_Airlines_Airbus_A350"), TEXT("A359"), TEXT("SIA") },
		{ TEXT("Finnair_Airbus_A350"), TEXT("A359"), TEXT("FIN") }, { TEXT("Qatar_Airways_Airbus_A350"), TEXT("A359"), TEXT("QTR") },
		{ TEXT("China_Airlines_Cargo_China_Cargo_Boeing_747"), TEXT("B744"), TEXT("CAL") }, { TEXT("NCA_Boeing_747"), TEXT("B744"), TEXT("NCA") },
		{ TEXT("A380_Emirates"), TEXT("A388"), TEXT("UAE") }, { TEXT("Emirates_Airbus_A380_861_A6_EOI"), TEXT("A388"), TEXT("UAE") },
	};

	/** A parked aircraft of the map: the visible static or skeletal meshes from Datasmith/Vliegtuigen on one actor. */
	struct FParkedModel
	{
		AActor* Actor = nullptr;
		/** Its folder under Vliegtuigen, for example KLM_Cityhopper. */
		FString Model;
		FVector Center = FVector::ZeroVector;
	};

	TArray<FParkedModel> FindParkedModels(UWorld* World)
	{
		TArray<FParkedModel> Models;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			FParkedModel Parked;
			FBox Box(ForceInit);
			TInlineComponentArray<UMeshComponent*> Components(*It);
			for (const UMeshComponent* Component : Components)
			{
				const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Component);
				const USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Component);
				const UObject* Mesh = Static ? static_cast<const UObject*>(Static->GetStaticMesh()) : Skinned ? Skinned->GetSkinnedAsset() : nullptr;
				FString InFolder;
				if (It->IsHidden() || !Component->IsVisible() || Component->bHiddenInGame || !Mesh
					|| !Mesh->GetPathName().Split(TEXT("/Vliegtuigen/"), nullptr, &InFolder))
				{
					continue;
				}
				InFolder.Split(TEXT("/"), &Parked.Model, nullptr);
				Box += Component->Bounds.GetBox();
			}
			if (Box.IsValid)
			{
				Parked.Actor = *It;
				Parked.Center = Box.GetCenter();
				Models.Add(Parked);
			}
		}
		return Models;
	}

	/**
	 * Where a parked model stands: the tip of its nose and the direction it points in. The top of the fin is the
	 * highest point, at the back. False when the model's vertices cannot be read.
	 */
	bool ModelFrame(const AActor* Actor, FVector& OutNoseTip, FVector& OutDirection, double& OutLengthCm)
	{
		TArray<FVector> Points;
		TInlineComponentArray<UMeshComponent*> Components(Actor);
		for (const UMeshComponent* Component : Components)
		{
			const FTransform Transform = Component->GetComponentTransform();
			const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Component);
			const USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Component);
			const FMeshDescription* Description = Static && Static->GetStaticMesh() ? Static->GetStaticMesh()->GetMeshDescription(0) : nullptr;
			const USkeletalMesh* Skeletal = Skinned ? Cast<USkeletalMesh>(Skinned->GetSkinnedAsset()) : nullptr;
			if (!Component->IsVisible() || Component->bHiddenInGame)
			{
				continue;
			}
			if (Description)
			{
				const TVertexAttributesConstRef<FVector3f> Positions = FStaticMeshConstAttributes(*Description).GetVertexPositions();
				for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
				{
					Points.Add(Transform.TransformPosition(FVector(Positions[Vertex])));
				}
			}
			else if (Skeletal && Skeletal->GetImportedModel() && Skeletal->GetImportedModel()->LODModels.Num() > 0)
			{
				for (const FSkelMeshSection& Section : Skeletal->GetImportedModel()->LODModels[0].Sections)
				{
					for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
					{
						Points.Add(Transform.TransformPosition(FVector(Vertex.Position)));
					}
				}
			}
		}
		if (Points.Num() < 3)
		{
			return false;
		}
		FBox Box(Points);
		FVector Fin = Points[0];
		for (const FVector& Point : Points)
		{
			Fin = Point.Z > Fin.Z ? Point : Fin;
		}
		OutDirection = (Box.GetCenter() - Fin).GetSafeNormal2D();
		double Front = -UE_DOUBLE_BIG_NUMBER, Back = UE_DOUBLE_BIG_NUMBER;
		for (const FVector& Point : Points)
		{
			Front = FMath::Max(Front, double((Point - Box.GetCenter()) | OutDirection));
			Back = FMath::Min(Back, double((Point - Box.GetCenter()) | OutDirection));
		}
		OutNoseTip = Box.GetCenter() * FVector(1, 1, 0) + OutDirection * Front + FVector(0, 0, Box.Min.Z);
		OutLengthCm = Front - Back;
		return !OutDirection.IsNearlyZero();
	}

	/** The OSM stand under a parked model: its lead-in line within 12 m of the model's middle (a stand point within 30 m). */
	FString FindStandUnder(const FSite& Site, const FVector& Center)
	{
		FString Best;
		double BestDistance = TNumericLimits<double>::Max();
		const FVector Flat = Center * FVector(1, 1, 0);
		for (const ATCOsm::FElement* Element : Site.Osm.WithTag(TEXT("aeroway"), TEXT("parking_position")))
		{
			double Distance = TNumericLimits<double>::Max();
			for (int32 Index = 0; Index < Element->Points.Num(); ++Index)
			{
				const FVector A = Site.Geo.ToMap(Element->Points[Index]) * FVector(1, 1, 0);
				const FVector B = Site.Geo.ToMap(Element->Points[FMath::Min(Index + 1, Element->Points.Num() - 1)]) * FVector(1, 1, 0);
				Distance = FMath::Min(Distance, double(FMath::PointDistToSegment(Flat, A, B)));
			}
			if (!Element->Tag(TEXT("ref")).IsEmpty() && Distance < (Element->bWay ? 1200.0 : 3000.0) && Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Element->Tag(TEXT("ref"));
			}
		}
		return Best;
	}

	/**
	 * ATC.AddParkedGates [list]: the aircraft that stand parked in the EHAM map (static and skeletal models from
	 * Datasmith/Vliegtuigen) join the traffic. A model on one of the traffic gates gives that gate its model; any other
	 * gets a gate of its own on the OSM stand it stands on: stand, pushback, departure (36L or 24) and arrival route
	 * (36C or 36R), whichever is shortest. When the traffic starts, an aircraft of the model's type and airline takes its
	 * place (the model is hidden) and departs later in the day. New gameplay tags go into Config/Tags/ATCSimTags.ini
	 * first: then restart the editor and run it again. Afterwards ATC.CheckRoutes and ATC.AddParallelRoutes; saves the map
	 * (copy first in Saved/Backup_before_ParkedGates). "list" only logs what it would do.
	 */
	void AddParkedGates(const TArray<FString>& Args)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const FString MapPackage = TEXT("/Game/EHAM/EHAM");
		FSite Site;
		if (!World || World->GetOutermost()->GetName() != MapPackage)
		{
			UE_LOG(LogATC, Error, TEXT("ATC.AddParkedGates: open %s first"), *MapPackage);
			return;
		}
		const bool bListOnly = Args.Num() > 0 && Args[0] == TEXT("list");
		ATCMapSurvey::LoadWholeWorld(World);
		ATrafficDirector* Director = nullptr;
		for (TActorIterator<ATrafficDirector> It(World); It; ++It)
		{
			Director = *It;
		}
		if (!Director || !MakeEHAMSite(World, Site))
		{
			UE_LOG(LogATC, Error, TEXT("ATC.AddParkedGates: no traffic director in the map (ATC.SetupEHAM first)"));
			return;
		}

		// The traffic gates, and the gates of an earlier run (replaced)
		TMap<FString, const AStand*> TrafficStands;
		TSet<FGameplayTag> OldStands;
		for (TActorIterator<AStand> It(World); It; ++It)
		{
			if (It->Tags.Contains(ParkedGateTag))
			{
				OldStands.Add(It->StandTag);
			}
			else if (It->StandTag.ToString().StartsWith(TEXT("Stand.EHAM.")))
			{
				TrafficStands.Add(It->StandTag.ToString().RightChop(FString(TEXT("Stand.EHAM.")).Len()), *It);
			}
		}
		auto LengthOf = [](const TArray<FVector>& Points)
		{
			double Length = 0.0;
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				Length += FVector::Dist2D(Points[Index - 1], Points[Index]);
			}
			return Length;
		};

		struct FPlan
		{
			FParkedModel Parked;
			/** Every model on the stand: normally one, sometimes a copy on top of it. */
			TArray<TObjectPtr<AActor>> Models;
			UAircraftTypeData* Type = nullptr;
			FString Airline;
			/** How FindStand finds the stand: the OSM ref, or "#x,y,yaw" for a stand made from the model itself. */
			FString Ref;
			/** The stand in the tags (Stand.EHAM.<name>): the OSM ref, or P1, P2, ... for a stand made from the model. */
			FString Name;
			const AStand* TrafficStand = nullptr;
			FDeparture Gate;
		};
		TArray<FPlan> Plans;
		TArray<FString> MissingTags;
		int32 Skipped = 0, Copies = 0, OwnStands = 0;
		for (const FParkedModel& Parked : FindParkedModels(World))
		{
			FPlan Plan;
			Plan.Parked = Parked;
			Plan.Models = { Parked.Actor };
			const FParkedModelKind* Kind = Algo::FindByPredicate(ParkedModelKinds, [&Parked](const FParkedModelKind& Entry) { return Parked.Model == Entry.Model; });
			Plan.Type = Kind ? LoadType(Kind->ICAO) : nullptr;
			Plan.Airline = Kind ? Kind->Airline : TEXT("");
			const FString Where = FString::Printf(TEXT("%s (%s) at X=%.0f Y=%.0f m"), *Parked.Actor->GetActorLabel(), *Parked.Model, Parked.Center.X / 100.0, Parked.Center.Y / 100.0);
			if (!Plan.Type)
			{
				UE_LOG(LogATC, Warning, TEXT("ATC.AddParkedGates: %s: no aircraft type for this model, stays parked"), *Where);
				++Skipped;
				continue;
			}
			// A copy standing on top of a model that already has its gate is hidden with that one
			if (FPlan* Under = Plans.FindByPredicate([&Parked](const FPlan& Other) { return FVector::Dist2D(Other.Parked.Center, Parked.Center) < 1500.0; }))
			{
				Under->Models.Add(Parked.Actor);
				UE_LOG(LogATC, Display, TEXT("ATC.AddParkedGates: %s: a copy on top of %s, hidden with it"), *Where, *Under->Parked.Actor->GetActorLabel());
				++Copies;
				continue;
			}

			// On a traffic gate already, on an OSM stand, or else a stand where the model itself stands
			for (const TPair<FString, const AStand*>& Pair : TrafficStands)
			{
				if (FVector::Dist2D(Pair.Value->GetParkingTransform().GetLocation(), Parked.Center) < 2000.0)
				{
					Plan.Ref = Pair.Key;
				}
			}
			Plan.Ref = Plan.Ref.IsEmpty() ? FindStandUnder(Site, Parked.Center) : Plan.Ref;
			if (Plans.ContainsByPredicate([&Plan](const FPlan& Other) { return Other.Ref == Plan.Ref; }))
			{
				Plan.Ref.Reset();   // that stand has a model already: this one stands beside it
			}
			Plan.TrafficStand = TrafficStands.FindRef(Plan.Ref);
			for (const TCHAR Char : Plan.Ref)
			{
				Plan.Name += FChar::IsAlnum(Char) ? FString::Chr(Char) : FString();
			}
			FVector NoseTip, Direction;
			double Length = 0.0;
			if (Plan.Ref.IsEmpty())
			{
				if (!ModelFrame(Parked.Actor, NoseTip, Direction, Length))
				{
					UE_LOG(LogATC, Warning, TEXT("ATC.AddParkedGates: %s: not on a known stand and its shape cannot be read, stays parked"), *Where);
					++Skipped;
					continue;
				}
				// The nose gear stands about 12 % of the length behind the tip
				const FVector NoseGear = NoseTip - Direction * 0.12 * Length;
				Plan.Ref = FString::Printf(TEXT("#%.0f,%.0f,%.1f"), NoseGear.X, NoseGear.Y, FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)));
				Plan.Name = FString::Printf(TEXT("P%d"), ++OwnStands);
			}

			if (!Plan.TrafficStand)
			{
				// Its own gate. Departures by pier like the traffic gates: B, C and H to the Kaagbaan (24), D to G to the
				// Polderbaan (36L), the other runway only when there is no route; other stands (cargo, regional) the
				// shortest. Then the shortest arrival
				const FString Pier = Plan.Name.Left(1);
				const FString Preferred = Pier == TEXT("B") || Pier == TEXT("C") || Pier == TEXT("H") ? TEXT("24")
					: Pier == TEXT("D") || Pier == TEXT("E") || Pier == TEXT("F") || Pier == TEXT("G") ? TEXT("36L") : TEXT("");
				double Best = 0.0;
				for (const TCHAR* Runway : { TEXT("36L"), TEXT("24") })
				{
					FDeparture D = MakeGate(Plan.Name, Runway, FString(), Plan.Type);
					D.StandRef = Plan.Ref;
					const double Score = BuildDeparture(Site, D) ? LengthOf(D.Taxi) * (Preferred.IsEmpty() || Preferred == Runway ? 1.0 : 1000.0) : 0.0;
					if (Score > 0.0 && (Best == 0.0 || Score < Best))
					{
						Best = Score;
						Plan.Gate = D;
					}
				}
				if (Best == 0.0)
				{
					UE_LOG(LogATC, Warning, TEXT("ATC.AddParkedGates: %s: no departure route from stand %s, stays parked"), *Where, *Plan.Ref);
					++Skipped;
					continue;
				}
				double BestArrival = 0.0;
				FDeparture WithArrival = Plan.Gate;
				for (const TCHAR* Runway : { TEXT("36C"), TEXT("36R") })
				{
					FDeparture D = Plan.Gate;
					D.ArrivalRunway = Runway;
					D.ArrivalRouteTag = MakeGate(Plan.Name, Plan.Gate.Runway, Runway, Plan.Type).ArrivalRouteTag;
					if (BuildArrival(Site, D) && (BestArrival == 0.0 || LengthOf(D.Arrival) < BestArrival))
					{
						BestArrival = LengthOf(D.Arrival);
						WithArrival = D;
					}
				}
				Plan.Gate = WithArrival;
				if (BestArrival == 0.0)
				{
					Plan.Gate.ArrivalRouteTag = NAME_None;
				}

				// Tags for the stand, its routes and their parallel lanes (ATC.AddParallelRoutes)
				TArray<FName> Names = { Plan.Gate.StandTag, Plan.Gate.PushRouteTag, Plan.Gate.TaxiRouteTag,
					FName(*FString::Printf(TEXT("Route.EHAM.%s.Alt.Rwy%s"), *Plan.Name, *Plan.Gate.Runway)) };
				if (!Plan.Gate.ArrivalRouteTag.IsNone())
				{
					Names.Append({ Plan.Gate.ArrivalRouteTag, FName(*FString::Printf(TEXT("Route.EHAM.%s.Alt.Arr%s"), *Plan.Name, *Plan.Gate.ArrivalRunway)) });
				}
				for (const FName& Name : Names)
				{
					if (!UGameplayTagsManager::Get().RequestGameplayTag(Name, false).IsValid())
					{
						MissingTags.AddUnique(Name.ToString());
					}
				}
			}
			UE_LOG(LogATC, Display, TEXT("ATC.AddParkedGates: %s -> %s %s on %s"), *Where, *Plan.Type->ICAOType.ToString(), *Plan.Airline,
				Plan.TrafficStand ? *FString::Printf(TEXT("traffic gate %s"), *Plan.Name) : *FString::Printf(TEXT("new gate %s%s, departure %s %.0f m, arrival %s %.0f m"),
					*Plan.Name, Plan.Ref.StartsWith(TEXT("#")) ? TEXT(" (stand made from the model)") : TEXT(""), *Plan.Gate.Runway, LengthOf(Plan.Gate.Taxi) / 100.0,
					*Plan.Gate.ArrivalRunway, LengthOf(Plan.Gate.Arrival) / 100.0));
			Plans.Add(Plan);
		}
		UE_LOG(LogATC, Display, TEXT("ATC.AddParkedGates: %d parked models join the traffic (%d copies on top of another hidden with it), %d stay parked"),
			Plans.Num(), Copies, Skipped);
		TArray<FDeparture> NewGates;
		for (const FPlan& Plan : Plans)
		{
			if (!Plan.TrafficStand)
			{
				NewGates.Add_GetRef(Plan.Gate).StandRef = Plan.Name;
			}
		}
		PlotSite(Site, NewGates, TEXT("parked_gates"));
		for (const FMapRunway& Runway : Site.Runways)
		{
			UE_LOG(LogATC, Display, TEXT("ATC.AddParkedGates: runway %s from X=%.0f Y=%.0f to X=%.0f Y=%.0f m"), *Runway.Ref, Runway.A.X / 100.0, Runway.A.Y / 100.0,
				Runway.B.X / 100.0, Runway.B.Y / 100.0);
		}
		if (bListOnly)
		{
			return;
		}
		if (MissingTags.Num() > 0)
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ATCSim"));
			const FString Ini = Plugin.IsValid() ? Plugin->GetBaseDir() / TEXT("Config") / TEXT("Tags") / TEXT("ATCSimTags.ini") : FString();
			FString Lines;
			for (const FString& Name : MissingTags)
			{
				Lines += FString::Printf(TEXT("GameplayTagList=(Tag=\"%s\",DevComment=\"Schiphol gate of a parked model, made by ATC.AddParkedGates\")\n"), *Name);
			}
			FFileHelper::SaveStringToFile(Lines, *Ini, FFileHelper::EEncodingOptions::ForceAnsi, &IFileManager::Get(), FILEWRITE_Append);
			UE_LOG(LogATC, Display, TEXT("ATC.AddParkedGates: %d new tags in %s; restart the editor and run ATC.AddParkedGates again (map not changed)"),
				MissingTags.Num(), *Ini);
			return;
		}

		const FString MapFile = FPackageName::LongPackageNameToFilename(MapPackage, FPackageName::GetMapPackageExtension());
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("Backup_before_ParkedGates") / FPaths::GetCleanFilename(MapFile);
		if (!FPaths::FileExists(Backup))
		{
			IFileManager::Get().Copy(*Backup, *MapFile);
		}

		// An earlier run: its gates go, the traffic gates forget their models
		TArray<AActor*> Old;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(ParkedGateTag))
			{
				Old.Add(*It);
			}
		}
		for (AActor* Actor : Old)
		{
			World->EditorDestroyActor(Actor, true);
		}
		Director->Modify();
		Director->Gates.RemoveAll([&OldStands](const FTrafficGate& Gate) { return OldStands.Contains(Gate.Stand); });
		for (FTrafficGate& Gate : Director->Gates)
		{
			Gate.ParkedModels.Reset();
			Gate.ParkedType.Reset();
			Gate.ParkedAirline.Reset();
		}

		int32 Added = 0;
		for (FPlan& Plan : Plans)
		{
			FTrafficGate* Gate = nullptr;
			if (Plan.TrafficStand)
			{
				Gate = Director->Gates.FindByPredicate([&Plan](const FTrafficGate& Existing) { return Existing.Stand == Plan.TrafficStand->StandTag; });
			}
			else if (SpawnDeparture(World, Site, Plan.Gate))
			{
				for (AActor* Actor : TArray<AActor*>({ Plan.Gate.Stand, Plan.Gate.PushRoute, Plan.Gate.TaxiRoute, Plan.Gate.ArrivalRoute }))
				{
					if (Actor)
					{
						Actor->Tags.Add(ParkedGateTag);
					}
				}
				Plan.Gate.Stand->SetActorLabel(FString(TEXT("Stand_")) + Plan.Name);
				Gate = &Director->Gates.AddDefaulted_GetRef();
				Gate->Stand = Plan.Gate.Stand->StandTag;
				Gate->PushbackOption = Plan.Gate.Stand->PushbackOptions[0].OptionTag;
				Gate->DepartureRoute = Plan.Gate.TaxiRoute->RouteTag;
				Gate->ArrivalRoute = Plan.Gate.ArrivalRoute ? Plan.Gate.ArrivalRoute->RouteTag : FGameplayTag();
				Gate->bWideBody = Plan.Type->AerodromeCode >= EAerodromeCode::E;
				++Added;
			}
			if (Gate)
			{
				Gate->ParkedModels = Plan.Models;
				Gate->ParkedType = Plan.Type;
				Gate->ParkedAirline = Plan.Airline;
			}
		}
		UEditorLoadingAndSavingUtils::SavePackages({ World->GetOutermost() }, false);
		UE_LOG(LogATC, Display, TEXT("ATC.AddParkedGates: %d models on %d new gates and %d traffic gates, director has %d gates, map saved"),
			Plans.Num(), Added, Plans.Num() - Added, Director->Gates.Num());
	}

	FAutoConsoleCommand AddParkedGatesCommand(TEXT("ATC.AddParkedGates"),
		TEXT("ATC.AddParkedGates [list]: the aircraft parked in the EHAM map join the traffic (a gate each, replaced by traffic at the start); saves the map"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&AddParkedGates));

	/** ATC.SetupEHAM: the gates of EHAMGates with pushback, departure and arrival routes over the painted lines, traffic and scenarios. */
	void SetupEHAM(const TArray<FString>& Args)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const FString MapPackage = TEXT("/Game/EHAM/EHAM");
		if (!World || World->GetOutermost()->GetName() != MapPackage)
		{
			UE_LOG(LogATC, Error, TEXT("ATC.SetupEHAM: open %s first"), *MapPackage);
			return;
		}
		BackupMapOnce(MapPackage);
		FSite Site;
		if (!MakeEHAMSite(World, Site))
		{
			return;
		}
		RemoveSetupActors(World);

		UAircraftTypeData* B789 = LoadType(TEXT("B789"));
		UAircraftTypeData* A320 = LoadType(TEXT("A320"));

		// A gate that does not work is left out; G7 and G4 carry the morning scenario and must work
		TArray<FDeparture> Departures;
		for (const FGatePlan& Plan : EHAMGates)
		{
			FDeparture D = MakeGate(Plan.Ref, Plan.Departure, Plan.Arrival, Plan.bWideBody ? B789 : A320);
			if (!BuildDeparture(Site, D))
			{
				UE_LOG(LogATC, Warning, TEXT("ATC.SetupEHAM: gate %s left out (no departure route)"), Plan.Ref);
				continue;
			}
			if (!BuildArrival(Site, D))
			{
				D.Arrival.Reset();
				D.ArrivalRouteTag = NAME_None;
			}
			if (SpawnDeparture(World, Site, D))
			{
				Departures.Add(D);
			}
		}
		const FDeparture* Klm = Departures.FindByPredicate([](const FDeparture& D) { return D.StandRef == TEXT("G7"); });
		const FDeparture* Ezy = Departures.FindByPredicate([](const FDeparture& D) { return D.StandRef == TEXT("G4"); });
		if (!Klm || !Ezy)
		{
			UE_LOG(LogATC, Error, TEXT("ATC.SetupEHAM: G7 or G4 failed, map not saved"));
			return;
		}
		PlotSite(Site, Departures, TEXT("eham_routes"));
		UEditorLoadingAndSavingUtils::SavePackages({ World->GetOutermost() }, false);

		// Scenario: two departures in the morning peak, seen from Tower Centrum
		UBlueprint* AircraftBlueprint = LoadObject<UBlueprint>(nullptr, TEXT("/ATCSim/Blueprints/BP_Aircraft_Base.BP_Aircraft_Base"));
		UScenarioData* Scenario = GetOrCreateAsset<UScenarioData>(TEXT("/Game/Scenarios"), TEXT("DA_Scenario_EHAM_Ochtendpiek"));
		Scenario->Title = FText::FromString(TEXT("Schiphol ochtendpiek: G7 naar de Polderbaan, G4 naar de Kaagbaan"));
		Scenario->Airport = TSoftObjectPtr<UAirportData>(FSoftObjectPath(TEXT("/Game/Airports/EHAM/Data/DA_Airport_EHAM.DA_Airport_EHAM")));
		Scenario->TowerPosition = Tag(TEXT("Tower.EHAM.Centrum"));
		Scenario->StartTimeUTC = FDateTime(2026, 10, 13, 7, 10, 0);
		Scenario->Weather = TSoftObjectPtr<UWeatherPresetData>(FSoftObjectPath(TEXT("/Game/Environment/Weather/DA_Weather_Clear.DA_Weather_Clear")));

		auto Entry = [AircraftBlueprint](const TCHAR* Callsign, UAircraftTypeData* Type, const FDeparture& D)
		{
			FScenarioAircraft Result;
			Result.Callsign = Callsign;
			Result.Type = Type;
			Result.AircraftClass = TSoftClassPtr<AATCAircraft>(FSoftObjectPath(AircraftBlueprint ? AircraftBlueprint->GeneratedClass.Get() : AATCAircraft::StaticClass()));
			Result.Stand = Tag(D.StandTag);
			Result.TaxiRoute = Tag(D.TaxiRouteTag);
			return Result;
		};
		Scenario->Aircraft = { Entry(TEXT("KLM1023"), B789, *Klm), Entry(TEXT("EZY45LK"), A320, *Ezy) };
		// Only the pushbacks are scripted: crossing and line-up clearances are the controller's job (ATC.Cmd <callsign> Continue)
		Scenario->Events = {
			Event(5.f, TEXT("KLM1023"), TEXT("Cmd.Pushback"), FString(TEXT("Pushback.Face")) + Klm->Face),
			Event(40.f, TEXT("EZY45LK"), TEXT("Cmd.Pushback"), FString(TEXT("Pushback.Face")) + Ezy->Face) };
		UEditorLoadingAndSavingUtils::SavePackages({ Scenario->GetPackage() }, false);
		UE_LOG(LogATC, Display, TEXT("ATC.SetupEHAM: done (%d gates, scenario %s)"), Departures.Num(), *Scenario->GetName());
		AddEHAMTraffic(World);
	}

	FAutoConsoleCommand SetupEHAMCommand(TEXT("ATC.SetupEHAM"),
		TEXT("Place stands G7/G4 and their pushback and taxi routes in the EHAM map (over the painted lines) and make the EHAM scenario"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetupEHAM));

	// ---------------------------------------------------------------- Checking OSM data

	/** ATC.PlotOsm <ICAO> <lat> <lon>: OSM lines, aprons and holding points around a point, in metres (to choose stands). */
	void PlotOsm(const TArray<FString>& Args)
	{
		if (Args.Num() < 3)
		{
			return;
		}
		FSite Site;
		Site.ICAO = Args[0];
		Site.Geo.Latitude = FCString::Atod(*Args[1]);
		Site.Geo.Longitude = FCString::Atod(*Args[2]);
		Site.Geo.NorthYawDeg = -90.f;
		if (!Site.Osm.Load(Site.ICAO))
		{
			return;
		}
		Site.Runways = MapRunways(Site.Geo, ATCOsm::Runways(Site.Osm), {});
		AddOsmHoldingAndAprons(Site);
		BuildOsmNetwork(Site, [](const FVector&) { return 0.0; });
		const double Half = Args.Num() > 3 ? FCString::Atod(*Args[3]) * 100.0 : 150000.0;
		ATCMapSurvey::FPlot Plot(FBox2D(FVector2D(-Half, -Half), FVector2D(Half, Half)), 1800);
		Plot.Grid(Half > 50000.0 ? 10000.f : 1000.f, FColor(45, 50, 60), true);
		for (const TArray<FVector2D>& Apron : Site.Aprons)
		{
			for (int32 Index = 1; Index < Apron.Num(); ++Index)
			{
				Plot.Line(FVector(Apron[Index - 1], 0.0), FVector(Apron[Index], 0.0), FColor(90, 90, 120), 1);
			}
		}
		// The connected taxiway system yellow, loose parts in other colours
		for (int32 Node = 0; Node < Site.Network->Nodes.Num(); ++Node)
		{
			const int32 Part = Site.Network->Part[Node];
			Plot.Point(Site.Network->Nodes[Node], Part == Site.Network->MainPart ? FColor(240, 200, 30)
				: FLinearColor::MakeFromHSV8(uint8(Part * 53), 200, 255).ToFColor(true), Half > 50000.0 ? 0 : 1);
		}
		for (const FMapRunway& Runway : Site.Runways)
		{
			Plot.Line(Runway.A, Runway.B, FColor(200, 200, 200), 3);
		}
		for (const FVector& Hold : Site.HoldMarkings)
		{
			Plot.Point(Hold, FColor::Red, 3);
		}
		for (const ATCOsm::FElement* Stand : Site.Osm.WithTag(TEXT("aeroway"), TEXT("parking_position")))
		{
			if (Stand->bWay)
			{
				Plot.Label(Site.Geo.ToMap(Stand->Points.Last()), Stand->Tag(TEXT("ref")), FColor::White, 2);
			}
		}
		Plot.Point(FVector::ZeroVector, FColor::Green, 5);
		Plot.Save(FString::Printf(TEXT("osm_%s"), *Site.ICAO));
	}

	FAutoConsoleCommand PlotOsmCommand(TEXT("ATC.PlotOsm"), TEXT("ATC.PlotOsm <ICAO> <lat> <lon> [half size m]: plot the OSM airport data around a point (north up)"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PlotOsm));
}
