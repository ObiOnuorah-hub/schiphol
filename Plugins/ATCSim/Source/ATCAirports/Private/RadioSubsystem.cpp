#include "RadioSubsystem.h"
#include "AircraftIdentityComponent.h"
#include "AirportData.h"
#include "AirportSubsystem.h"
#include "ATCAircraft.h"
#include "ATCGameplayTags.h"
#include "ATCUnits.h"
#include "EnvironmentTypes.h"
#include "RadioVoiceData.h"
#include "Stand.h"
#include "TaxiControllerComponent.h"
#include "TaxiRoute.h"
#include "TrafficSubsystem.h"
#include "WeatherSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarRadioVolume(TEXT("atc.RadioVolume"), 0.8f, TEXT("Volume of the radio chatter, 0..1"));

	const TCHAR* DigitWords[] = { TEXT("zero"), TEXT("one"), TEXT("two"), TEXT("tree"), TEXT("four"), TEXT("fife"), TEXT("six"), TEXT("seven"), TEXT("eight"), TEXT("niner") };
	const TCHAR* LetterWords[] = { TEXT("Alfa"), TEXT("Bravo"), TEXT("Charlie"), TEXT("Delta"), TEXT("Echo"), TEXT("Foxtrot"), TEXT("Golf"), TEXT("Hotel"),
		TEXT("India"), TEXT("Juliett"), TEXT("Kilo"), TEXT("Lima"), TEXT("Mike"), TEXT("November"), TEXT("Oscar"), TEXT("Papa"), TEXT("Quebec"), TEXT("Romeo"),
		TEXT("Sierra"), TEXT("Tango"), TEXT("Uniform"), TEXT("Victor"), TEXT("Whiskey"), TEXT("X-ray"), TEXT("Yankee"), TEXT("Zulu") };

	/** ICAO airline code -> fragment and telephony designator. */
	struct FAirline { const TCHAR* Code; const TCHAR* Token; const TCHAR* Telephony; };
	const FAirline Airlines[] = { { TEXT("KLM"), TEXT("klm"), TEXT("KLM") }, { TEXT("EZY"), TEXT("ezy"), TEXT("Easy") },
		{ TEXT("FIN"), TEXT("fin"), TEXT("Finnair") }, { TEXT("TRA"), TEXT("tra"), TEXT("Transavia") }, { TEXT("BAW"), TEXT("baw"), TEXT("Speedbird") },
		{ TEXT("DLH"), TEXT("dlh"), TEXT("Lufthansa") }, { TEXT("AFR"), TEXT("afr"), TEXT("Air France") } };

	void Append(TArray<FName>& Tokens, FString& Text, const TArray<FName>& MoreTokens, const FString& MoreText)
	{
		Tokens.Append(MoreTokens);
		Text += (Text.IsEmpty() ? TEXT("") : TEXT(" ")) + MoreText;
	}

	void Append(TArray<FName>& Tokens, FString& Text, const TCHAR* Token, const TCHAR* Words)
	{
		Append(Tokens, Text, TArray<FName>{ FName(Token) }, Words);
	}
}

bool URadioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void URadioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Voice = LoadObject<URadioVoiceData>(nullptr, TEXT("/ATCSim/Radio/DA_RadioVoice.DA_RadioVoice"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Voice)
	{
		UE_LOG(LogATC, Warning, TEXT("Radio: no recorded voice (run ATC.ImportRadioVoice); the radio log still works"));
	}
	if (UTrafficSubsystem* Traffic = InWorld.GetSubsystem<UTrafficSubsystem>())
	{
		Traffic->OnAircraftEvent.AddUniqueDynamic(this, &URadioSubsystem::HandleAircraftEvent);
	}
}

void URadioSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	Super::Deinitialize();
}

void URadioSubsystem::SetEnabled(bool bEnable)
{
	bEnabled = bEnable;
	if (!bEnabled)
	{
		Queue.Reset();
	}
}

