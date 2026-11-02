#pragma once

#include "CoreMinimal.h"
#include "EnvironmentTypes.h"

/**
 * What a METAR says, in numbers. Parses the ICAO format the way it comes from aviationweather.gov or an ATIS:
 * "EHAM 021225Z 22012G24KT 9999 -SHRA FEW012 BKN025CB 12/08 Q1018 TEMPO 4000 SHRA". Trends (TEMPO, BECMG) and
 * remarks are skipped: the report is the weather now. US units (SM, inHg) are understood too.
 */
struct ATCENVIRONMENT_API FMetarReport
{
	FString Station;

	/** Day, hour and minute of the observation (UTC). 0 = not in the text. */
	int32 Day = 0;
	int32 Hour = 0;
	int32 Minute = 0;

	/** True direction the wind blows from; -1 = variable (VRB) or calm. */
	float WindDirectionDeg = -1.f;
	float WindSpeedKt = 0.f;
	float GustKt = 0.f;

	/** Prevailing visibility. 9999 = 10 km or more (bTenKmOrMore). */
	float VisibilityM = 10000.f;
	bool bTenKmOrMore = true;
	bool bCavok = false;

	/** Largest cover of all layers (FEW 0.15, SCT 0.4, BKN 0.7, OVC 0.9, VV 1) plus 0.05 for each further layer. */
	float CloudCoverage = 0.f;
	/** Ceiling (lowest BKN, OVC or VV), else the lowest layer; 0 = no clouds. */
	float CloudBaseFt = 0.f;
	bool bConvective = false;

	EPrecipitationType Precipitation = EPrecipitationType::None;
	/** 0..1: light 0.35, moderate 0.65, heavy 1; drizzle half of that. */
	float PrecipitationRate = 0.f;
	bool bThunderstorm = false;
	bool bFog = false;
	bool bMist = false;
	/** RERA, RESN ...: it rained or snowed in the last hour. */
	bool bRecentPrecipitation = false;

	bool bHasTemperature = false;
	float TemperatureC = 10.f;
	float DewPointC = 5.f;

	/** QNH in hPa, 0 = not given. */
	float QnhHpa = 0.f;

	/** False when the text has no station, no wind and no visibility: not a METAR. */
	static bool Parse(const FString& Text, FMetarReport& Out);

	/** The weather picture for the lighting rig. Previous gives the wind direction when the wind is variable. */
	FWeatherState ToWeatherState(const FWeatherState& Previous) const;

	/** "EHAM 12:25Z wind 220/12G24 kt, 10 km+, BKN 2500 ft, light rain, 12 C" */
	FString Describe() const;
};
