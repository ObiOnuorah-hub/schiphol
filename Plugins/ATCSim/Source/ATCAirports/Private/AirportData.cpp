#include "AirportData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/World.h"

namespace
{
	// WGS84 metres per degree at a latitude (meridian and parallel)
	double MetresPerDegreeLatitude(double LatitudeDeg)
	{
		const double Phi = FMath::DegreesToRadians(LatitudeDeg);
		return 111132.92 - 559.82 * FMath::Cos(2.0 * Phi) + 1.175 * FMath::Cos(4.0 * Phi) - 0.0023 * FMath::Cos(6.0 * Phi);
	}

	double MetresPerDegreeLongitude(double LatitudeDeg)
	{
		const double Phi = FMath::DegreesToRadians(LatitudeDeg);
		return 111412.84 * FMath::Cos(Phi) - 93.5 * FMath::Cos(3.0 * Phi) + 0.118 * FMath::Cos(5.0 * Phi);
	}
}

const FTowerPosition* UAirportData::FindTower(const FGameplayTag& Tag) const
{
	for (const FTowerPosition& Tower : TowerPositions)
	{
		if (Tower.Tag == Tag)
		{
			return &Tower;
		}
	}
	// No tag: the first tower of the airport
	return TowerPositions.Num() > 0 && !Tag.IsValid() ? &TowerPositions[0] : nullptr;
}

UAirportData* UAirportData::FindForWorld(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	const FString MapPackage = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByClass(UAirportData::StaticClass()->GetClassPathName(), Assets);
	for (const FAssetData& Asset : Assets)
	{
		UAirportData* Airport = Cast<UAirportData>(Asset.GetAsset());
		if (Airport && Airport->Map.ToSoftObjectPath().GetLongPackageName() == MapPackage)
		{
			return Airport;
		}
	}
	return nullptr;
}

FVector UAirportData::ProjectGeo(double OriginLatitude, double OriginLongitude, float NorthYawDeg, const FVector& Origin, double InLatitude, double InLongitude)
{
	// Scale at the middle latitude: the first-order error of a flat projection cancels out
	const double MidLatitude = 0.5 * (OriginLatitude + InLatitude);
	const double NorthM = (InLatitude - OriginLatitude) * MetresPerDegreeLatitude(MidLatitude);
	const double EastM = (InLongitude - OriginLongitude) * MetresPerDegreeLongitude(MidLatitude);

	const double North = FMath::DegreesToRadians(double(NorthYawDeg));
	const double East = North + UE_DOUBLE_HALF_PI;
	return Origin + FVector(NorthM * FMath::Cos(North) + EastM * FMath::Cos(East), NorthM * FMath::Sin(North) + EastM * FMath::Sin(East), 0.0) * 100.0;
}

void UAirportData::UnprojectGeo(double OriginLatitude, double OriginLongitude, float NorthYawDeg, const FVector& Origin, const FVector& Location,
	double& OutLatitude, double& OutLongitude)
{
	const FVector Delta = (Location - Origin) / 100.0;
	const double North = FMath::DegreesToRadians(double(NorthYawDeg));
	const double NorthM = Delta.X * FMath::Cos(North) + Delta.Y * FMath::Sin(North);
	const double EastM = -Delta.X * FMath::Sin(North) + Delta.Y * FMath::Cos(North);

	OutLatitude = OriginLatitude;
	for (int32 Iteration = 0; Iteration < 3; ++Iteration)
	{
		const double MidLatitude = 0.5 * (OriginLatitude + OutLatitude);
		OutLatitude = OriginLatitude + NorthM / MetresPerDegreeLatitude(MidLatitude);
		OutLongitude = OriginLongitude + EastM / MetresPerDegreeLongitude(MidLatitude);
	}
}

FVector UAirportData::GeoToMap(double InLatitude, double InLongitude) const
{
	return ProjectGeo(Latitude, Longitude, NorthOffsetDeg, MapOrigin, InLatitude, InLongitude);
}

void UAirportData::MapToGeo(const FVector& Location, double& OutLatitude, double& OutLongitude) const
{
	UnprojectGeo(Latitude, Longitude, NorthOffsetDeg, MapOrigin, Location, OutLatitude, OutLongitude);
}
