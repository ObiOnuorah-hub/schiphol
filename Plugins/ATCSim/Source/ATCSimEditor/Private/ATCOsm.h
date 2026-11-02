#pragma once

#include "CoreMinimal.h"

/**
 * OpenStreetMap data of an airport: Plugins/ATCSim/Data/OSM/<ICAO>.json, an Overpass "out geom" export.
 * Map data (c) OpenStreetMap contributors, ODbL 1.0 (https://www.openstreetmap.org/copyright).
 */
namespace ATCOsm
{
	struct FElement
	{
		int64 Id = 0;
		bool bWay = false;
		TMap<FString, FString> Tags;
		/** X = latitude, Y = longitude. A node has one point. */
		TArray<FVector2D> Points;

		FString Tag(const TCHAR* Key) const
		{
			const FString* Value = Tags.Find(Key);
			return Value ? *Value : FString();
		}
		bool IsClosed() const { return bWay && Points.Num() > 3 && Points[0].Equals(Points.Last(), 1e-9); }
		FVector2D Center() const;
	};

	struct FData
	{
		FString ICAO;
		TArray<FElement> Elements;

		bool Load(const FString& InICAO);
		TArray<const FElement*> WithTag(const TCHAR* Key, const TCHAR* Value) const;
	};

	/** A whole runway: OSM splits runways at displaced thresholds, the pieces are joined here. Ends in lat/lon. */
	struct FRunway
	{
		FString Ref;
		double WidthM = 45.0;
		FVector2D A;
		FVector2D B;
		/** Pieces in order from A to B. */
		TArray<FVector2D> Line;
	};
	TArray<FRunway> Runways(const FData& Data);

	FString DataFolder();
}
