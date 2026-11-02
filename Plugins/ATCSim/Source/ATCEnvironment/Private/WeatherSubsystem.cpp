#include "WeatherSubsystem.h"
#include "ATCSimClockSubsystem.h"
#include "ATCUnits.h"
#include "LightingRigComponent.h"
#include "TimeOfDaySubsystem.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace
{
	/** A blend shorter than this updates the rig every frame; a longer one ten times a second. */
	constexpr float SmoothPushSeconds = 30.f;

	/** The first METAR of a session comes in at once instead of blending, when it arrives this soon after the start. */
	constexpr float InstantLiveWeatherSeconds = 30.f;

	/** Not a station anybody can ask a METAR for: "Showcase", "", "EH-1". */
	bool IsIcaoCode(const FString& Code)
	{
		if (Code.Len() != 4 || !FChar::IsAlpha(Code[0]))
		{
			return false;
		}
		for (const TCHAR Char : Code)
		{
			if (!FChar::IsAlnum(Char))
			{
				return false;
			}
		}
		return true;
	}

	/** The first line of a text, trimmed: the METAR itself when the service adds an empty line. */
	FString FirstLine(const FString& Text)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (!Trimmed.IsEmpty())
			{
				return Trimmed;
			}
		}
		return FString();
	}

	const TCHAR* ModeName(EATCWeatherMode Mode)
	{
		return Mode == EATCWeatherMode::Fixed ? TEXT("fixed") : Mode == EATCWeatherMode::Dynamic ? TEXT("dynamic") : TEXT("live");
	}
}

void UWeatherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UATCSimClockSubsystem>();
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	Mode = Settings->WeatherMode;
	Station = IsIcaoCode(Settings->DefaultMetarStation) ? Settings->DefaultMetarStation.ToUpper() : FString(TEXT("EHAM"));
	Random.Initialize(int32(FDateTime::UtcNow().GetTicks() % 2147483647));
	const FVector2D Minutes = Settings->DynamicWeatherMinutes;
	DynamicTimer = Random.FRandRange(FMath::Max(Minutes.X, 1.f), FMath::Max(Minutes.Y, Minutes.X + 1.f)) * 60.f;
}

TStatId UWeatherSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWeatherSubsystem, STATGROUP_Tickables);
}

bool UWeatherSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UWeatherSubsystem::SetWeather(UWeatherPresetData* Preset, float TransitionSeconds)
{
	if (Preset)
	{
		UE_LOG(LogATC, Display, TEXT("Weather -> %s (%s) in %.0f s"), *Preset->GetName(), *Preset->MetarExample, TransitionSeconds);
		SetWeatherState(Preset->State, TransitionSeconds);
	}
}

void UWeatherSubsystem::SetWeatherState(const FWeatherState& NewState, float TransitionSeconds)
{
	if (TransitionSeconds <= 0.f)
	{
		Current = NewState;
		bTransitioning = false;
		Push();
		NotifyChanged();
		return;
	}
	Start = Current;
	Target = NewState;
	Alpha = 0.f;
	Duration = TransitionSeconds;
	bTransitioning = true;
	PushTimer = 0.f;
}

void UWeatherSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bTransitioning)
	{
		Alpha = FMath::Min(Alpha + DeltaTime / FMath::Max(Duration, 0.01f), 1.f);
		Current = FWeatherState::Lerp(Start, Target, FMath::SmoothStep(0.f, 1.f, Alpha));
		PushTimer -= DeltaTime;
		if (PushTimer <= 0.f || Alpha >= 1.f)
		{
			// A blend over minutes changes too little per frame to see: ten updates a second keep the fog and sky cheap
			PushTimer = Duration > SmoothPushSeconds ? 0.1f : 0.f;
			Push();
		}
		if (Alpha >= 1.f)
		{
			bTransitioning = false;
			NotifyChanged();
		}
	}

	// Nothing shows the weather (no rig in the map): no requests and no drift
	if (!HasRig())
	{
		return;
	}
	if (Mode == EATCWeatherMode::Live)
	{
		TickLive(DeltaTime);
	}
	// Live without an answer from the weather service: the weather still changes, until the METAR comes
	if (Mode == EATCWeatherMode::Dynamic || (Mode == EATCWeatherMode::Live && !bHasLiveWeather && FailedRequests > 0))
	{
		TickDynamic(GetSimDelta(DeltaTime));
	}
}