TArray<FName> URadioSubsystem::SpellCode(const FString& Code, FString& OutText)
{
	TArray<FName> Tokens;
	TArray<FString> Words;
	for (const TCHAR Character : Code.ToUpper())
	{
		if (FChar::IsDigit(Character))
		{
			Tokens.Add(FName(*FString::Printf(TEXT("d%c"), Character)));
			Words.Add(DigitWords[Character - TEXT('0')]);
		}
		else if (Character >= TEXT('A') && Character <= TEXT('Z'))
		{
			Tokens.Add(FName(*FString::Printf(TEXT("l%c"), FChar::ToLower(Character))));
			Words.Add(LetterWords[Character - TEXT('A')]);
		}
	}
	OutText = FString::Join(Words, TEXT(" "));
	return Tokens;
}

TArray<FName> URadioSubsystem::SpellRunway(const FString& Runway, FString& OutText)
{
	// "18R/36L": one direction is enough; the letter is left, right or center
	FString Designator = Runway;
	Designator.Split(TEXT("/"), &Designator, nullptr);
	TArray<FName> Tokens;
	TArray<FString> Words;
	for (const TCHAR Character : Designator.ToUpper())
	{
		if (FChar::IsDigit(Character))
		{
			Tokens.Add(FName(*FString::Printf(TEXT("d%c"), Character)));
			Words.Add(DigitWords[Character - TEXT('0')]);
		}
		else if (Character == TEXT('L') || Character == TEXT('R') || Character == TEXT('C'))
		{
			const TCHAR* Side = Character == TEXT('L') ? TEXT("left") : Character == TEXT('R') ? TEXT("right") : TEXT("center");
			Tokens.Add(FName(Side));
			Words.Add(Side);
		}
	}
	OutText = FString::Join(Words, TEXT(" "));
	return Tokens;
}

TArray<FName> URadioSubsystem::SpellCallsign(FName Callsign, FString& OutText) const
{
	const FString Text = Callsign.ToString().ToUpper();
	for (const FAirline& Airline : Airlines)
	{
		if (Text.StartsWith(Airline.Code))
		{
			FString Rest;
			TArray<FName> Tokens = { FName(Airline.Token) };
			Tokens.Append(SpellCode(Text.RightChop(FCString::Strlen(Airline.Code)), Rest));
			OutText = FString::Printf(TEXT("%s %s"), Airline.Telephony, *Rest);
			return Tokens;
		}
	}
	return SpellCode(Text, OutText);
}

void URadioSubsystem::Say(bool bPilot, const TArray<FName>& Tokens, const FString& Text, float PauseBefore)
{
	if (!bEnabled)
	{
		return;
	}
	FRadioTransmission& Transmission = Queue.AddDefaulted_GetRef();
	Transmission.bPilot = bPilot;
	Transmission.Tokens = Tokens;
	Transmission.Text = Text;
	Transmission.PauseBefore = PauseBefore;
	if (!bBusy)
	{
		bBusy = true;
		GetWorld()->GetTimerManager().SetTimer(PauseTimer, this, &URadioSubsystem::PlayNext, FMath::Max(PauseBefore, 0.05f), false);
	}
}

void URadioSubsystem::PlayNext()
{
	if (Queue.Num() == 0 || !bEnabled)
	{
		bBusy = false;
		return;
	}
	const FRadioTransmission Transmission = Queue[0];
	Queue.RemoveAt(0);

	// Written line at the moment it is said
	const FString Line = Transmission.Text;
	UE_LOG(LogATC, Display, TEXT("Radio: %s"), *Line);
	Recent.Add(Line);
	if (Recent.Num() > 8)
	{
		Recent.RemoveAt(0);
	}
	OnRadioMessage.Broadcast(Line);

	// Squelch, the fragments, squelch; a missing fragment is skipped
	OnAir.Reset();
	OnAirIndex = 0;
	if (Voice)
	{
		const TMap<FName, TObjectPtr<USoundBase>>& Set = Transmission.bPilot ? Voice->Pilot : Voice->Controller;
		auto Add = [&Set, this](const FName& Token)
		{
			if (const TObjectPtr<USoundBase>* Sound = Set.Find(Token))
			{
				OnAir.Add(*Sound);
			}
		};
		Add(TEXT("squelch_open"));
		for (const FName& Token : Transmission.Tokens)
		{
			Add(Token);
		}
		Add(TEXT("squelch_close"));
	}
	PlayFragment();
}

