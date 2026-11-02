#include "MetarReport.h"

namespace
{
	bool IsDigitsAt(const FString& Text, int32 From, int32 Count)
	{
		if (From < 0 || Count <= 0 || From + Count > Text.Len())
		{
			return false;
		}
		for (int32 Index = From; Index < From + Count; ++Index)
		{
			if (!FChar::IsDigit(Text[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool AllDigits(const FString& Text)
	{
		return IsDigitsAt(Text, 0, Text.Len());
	}

	bool AllLetters(const FString& Text, int32 From)
	{
		for (int32 Index = From; Index < Text.Len(); ++Index)
		{
			if (!FChar::IsAlpha(Text[Index]))
			{
				return false;
			}
		}
		return true;
	}

	/** "M05" -> -5, "12" -> 12 */
	bool ParseTemperature(const FString& Text, float& Out)
	{
		const bool bMinus = Text.StartsWith(TEXT("M"));
		const FString Digits = bMinus ? Text.Mid(1) : Text;
		if (Digits.Len() != 2 || !AllDigits(Digits))
		{
			return false;
		}
		Out = float(FCString::Atoi(*Digits)) * (bMinus ? -1.f : 1.f);
		return true;
	}

	/** Statute miles: "3", "1/2", "P6" (more than 6), "M1/4" (less than a quarter). */
	bool ParseMiles(const FString& Text, float& Out)
	{
		FString Body = Text;
		if (Body.StartsWith(TEXT("P")) || Body.StartsWith(TEXT("M")))
		{
			Body = Body.Mid(1);
		}
		FString Numerator, Denominator;
		if (Body.Split(TEXT("/"), &Numerator, &Denominator))
		{
			if (!AllDigits(Numerator) || !AllDigits(Denominator) || FCString::Atoi(*Denominator) == 0)
			{
				return false;
			}
			Out = float(FCString::Atoi(*Numerator)) / float(FCString::Atoi(*Denominator));
			return true;
		}
		if (!AllDigits(Body))
		{
			return false;
		}
		Out = float(FCString::Atoi(*Body));
		return true;
	}

	/** The two-letter codes of present weather (ICAO Annex 3): descriptors, precipitation, obscuration, other. */
	bool IsWeatherCode(const FString& Pair)
	{
		static const TCHAR* const Codes[] = {
			TEXT("MI"), TEXT("PR"), TEXT("BC"), TEXT("DR"), TEXT("BL"), TEXT("SH"), TEXT("TS"), TEXT("FZ"),
			TEXT("DZ"), TEXT("RA"), TEXT("SN"), TEXT("SG"), TEXT("IC"), TEXT("PL"), TEXT("GR"), TEXT("GS"), TEXT("UP"),
			TEXT("BR"), TEXT("FG"), TEXT("FU"), TEXT("VA"), TEXT("DU"), TEXT("SA"), TEXT("HZ"), TEXT("PY"),
			TEXT("PO"), TEXT("SQ"), TEXT("FC"), TEXT("SS"), TEXT("DS") };
		for (const TCHAR* Code : Codes)
		{
			if (Pair == Code)
			{
				return true;
			}
		}
		return false;
	}

	/** What the weather groups add up to, decided once the temperature is known (RASN at +3 C is rain). */
	struct FWeatherGroups
	{
		bool bRain = false;
		bool bSnow = false;
		float Rate = 0.f;
	};

	/** One present-weather group, for example -SHRA, +TSRA, FZFG, BR, VCSH or RERA. False when it is not one. */
	bool ParseWeatherGroup(const FString& Token, FMetarReport& Out, FWeatherGroups& Groups)
	{
		FString Body = Token;
		float Intensity = 0.65f;
		bool bRecent = false;
		bool bVicinity = false;
		if (Body.StartsWith(TEXT("RE")) && Body.Len() >= 4)
		{
			bRecent = true;
			Body = Body.Mid(2);
		}
		else if (Body.StartsWith(TEXT("-")))
		{
			Intensity = 0.35f;
			Body = Body.Mid(1);
		}
		else if (Body.StartsWith(TEXT("+")))
		{
			Intensity = 1.f;
			Body = Body.Mid(1);
		}
		if (Body.StartsWith(TEXT("VC")))
		{
			bVicinity = true;
			Body = Body.Mid(2);
		}
		if (Body.Len() == 0 || Body.Len() % 2 != 0)
		{
			return false;
		}
		TArray<FString> Codes;
		for (int32 Index = 0; Index < Body.Len(); Index += 2)
		{
			const FString Pair = Body.Mid(Index, 2);
			if (!IsWeatherCode(Pair))
			{
				return false;
			}
			Codes.Add(Pair);
		}

		const bool bThunder = Codes.Contains(TEXT("TS"));
		const bool bRain = Codes.Contains(TEXT("RA")) || Codes.Contains(TEXT("DZ")) || Codes.Contains(TEXT("GR")) || Codes.Contains(TEXT("GS"))
			|| Codes.Contains(TEXT("UP"));
		const bool bSnow = Codes.Contains(TEXT("SN")) || Codes.Contains(TEXT("SG")) || Codes.Contains(TEXT("IC")) || Codes.Contains(TEXT("PL"));
		if (bRecent)
		{
			Out.bRecentPrecipitation |= bRain || bSnow || Codes.Contains(TEXT("SH")) || bThunder;
			return true;
		}
		if (bVicinity)
		{
			// Showers or a storm in the distance: towering clouds, but nothing falls on the airport
			Out.bConvective |= bThunder || Codes.Contains(TEXT("SH"));
			return true;
		}

		if (Codes.Contains(TEXT("FG")))
		{
			// Shallow fog or fog patches: mist over the field, not a white-out
			const bool bPartial = Codes.Contains(TEXT("MI")) || Codes.Contains(TEXT("BC")) || Codes.Contains(TEXT("PR"));
			Out.bFog |= !bPartial;
			Out.bMist |= bPartial;
		}
		if (Codes.Contains(TEXT("BR")) || Codes.Contains(TEXT("HZ")) || Codes.Contains(TEXT("FU")) || Codes.Contains(TEXT("DU")) || Codes.Contains(TEXT("SA")))
		{
			Out.bMist = true;
		}
		if (bThunder)
		{
			Out.bThunderstorm = true;
			Out.bConvective = true;
		}
		if (bRain || bSnow)
		{
			const bool bDrizzleOnly = Codes.Contains(TEXT("DZ")) && !Codes.Contains(TEXT("RA")) && !bSnow;
			float Rate = Intensity * (bDrizzleOnly ? 0.5f : 1.f);
			if (bThunder)
			{
				Rate = FMath::Min(Rate + 0.2f, 1.f);
			}
			Groups.bRain |= bRain;
			Groups.bSnow |= bSnow;
			Groups.Rate = FMath::Max(Groups.Rate, Rate);
		}
		return true;
	}
}

bool FMetarReport::Parse(const FString& Text, FMetarReport& Out)
{
	Out = FMetarReport();

	// The first line that is a report: the service may send a header or several stations
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Tokens;
		Line.ToUpper().ParseIntoArrayWS(Tokens);

		FMetarReport Report;
		FWeatherGroups Groups;
		bool bWind = false;
		bool bVisibility = false;
		int32 WholeMiles = 0;
		int32 Layers = 0;
		float LowestLayerFt = 0.f;
		float CeilingFt = 0.f;
		bool bNoClouds = false;
		for (FString Token : Tokens)
		{
			Token.RemoveFromEnd(TEXT("="));
			if (Token.IsEmpty())
			{
				continue;
			}
			// The weather now: trends and remarks are not
			if (Token == TEXT("TEMPO") || Token == TEXT("BECMG") || Token == TEXT("NOSIG") || Token == TEXT("RMK") || Token == TEXT("TREND"))
			{
				break;
			}
			if (Token == TEXT("METAR") || Token == TEXT("SPECI") || Token == TEXT("COR") || Token == TEXT("AUTO") || Token == TEXT("NIL"))
			{
				continue;
			}
			if (Report.Station.IsEmpty())
			{
				// The station comes before everything else: four letters (or a letter and digits, like K1V4)
				if (Token.Len() == 4 && FChar::IsAlpha(Token[0]) && !Token.EndsWith(TEXT("Z")))
				{
					Report.Station = Token;
				}
				continue;
			}
			// Day, hour and minute: 021225Z
			if (Token.Len() == 7 && Token.EndsWith(TEXT("Z")) && IsDigitsAt(Token, 0, 6))
			{
				Report.Day = FCString::Atoi(*Token.Mid(0, 2));
				Report.Hour = FCString::Atoi(*Token.Mid(2, 2));
				Report.Minute = FCString::Atoi(*Token.Mid(4, 2));
				continue;
			}
			// Wind: 22012KT, 22012G25KT, VRB03KT, 00000KT, 05006MPS
			const bool bKnots = Token.EndsWith(TEXT("KT"));
			const bool bMps = Token.EndsWith(TEXT("MPS"));
			const bool bKmh = Token.EndsWith(TEXT("KMH"));
			if (!bWind && (bKnots || bMps || bKmh) && Token.Len() >= 7)
			{
				const FString Body = Token.LeftChop(bKnots ? 2 : 3);
				const FString Direction = Body.Left(3);
				const FString SpeedAndGust = Body.Mid(3);
				FString Speed, Gust;
				if (!SpeedAndGust.Split(TEXT("G"), &Speed, &Gust))
				{
					Speed = SpeedAndGust;
				}
				Speed.RemoveFromStart(TEXT("P"));
				Gust.RemoveFromStart(TEXT("P"));
				if ((Direction == TEXT("VRB") || AllDigits(Direction)) && AllDigits(Speed))
				{
					const float ToKnots = bKnots ? 1.f : bMps ? 1.94384f : 1.f / 1.852f;
					Report.WindSpeedKt = float(FCString::Atoi(*Speed)) * ToKnots;
					Report.GustKt = AllDigits(Gust) ? float(FCString::Atoi(*Gust)) * ToKnots : 0.f;
					Report.WindDirectionDeg = Direction == TEXT("VRB") || Report.WindSpeedKt <= 0.f ? -1.f : float(FCString::Atoi(*Direction) % 360);
					bWind = true;
				}
				continue;
			}
			// Variable wind direction 180V250: the mean wind is enough for the sim
			if (Token.Len() == 7 && Token[3] == TEXT('V') && IsDigitsAt(Token, 0, 3) && IsDigitsAt(Token, 4, 3))
			{
				continue;
			}
			if (Token == TEXT("CAVOK"))
			{
				Report.bCavok = true;
				Report.VisibilityM = 10000.f;
				Report.bTenKmOrMore = true;
				bVisibility = true;
				bNoClouds = true;
				continue;
			}
			// Visibility in metres (9999 = 10 km or more); a second one with a direction is the minimum visibility
			if (Token.Len() >= 4 && IsDigitsAt(Token, 0, 4) && AllLetters(Token, 4) && Token.Len() <= 7)
			{
				if (!bVisibility)
				{
					const int32 Metres = FCString::Atoi(*Token.Left(4));
					Report.bTenKmOrMore = Metres >= 9999;
					Report.VisibilityM = Report.bTenKmOrMore ? 10000.f : float(Metres);
					bVisibility = true;
				}
				continue;
			}
			// Statute miles: 10SM, 1/2SM, P6SM, and "1 1/2SM" in two parts
			if (!bVisibility && Token.Len() <= 2 && AllDigits(Token))
			{
				WholeMiles = FCString::Atoi(*Token);
				continue;
			}
			if (!bVisibility && Token.EndsWith(TEXT("SM")))
			{
				float Miles = 0.f;
				if (ParseMiles(Token.LeftChop(2), Miles))
				{
					Miles += Token.Contains(TEXT("/")) ? float(WholeMiles) : 0.f;
					Report.VisibilityM = FMath::Min(Miles * 1609.34f, 10000.f);
					Report.bTenKmOrMore = Token.StartsWith(TEXT("P")) || Miles * 1609.34f >= 9999.f;
					bVisibility = true;
				}
				continue;
			}
			// Runway visual range R18R/0550N: the prevailing visibility already says it
			if (Token.StartsWith(TEXT("R")) && Token.Len() >= 5 && FChar::IsDigit(Token[1]) && Token.Contains(TEXT("/")))
			{
				continue;
			}
			// Cloud layers FEW030, SCT025CB, BKN012, OVC008TCU, VV002
			const FString Cover = Token.Left(3);
			const bool bLayer = Token.Len() >= 6 && (Cover == TEXT("FEW") || Cover == TEXT("SCT") || Cover == TEXT("BKN") || Cover == TEXT("OVC"));
			const bool bVertical = Token.Len() == 5 && Token.StartsWith(TEXT("VV"));
			if (bLayer || bVertical)
			{
				const FString Height = bLayer ? Token.Mid(3, 3) : Token.Mid(2, 3);
				const float Amount = Cover == TEXT("FEW") ? 0.15f : Cover == TEXT("SCT") ? 0.4f : Cover == TEXT("BKN") ? 0.7f : Cover == TEXT("OVC") ? 0.9f : 1.f;
				Report.CloudCoverage = Layers == 0 ? Amount : FMath::Max(Report.CloudCoverage, Amount) + 0.05f;
				++Layers;
				if (AllDigits(Height))
				{
					const float Feet = FMath::Max(float(FCString::Atoi(*Height)) * 100.f, 100.f);
					LowestLayerFt = LowestLayerFt > 0.f ? FMath::Min(LowestLayerFt, Feet) : Feet;
					if (Amount >= 0.7f)
					{
						CeilingFt = CeilingFt > 0.f ? FMath::Min(CeilingFt, Feet) : Feet;
					}
				}
				Report.bConvective |= Token.EndsWith(TEXT("CB")) || Token.EndsWith(TEXT("TCU"));
				continue;
			}
			if (Token == TEXT("NSC") || Token == TEXT("SKC") || Token == TEXT("CLR") || Token == TEXT("NCD") || Token == TEXT("NSW"))
			{
				bNoClouds |= Token != TEXT("NSW");
				continue;
			}
			// Temperature and dew point 12/08, M02/M05, 05/
			FString Left, Right;
			if (Token.Split(TEXT("/"), &Left, &Right) && ParseTemperature(Left, Report.TemperatureC))
			{
				Report.bHasTemperature = true;
				float DewPoint = 0.f;
				Report.DewPointC = ParseTemperature(Right, DewPoint) ? DewPoint : Report.TemperatureC;
				continue;
			}
			// QNH Q1018 (hPa) or A2992 (inches of mercury)
			if (Token.Len() == 5 && (Token.StartsWith(TEXT("Q")) || Token.StartsWith(TEXT("A"))) && IsDigitsAt(Token, 1, 4))
			{
				const float Value = float(FCString::Atoi(*Token.Mid(1)));
				Report.QnhHpa = Token.StartsWith(TEXT("Q")) ? Value : Value * 0.01f * 33.8639f;
				continue;
			}
			ParseWeatherGroup(Token, Report, Groups);
		}

		if (Report.Station.IsEmpty() || (!bWind && !bVisibility))
		{
			continue;
		}
		if (bNoClouds && Layers == 0)
		{
			Report.CloudCoverage = 0.f;
		}
		Report.CloudCoverage = FMath::Clamp(Report.CloudCoverage + (Report.bConvective && Layers > 0 ? 0.05f : 0.f), 0.f, 1.f);
		Report.CloudBaseFt = CeilingFt > 0.f ? CeilingFt : LowestLayerFt;
		if (Groups.bRain || Groups.bSnow)
		{
			// Rain and snow together (RASN): snow when it is cold enough for it to stay snow
			const bool bSnowNow = Groups.bSnow && (!Groups.bRain || (Report.bHasTemperature && Report.TemperatureC <= 1.f));
			Report.Precipitation = bSnowNow ? EPrecipitationType::Snow : EPrecipitationType::Rain;
			Report.PrecipitationRate = FMath::Clamp(Groups.Rate, 0.05f, 1.f);
		}
		Out = Report;
		return true;
	}
	return false;
}

FWeatherState FMetarReport::ToWeatherState(const FWeatherState& Previous) const
{
	FWeatherState State;
	const float Cover = FMath::Clamp(CloudCoverage, 0.f, 1.f);
	State.CloudCoverage = FMath::Max(Cover, 0.05f);
	State.CloudBaseFt = CloudBaseFt > 0.f ? CloudBaseFt : 4000.f;

	// "10 km or more" is often 30 km in clear air: the less cloud, the further you see (CAVOK preset 25 km, overcast 10 km)
	if (bTenKmOrMore)
	{
		State.VisibilityM = bMist ? 8000.f : FMath::Lerp(25000.f, 10000.f, Cover);
	}
	else
	{
		State.VisibilityM = FMath::Max(VisibilityM, 50.f);
	}

	State.Precipitation = Precipitation;
	State.PrecipitationRate = Precipitation == EPrecipitationType::None ? 0.f : PrecipitationRate;

	// Ground: wet in and after rain and in fog, lying snow while it stays below freezing
	float Wetness = 0.f;
	if (Precipitation == EPrecipitationType::Rain)
	{
		Wetness = FMath::Clamp(0.4f + PrecipitationRate, 0.f, 1.f);
	}
	if (bRecentPrecipitation)
	{
		Wetness = FMath::Max(Wetness, 0.5f);
	}
	if (bFog)
	{
		Wetness = FMath::Max(Wetness, 0.4f);
	}
	if (Cover >= 0.8f)
	{
		Wetness = FMath::Max(Wetness, 0.1f);
	}
	const bool bFreezing = bHasTemperature && TemperatureC <= 0.f;
	float SnowCover = 0.f;
	if (Precipitation == EPrecipitationType::Snow)
	{
		SnowCover = (!bHasTemperature || TemperatureC <= 1.f) ? FMath::Clamp(0.3f + 0.5f * PrecipitationRate, 0.f, 1.f) : 0.1f;
		Wetness = bHasTemperature && TemperatureC > 0.f ? FMath::Max(Wetness, 0.3f) : Wetness;
	}
	else if (bFreezing)
	{
		// Snow that fell earlier stays while it freezes
		SnowCover = FMath::Max(Previous.SnowCover, bRecentPrecipitation ? 0.3f : 0.f);
	}
	State.Wetness = Wetness;
	State.SnowCover = SnowCover;

	State.WindDirectionDeg = WindDirectionDeg >= 0.f ? WindDirectionDeg : Previous.WindDirectionDeg;
	State.WindSpeedKt = WindSpeedKt;
	State.DeriveSunFromSky();
	return State;
}

FString FMetarReport::Describe() const
{
	const FString Wind = WindSpeedKt <= 0.f ? FString(TEXT("calm"))
		: FString::Printf(TEXT("%s/%.0f%s kt"), WindDirectionDeg < 0.f ? TEXT("VRB") : *FString::Printf(TEXT("%03.0f"), WindDirectionDeg), WindSpeedKt,
			GustKt > WindSpeedKt ? *FString::Printf(TEXT("G%.0f"), GustKt) : TEXT(""));
	const FString Visibility = bCavok ? FString(TEXT("CAVOK")) : bTenKmOrMore ? FString(TEXT("10 km+")) : FString::Printf(TEXT("%.0f m"), VisibilityM);
	const FString Clouds = CloudCoverage <= 0.f ? FString(TEXT("no clouds")) : FString::Printf(TEXT("clouds %.0f%% base %.0f ft"), CloudCoverage * 100.f, CloudBaseFt);
	const FString Weather = Precipitation == EPrecipitationType::None ? (bFog ? FString(TEXT(", fog")) : bMist ? FString(TEXT(", mist")) : FString())
		: FString::Printf(TEXT(", %s %.2f%s"), Precipitation == EPrecipitationType::Snow ? TEXT("snow") : TEXT("rain"), PrecipitationRate,
			bThunderstorm ? TEXT(" thunderstorm") : TEXT(""));
	const FString Temperature = bHasTemperature ? FString::Printf(TEXT(", %.0f C"), TemperatureC) : FString();
	return FString::Printf(TEXT("%s %02d:%02dZ wind %s, %s, %s%s%s"), *Station, Hour, Minute, *Wind, *Visibility, *Clouds, *Weather, *Temperature);
}