float UWeatherSubsystem::GetSimDelta(float DeltaTime) const
{
	const UATCSimClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UATCSimClockSubsystem>() : nullptr;
	return DeltaTime * (Clock ? Clock->GetTimeScale() : 1.f);
}

void UWeatherSubsystem::RegisterRig(ULightingRigComponent* Rig)
{
	LightingRig = Rig;
	Push();
	if (Mode == EATCWeatherMode::Live && !bRequestInFlight)
	{
		// A rig shows the weather from now on: ask for the real one straight away
		LiveTimer = 0.f;
	}
}

void UWeatherSubsystem::RegisterListener(UObject* Listener)
{
	if (Listener && Listener->Implements<UWeatherListener>())
	{
		Listeners.AddUnique(Listener);
	}
}

void UWeatherSubsystem::Push()
{
	if (ULightingRigComponent* Rig = LightingRig.Get())
	{
		Rig->SetWeather(Current);
	}
}

void UWeatherSubsystem::NotifyChanged()
{
	OnWeatherChanged.Broadcast(Current);
	for (const TWeakObjectPtr<UObject>& Listener : Listeners)
	{
		if (Listener.IsValid())
		{
			IWeatherListener::Execute_OnWeatherChanged(Listener.Get(), Current);
		}
	}
}

void UWeatherSubsystem::SetWeatherMode(EATCWeatherMode NewMode)
{
	if (NewMode == Mode)
	{
		return;
	}
	Mode = NewMode;
	if (Mode == EATCWeatherMode::Live)
	{
		FailedRequests = 0;
		LiveTimer = 0.f;
	}
	else if (Mode == EATCWeatherMode::Dynamic)
	{
		// Switched on by hand: the first change starts now, not in half an hour
		DynamicTimer = 0.f;
	}
	UE_LOG(LogATC, Display, TEXT("Weather mode: %s"), ModeName(Mode));
}

void UWeatherSubsystem::SetMetarStation(const FString& ICAO)
{
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	const FString NewStation = IsIcaoCode(ICAO) ? ICAO.ToUpper()
		: IsIcaoCode(Settings->DefaultMetarStation) ? Settings->DefaultMetarStation.ToUpper() : FString(TEXT("EHAM"));
	if (NewStation != Station)
	{
		Station = NewStation;
		bHasLiveWeather = false;
		LastMetar.Reset();
		FailedRequests = 0;
		LiveTimer = 0.f;
	}
}

void UWeatherSubsystem::RefreshLiveWeather()
{
	if (bRequestInFlight)
	{
		return;
	}
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	LiveTimer = FMath::Max(Settings->MetarRefreshMinutes, 1.f) * 60.f;
	const FString Url = Settings->MetarUrl.Replace(TEXT("{ICAO}"), *Station);
	if (Url.IsEmpty())
	{
		return;
	}

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("ATCSim/0.1 (Unreal Engine)"));
	Request->SetHeader(TEXT("Accept"), TEXT("text/plain"));
	Request->SetTimeout(15.f);
	// Weak: a request that comes back after the world has gone does nothing
	Request->OnProcessRequestComplete().BindWeakLambda(this, [this](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		bRequestInFlight = false;
		const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
		HandleMetarText(Response.IsValid() ? Response->GetContentAsString() : FString(), bConnected && Code == 200, Code);
	});
	bRequestInFlight = Request->ProcessRequest();
	if (!bRequestInFlight)
	{
		HandleMetarText(FString(), false, 0);
	}
}

void UWeatherSubsystem::RestartAutomaticWeather()
{
	if (Mode == EATCWeatherMode::Live)
	{
		// The real weather we already know, at once; and ask whether there is a newer one
		if (bHasLiveWeather && !LastMetar.IsEmpty())
		{
			const FString Known = LastMetar;
			ApplyMetar(Known, 0.f);
		}
		if (!bRequestInFlight)
		{
			LiveTimer = 0.f;
		}
	}
	else if (Mode == EATCWeatherMode::Dynamic)
	{
		const FVector2D Minutes = GetDefault<UATCSimSettings>()->DynamicWeatherMinutes;
		DynamicTimer = Random.FRandRange(FMath::Max(Minutes.X, 1.f), FMath::Max(Minutes.Y, Minutes.X + 1.f)) * 60.f;
	}
}