void URadioSubsystem::PlayFragment()
{
	if (!OnAir.IsValidIndex(OnAirIndex))
	{
		// Transmission over: the next one after its own pause
		const float Pause = Queue.Num() > 0 ? Queue[0].PauseBefore : 0.f;
		if (Queue.Num() > 0)
		{
			GetWorld()->GetTimerManager().SetTimer(PauseTimer, this, &URadioSubsystem::PlayNext, FMath::Max(Pause, 0.05f), false);
		}
		else
		{
			bBusy = false;
		}
		return;
	}
	USoundBase* Sound = OnAir[OnAirIndex++];
	// The timer drives the sequence, so the log and the screens also run without an audio device (-nosound, tests)
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(GetWorld(), Sound, CVarRadioVolume.GetValueOnGameThread());
	}
	const float Duration = Sound ? FMath::Max(Sound->GetDuration() - 0.04f, 0.05f) : 0.05f;
	GetWorld()->GetTimerManager().SetTimer(PauseTimer, this, &URadioSubsystem::PlayFragment, Duration, false);
}

FString URadioSubsystem::DepartureRunway(const AATCAircraft* Aircraft) const
{
	FString Runway;
	Aircraft->Identity->AssignedTaxiRoute.ToString().Split(TEXT(".Rwy"), nullptr, &Runway);
	return Runway;
}