void UWeatherSubsystem::TickLive(float DeltaTime)
{
	LiveTimer -= DeltaTime;
	if (LiveTimer <= 0.f && !bRequestInFlight)
	{
		RefreshLiveWeather();
	}
}

void UWeatherSubsystem::HandleMetarText(const FString& Text, bool bSucceeded, int32 ResponseCode)
{
	FMetarReport Report;
	const bool bParsed = bSucceeded && FMetarReport::Parse(Text, Report);
	if (!bParsed)
	{
		++FailedRequests;
		// Offline or no report: try again soon a few times, then at the normal interval
		if (FailedRequests <= 3)
		{
			LiveTimer = FMath::Min(LiveTimer, 60.f * FailedRequests);
		}
		if (FailedRequests >= 3)
		{
			bHasLiveWeather = false;
		}
		if (FailedRequests == 1 || FailedRequests % 10 == 0)
		{
			UE_LOG(LogATC, Warning, TEXT("Live weather: no METAR for %s (%s); the weather changes by itself until there is one."), *Station,
				ResponseCode > 0 ? *FString::Printf(TEXT("HTTP %d"), ResponseCode) : TEXT("no connection"));
		}
		return;
	}
	FailedRequests = 0;
	if (Mode != EATCWeatherMode::Live)
	{
		return;
	}
	// The same report as last time: nothing changes (METARs come every 30 minutes, we ask more often)
	if (bHasLiveWeather && FirstLine(Text) == LastMetar)
	{
		return;
	}
	const bool bFirst = !bHasLiveWeather && GetWorld() && GetWorld()->GetTimeSeconds() < InstantLiveWeatherSeconds;
	ApplyMetar(Text, bFirst ? 0.f : GetDefault<UATCSimSettings>()->LiveWeatherBlendSeconds);
}

bool UWeatherSubsystem::ApplyMetar(const FString& MetarText, float TransitionSeconds)
{
	FMetarReport Report;
	if (!FMetarReport::Parse(MetarText, Report))
	{
		UE_LOG(LogATC, Warning, TEXT("Weather: not a METAR: %s"), *MetarText.Left(120));
		return false;
	}
	LastReport = Report;
	LastMetar = FirstLine(MetarText);
	bHasLiveWeather = true;
	// Variable wind keeps the direction it had
	const FWeatherState Next = Report.ToWeatherState(bTransitioning ? Target : Current);
	UE_LOG(LogATC, Display, TEXT("Weather from METAR (%s): %s"), *LastMetar, *Report.Describe());
	SetWeatherState(Next, TransitionSeconds);
	return true;
}

void UWeatherSubsystem::TickDynamic(float SimDeltaSeconds)
{
	DynamicTimer -= SimDeltaSeconds;
	if (DynamicTimer <= 0.f)
	{
		StartDynamicChange();
	}
}