void URadioSubsystem::HandleAircraftEvent(AActor* Actor, FName Callsign, FGameplayTag EventTag, const FString& Detail)
{
	AATCAircraft* Aircraft = Cast<AATCAircraft>(Actor);
	if (!bEnabled || Callsign.IsNone() || !Aircraft)
	{
		return;
	}
	FString CallText;
	const TArray<FName> Call = SpellCallsign(Callsign, CallText);
	const UGameInstance* GameInstance = GetWorld()->GetGameInstance();
	const UAirportSubsystem* Airports = GameInstance ? GameInstance->GetSubsystem<UAirportSubsystem>() : nullptr;
	const bool bSchiphol = Airports && Airports->GetActiveAirport() && Airports->GetActiveAirport()->ICAO == TEXT("EHAM");
	const FString Who = Callsign.ToString();

	// Request and approval of the pushback
	if (EventTag == TAG_ATC_Event_Pushback_State && Detail == TEXT("Requested"))
	{
		const AStand* Stand = Aircraft->Identity->Stand;
		FString StandText, DirectionText;
		const TArray<FName> StandTokens = SpellCode(Stand ? Stand->StandTag.ToString().RightChop(Stand->StandTag.ToString().Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1) : FString(), StandText);
		FString Direction = Stand && Stand->PushbackOptions.Num() > 0 ? Stand->PushbackOptions[0].OptionTag.ToString() : FString();
		Direction.RemoveFromStart(TEXT("Pushback.Face"));
		DirectionText = Direction.ToLower();

		TArray<FName> Tokens;
		FString Text;
		if (bSchiphol)
		{
			Append(Tokens, Text, TEXT("schiphol_ground"), TEXT("Schiphol Ground,"));
		}
		Append(Tokens, Text, Call, CallText + TEXT(","));
		if (StandTokens.Num() > 0)
		{
			Append(Tokens, Text, TEXT("stand"), TEXT("stand"));
			Append(Tokens, Text, StandTokens, StandText + TEXT(","));
		}
		Append(Tokens, Text, TEXT("request_pushback"), TEXT("request push back."));
		Say(true, Tokens, FString::Printf(TEXT("[%s] %s"), *Who, *Text), 1.2f);

		if (!DirectionText.IsEmpty())
		{
			Tokens.Reset();
			Text.Reset();
			Append(Tokens, Text, Call, CallText + TEXT(","));
			Append(Tokens, Text, TEXT("pushback_approved_face"), TEXT("push back approved, face"));
			Append(Tokens, Text, *DirectionText, *(DirectionText + TEXT(".")));
			Say(false, Tokens, FString::Printf(TEXT("[Ground] %s"), *Text), 0.9f);

			Tokens.Reset();
			Text.Reset();
			Append(Tokens, Text, TEXT("pushback_approved_face"), TEXT("Push back approved, face"));
			Append(Tokens, Text, *DirectionText, *(DirectionText + TEXT(",")));
			Append(Tokens, Text, Call, CallText + TEXT("."));
			Say(true, Tokens, FString::Printf(TEXT("[%s] %s"), *Who, *Text), 0.5f);
		}
	}
	// After the pushback: taxi clearance to the holding point
	else if (EventTag == TAG_ATC_Event_Pushback_Completed)
	{
		FString RunwayText;
		const TArray<FName> Runway = SpellRunway(DepartureRunway(Aircraft), RunwayText);
		TArray<FName> Tokens;
		FString Text;
		Append(Tokens, Text, Call, CallText + TEXT(","));
		Append(Tokens, Text, TEXT("ready_for_taxi"), TEXT("ready for taxi."));
		Say(true, Tokens, FString::Printf(TEXT("[%s] %s"), *Who, *Text), 1.5f);
		if (Runway.Num() > 0)
		{
			Tokens.Reset();
			Text.Reset();
			Append(Tokens, Text, Call, CallText + TEXT(","));
			Append(Tokens, Text, TEXT("taxi_to_holding_point"), TEXT("taxi to holding point, runway"));
			Append(Tokens, Text, Runway, RunwayText + TEXT("."));
			Say(false, Tokens, FString::Printf(TEXT("[Ground] %s"), *Text), 1.f);

			Tokens.Reset();
			Text.Reset();
			Append(Tokens, Text, TEXT("taxi_to_holding_point"), TEXT("Taxi to holding point, runway"));
			Append(Tokens, Text, Runway, RunwayText + TEXT(","));
			Append(Tokens, Text, Call, CallText + TEXT("."));
			Say(true, Tokens, FString::Printf(TEXT("[%s] %s"), *Who, *Text), 0.5f);
		}
	}
	// At a stop bar: ready for departure, or holding short of a runway to cross
	else if (EventTag == TAG_ATC_Event_Taxi_StopReached && Detail.Contains(TEXT("waiting for clearance")))
	{
		const FString Stop = Detail.Replace(TEXT(" (waiting for clearance)"), TEXT(""));
		WaitingAt.Add(Callsign, Stop);
		FString RunwayText;
		FString Runway = Stop;
		Runway.RemoveFromStart(TEXT("Holding point "));
		Runway.RemoveFromStart(TEXT("Runway crossing "));
		const TArray<FName> RunwayTokens = SpellRunway(Runway, RunwayText);
		TArray<FName> Tokens;
		FString Text;
		if (Stop.StartsWith(TEXT("Holding point")))
		{
			if (bSchiphol)
			{
				Append(Tokens, Text, TEXT("schiphol_tower"), TEXT("Schiphol Tower,"));
			}
			Append(Tokens, Text, Call, CallText + TEXT(","));
			Append(Tokens, Text, TEXT("holding_point"), TEXT("holding point, runway"));
			Append(Tokens, Text, RunwayTokens, RunwayText + TEXT(","));
			Append(Tokens, Text, TEXT("ready_for_departure"), TEXT("ready for departure."));
		}
		else
		{
			Append(Tokens, Text, Call, CallText + TEXT(","));
			Append(Tokens, Text, TEXT("holding_short"), TEXT("holding short, runway"));
			Append(Tokens, Text, RunwayTokens, RunwayText + TEXT("."));
		}
		Say(true, Tokens, FString::Printf(TEXT("[%s] %s"), *Who, *Text), 1.f);
	}
	// Moving again after waiting at a stop bar: the clearance was given (by the colleague or the trainee)
	else if (EventTag == TAG_ATC_Event_Taxi_State && Detail == TEXT("Taxiing") && WaitingAt.Contains(Callsign))
	{
		const FString Stop = WaitingAt.FindAndRemoveChecked(Callsign);
		FString Runway = Stop;
		Runway.RemoveFromStart(TEXT("Holding point "));
		Runway.RemoveFromStart(TEXT("Runway crossing "));
		FString RunwayText;
		const TArray<FName> RunwayTokens = SpellRunway(Runway, RunwayText);
		TArray<FName> Tokens;
		FString Text;
		if (Stop.StartsWith(TEXT("Holding point")))
		{
			// Takeoff clearance with the surface wind, rounded to 10 degrees
			FWeatherState Weather;
			if (const UWeatherSubsystem* WeatherSystem = GetWorld()->GetSubsystem<UWeatherSubsystem>())
			{
				Weather = WeatherSystem->GetCurrentWeather();
			}
			const int32 Direction = (FMath::RoundToInt(Weather.WindDirectionDeg / 10.f) * 10 + 359) % 360 + 1;
			FString DirectionText, SpeedText;
			const TArray<FName> DirectionTokens = SpellCode(FString::Printf(TEXT("%03d"), Direction), DirectionText);
			const TArray<FName> SpeedTokens = SpellCode(FString::FromInt(FMath::RoundToInt(Weather.WindSpeedKt)), SpeedText);
			Append(Tokens, Text, Call, CallText + TEXT(","));
			Append(Tokens, Text, TEXT("wind"), TEXT("wind"));
			Append(Tokens, Text, DirectionTokens, DirectionText);
			Append(Tokens, Text, TEXT("degrees"), TEXT("degrees,"));
			Append(Tokens, Text, SpeedTokens, SpeedText);
			Append(Tokens, Text, TEXT("knots"), TEXT("knots,"));
			Append(Tokens, Text, TEXT("runway"), TEXT("runway"));
			Append(Tokens, Text, RunwayTokens, RunwayText + TEXT(","));
			Append(Tokens, Text, TEXT("cleared_for_takeoff"), TEXT("cleared for takeoff."));
			Say(false, Tokens, FString::Printf(TEXT("[Tower] %s"), *Text), 0.6f);

			Tokens.Reset();
			Text.Reset();
			Append(Tokens, Text, TEXT("cleared_for_takeoff"), TEXT("Cleared for takeoff,"));
			Append(Tokens, Text, TEXT("runway"), TEXT("runway"));
			Append(Tokens, Text, RunwayTokens, RunwayText + TEXT(","));
			Append(Tokens, Text, Call, CallText + TEXT("."));
			Say(true, Tokens, FString::Printf(TEXT("[%s] %s"), *Who, *Text), 0.5f);
		}
		else
		{
			Append(Tokens, Text, Call, CallText + TEXT(","));
			Append(Tokens, Text, TEXT("cross_runway"), TEXT("cross runway"));
			Append(Tokens, Text, RunwayTokens, RunwayText + TEXT("."));
			Say(false, Tokens, FString::Printf(TEXT("[Tower] %s"), *Text), 0.6f);

			Tokens.Reset();
			Text.Reset();
			Append(Tokens, Text, TEXT("crossing_runway"), TEXT("Crossing runway"));
			Append(Tokens, Text, RunwayTokens, RunwayText + TEXT(","));
			Append(Tokens, Text, Call, CallText + TEXT("."));
			Say(true, Tokens, FString::Printf(TEXT("[%s] %s"), *Who, *Text), 0.5f);
		}
	}
	// An arrival came off the runway: it calls when the gate is known (a moment later)
	else if (EventTag == TAG_ATC_Event_Traffic_Spawned && Detail.Contains(TEXT(".Arr")))
	{
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Callsign]() { SayArrival(Callsign); }), 6.f, false);
	}
	// Airborne: off to the departure frequency
	else if (EventTag == TAG_ATC_Event_Traffic_Takeoff)
	{
		const TArray<FName> CallTokens = Call;
		const FString Spoken = CallText;
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, CallTokens, Spoken, Who]()
		{
			TArray<FName> Tokens;
			FString Text;
			Append(Tokens, Text, CallTokens, Spoken + TEXT(","));
			Append(Tokens, Text, TEXT("contact_departure"), TEXT("contact departure, good day."));
			Say(false, Tokens, FString::Printf(TEXT("[Tower] %s"), *Text), 0.8f);
			Tokens.Reset();
			Text.Reset();
			Append(Tokens, Text, TEXT("good_day"), TEXT("Good day,"));
			Append(Tokens, Text, CallTokens, Spoken + TEXT("."));
			Say(true, Tokens, FString::Printf(TEXT("[%s] %s"), *Who, *Text), 0.4f);
		}), 30.f, false);
	}
	else if (EventTag == TAG_ATC_Event_Traffic_Removed)
	{
		WaitingAt.Remove(Callsign);
	}
}