void UWeatherSubsystem::StartDynamicChange()
{
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	const UWorld* World = GetWorld();
	const UATCSimClockSubsystem* Clock = World ? World->GetSubsystem<UATCSimClockSubsystem>() : nullptr;
	const UTimeOfDaySubsystem* TimeOfDay = World ? World->GetSubsystem<UTimeOfDaySubsystem>() : nullptr;
	const FDateTime Now = Clock ? Clock->GetSimTimeUTC() : FDateTime::UtcNow();
	// Solar time of the airport: mist belongs to the night and the early morning there, whatever the time zone
	const double Longitude = TimeOfDay ? TimeOfDay->GetLongitude() : 4.76;
	const double SolarHour = FMath::Fmod(Now.GetHour() + Now.GetMinute() / 60.0 + Longitude / 15.0 + 48.0, 24.0);

	const FWeatherState Next = MakeDynamicWeather(Random, bTransitioning ? Target : Current, Now.GetMonth(), FMath::FloorToInt(SolarHour));
	const float TimeScale = Clock ? FMath::Max(Clock->GetTimeScale(), 0.01f) : 1.f;
	const FVector2D Blend = Settings->DynamicBlendMinutes;
	const float BlendSeconds = FMath::Max(Random.FRandRange(FMath::Max(Blend.X, 0.1f), FMath::Max(Blend.Y, Blend.X + 0.1f)) * 60.f / TimeScale, 5.f);
	const FVector2D Interval = Settings->DynamicWeatherMinutes;
	DynamicTimer = Random.FRandRange(FMath::Max(Interval.X, 1.f), FMath::Max(Interval.Y, Interval.X + 1.f)) * 60.f;

	UE_LOG(LogATC, Display, TEXT("Dynamic weather -> clouds %.2f base %.0f ft, visibility %.0f m, %s %.2f, wind %03.0f/%.0f kt, in %.0f s"),
		Next.CloudCoverage, Next.CloudBaseFt, Next.VisibilityM, *UEnum::GetValueAsString(Next.Precipitation), Next.PrecipitationRate,
		Next.WindDirectionDeg, Next.WindSpeedKt, BlendSeconds);
	SetWeatherState(Next, BlendSeconds);
}

FWeatherState UWeatherSubsystem::MakeDynamicWeather(FRandomStream& InRandom, const FWeatherState& From, int32 Month, int32 LocalHour)
{
	enum ESky : int32 { Clear, Cloudy, Overcast, Wet, Fog, NumSkies };
	const ESky Now = From.Precipitation != EPrecipitationType::None && From.PrecipitationRate > 0.05f ? Wet
		: From.VisibilityM < 1500.f ? Fog
		: From.CloudCoverage >= 0.75f ? Overcast
		: From.CloudCoverage >= 0.3f ? Cloudy : Clear;

	// Mostly one step at a time: clear -> some cloud -> overcast -> rain, and back
	float Weights[NumSkies] = {};
	auto SetWeights = [&Weights](float InClear, float InCloudy, float InOvercast, float InWet, float InFog)
	{
		Weights[Clear] = InClear;
		Weights[Cloudy] = InCloudy;
		Weights[Overcast] = InOvercast;
		Weights[Wet] = InWet;
		Weights[Fog] = InFog;
	};
	switch (Now)
	{
	case Clear:    SetWeights(0.35f, 0.45f, 0.08f, 0.02f, 0.10f); break;
	case Cloudy:   SetWeights(0.25f, 0.30f, 0.30f, 0.15f, 0.f); break;
	case Overcast: SetWeights(0.05f, 0.30f, 0.25f, 0.35f, 0.05f); break;
	case Wet:      SetWeights(0.05f, 0.25f, 0.45f, 0.25f, 0.f); break;
	default:       SetWeights(0.30f, 0.40f, 0.15f, 0.f, 0.15f); break;
	}
	// Radiation fog forms at night and burns off in the morning
	const bool bMistHours = LocalHour < 10 || LocalHour >= 21;
	if (!bMistHours)
	{
		Weights[Cloudy] += Weights[Fog];
		Weights[Fog] = 0.f;
	}
	float Total = 0.f;
	for (const float Weight : Weights)
	{
		Total += Weight;
	}
	float Roll = InRandom.FRandRange(0.f, Total);
	int32 Pick = Cloudy;
	for (int32 Index = 0; Index < NumSkies; ++Index)
	{
		Roll -= Weights[Index];
		if (Roll <= 0.f && Weights[Index] > 0.f)
		{
			Pick = Index;
			break;
		}
	}

	const bool bWinter = Month == 12 || Month <= 2;
	const float SnowChance = bWinter ? 0.4f : (Month == 3 || Month == 11) ? 0.15f : 0.f;

	FWeatherState State;
	// The wind veers or backs a little and changes strength
	State.WindDirectionDeg = FMath::Fmod(From.WindDirectionDeg + InRandom.FRandRange(-40.f, 40.f) + 360.f, 360.f);
	State.WindSpeedKt = FMath::Clamp(From.WindSpeedKt + InRandom.FRandRange(-5.f, 5.f), 2.f, 25.f);
	switch (Pick)
	{
	case Clear:
		State.CloudCoverage = InRandom.FRandRange(0.f, 0.15f);
		State.CloudBaseFt = InRandom.FRandRange(3000.f, 6000.f);
		State.VisibilityM = InRandom.FRandRange(15000.f, 30000.f);
		State.Wetness = From.Wetness * 0.3f;
		break;
	case Cloudy:
		State.CloudCoverage = InRandom.FRandRange(0.3f, 0.6f);
		State.CloudBaseFt = InRandom.FRandRange(1800.f, 4500.f);
		State.VisibilityM = InRandom.FRandRange(10000.f, 20000.f);
		State.Wetness = From.Wetness * 0.5f;
		break;
	case Overcast:
		State.CloudCoverage = InRandom.FRandRange(0.75f, 0.95f);
		State.CloudBaseFt = InRandom.FRandRange(800.f, 2500.f);
		State.VisibilityM = InRandom.FRandRange(6000.f, 12000.f);
		State.Wetness = FMath::Max(From.Wetness * 0.7f, 0.1f);
		break;
	case Wet:
	{
		const bool bSnow = InRandom.FRand() < SnowChance;
		State.CloudCoverage = InRandom.FRandRange(0.9f, 1.f);
		State.CloudBaseFt = InRandom.FRandRange(600.f, 1500.f);
		State.Precipitation = bSnow ? EPrecipitationType::Snow : EPrecipitationType::Rain;
		State.PrecipitationRate = InRandom.FRandRange(0.25f, 0.9f);
		State.VisibilityM = bSnow ? InRandom.FRandRange(800.f, 3000.f) : InRandom.FRandRange(2500.f, 7000.f);
		State.Wetness = bSnow ? 0.f : FMath::Clamp(0.4f + State.PrecipitationRate, 0.f, 1.f);
		State.SnowCover = bSnow ? FMath::Clamp(From.SnowCover + 0.3f + 0.4f * State.PrecipitationRate, 0.f, 1.f) : 0.f;
		// Showers come with gusts
		State.WindSpeedKt = FMath::Min(State.WindSpeedKt + InRandom.FRandRange(0.f, 6.f), 30.f);
		break;
	}
	default:
		State.CloudCoverage = 1.f;
		State.CloudBaseFt = InRandom.FRandRange(100.f, 400.f);
		State.VisibilityM = InRandom.FRandRange(200.f, 900.f);
		State.Wetness = 0.4f;
		State.WindSpeedKt = InRandom.FRandRange(0.f, 4.f);
		break;
	}
	// Snow that fell stays a while in winter and melts otherwise
	if (State.Precipitation != EPrecipitationType::Snow)
	{
		State.SnowCover = bWinter ? From.SnowCover * 0.8f : 0.f;
	}
	State.DeriveSunFromSky();
	return State;
}