void URadioSubsystem::SayArrival(FName Callsign)
{
	const UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	const AATCAircraft* Aircraft = Traffic ? Cast<AATCAircraft>(Traffic->FindAircraft(Callsign)) : nullptr;
	if (!Aircraft || !Aircraft->TaxiController->Route)
	{
		return;
	}
	FString Runway, CallText, RunwayText, StandText;
	Aircraft->TaxiController->Route->RouteTag.ToString().Split(TEXT(".Arr"), nullptr, &Runway);
	const TArray<FName> Call = SpellCallsign(Callsign, CallText);
	const TArray<FName> RunwayTokens = SpellRunway(Runway, RunwayText);
	const FString StandCode = Aircraft->Identity->Stand ? Aircraft->Identity->Stand->StandTag.ToString().RightChop(
		Aircraft->Identity->Stand->StandTag.ToString().Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1) : FString();
	const TArray<FName> StandTokens = SpellCode(StandCode, StandText);
	const FString Who = Callsign.ToString();

	TArray<FName> Tokens;
	FString Text;
	Append(Tokens, Text, TEXT("schiphol_ground"), TEXT("Schiphol Ground,"));
	Append(Tokens, Text, Call, CallText + TEXT(","));
	Append(Tokens, Text, TEXT("vacated_runway"), TEXT("vacated runway"));
	Append(Tokens, Text, RunwayTokens, RunwayText + TEXT("."));
	Say(true, Tokens, FString::Printf(TEXT("[%s] %s"), *Who, *Text), 1.f);
	if (StandTokens.Num() > 0)
	{
		Tokens.Reset();
		Text.Reset();
		Append(Tokens, Text, Call, CallText + TEXT(","));
		Append(Tokens, Text, TEXT("taxi_to_stand"), TEXT("taxi to stand"));
		Append(Tokens, Text, StandTokens, StandText + TEXT("."));
		Say(false, Tokens, FString::Printf(TEXT("[Ground] %s"), *Text), 0.9f);

		Tokens.Reset();
		Text.Reset();
		Append(Tokens, Text, TEXT("taxi_to_stand"), TEXT("Taxi to stand"));
		Append(Tokens, Text, StandTokens, StandText + TEXT(","));
		Append(Tokens, Text, Call, CallText + TEXT("."));
		Say(true, Tokens, FString::Printf(TEXT("[%s] %s"), *Who, *Text), 0.5f);
	}
}