FString UWeatherSubsystem::Describe() const
{
	FString Line = FString::Printf(TEXT("mode %s, station %s, rig %s"), ModeName(Mode), *Station, HasRig() ? TEXT("yes") : TEXT("no (nothing changes)"));
	if (Mode == EATCWeatherMode::Live)
	{
		if (bHasLiveWeather)
		{
			Line += FString::Printf(TEXT(", METAR %s (%s)"), *LastMetar, *LastReport.Describe());
		}
		else
		{
			Line += FailedRequests > 0 ? FString::Printf(TEXT(", no METAR yet (%d requests failed), dynamic until then"), FailedRequests)
				: FString(TEXT(", waiting for the METAR"));
		}
		Line += FString::Printf(TEXT(", next request in %.0f s"), FMath::Max(LiveTimer, 0.f));
	}
	if (Mode == EATCWeatherMode::Dynamic || (Mode == EATCWeatherMode::Live && !bHasLiveWeather && FailedRequests > 0))
	{
		Line += FString::Printf(TEXT(", next change in %.1f min"), FMath::Max(DynamicTimer, 0.f) / 60.f);
	}
	if (bTransitioning)
	{
		Line += FString::Printf(TEXT(", changing: %.0f%% of %.0f s"), Alpha * 100.f, Duration);
	}
	return Line;
}
