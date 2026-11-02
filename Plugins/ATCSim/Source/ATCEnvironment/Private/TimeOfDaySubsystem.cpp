#include "TimeOfDaySubsystem.h"
#include "ATCSimClockSubsystem.h"
#include "ATCUnits.h"
#include "LightingRigComponent.h"
#include "SunPosition.h"

namespace
{
	constexpr float SunriseElevation = -0.833f;   // standard: refraction plus the sun's radius

	/** The light turns again after the sun has moved this far (elevation or azimuth). */
	constexpr float SunStepDeg = 0.1f;
}

void UTimeOfDaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UATCSimClockSubsystem* Clock = Collection.InitializeDependency<UATCSimClockSubsystem>())
	{
		Clock->OnSimTimeSet.AddDynamic(this, &UTimeOfDaySubsystem::HandleTimeSet);
	}
}

TStatId UTimeOfDaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeOfDaySubsystem, STATGROUP_Tickables);
}

bool UTimeOfDaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTimeOfDaySubsystem::ComputeSun(double InLatitude, double InLongitude, const FDateTime& TimeUTC, float& OutElevation, float& OutAzimuth)
{
	FSunPositionData Data;
	USunPositionFunctionLibrary::GetSunPosition(InLatitude, InLongitude, 0.f, false, TimeUTC.GetYear(), TimeUTC.GetMonth(), TimeUTC.GetDay(),
		TimeUTC.GetHour(), TimeUTC.GetMinute(), TimeUTC.GetSecond(), Data);
	// The plugin adds 180 degrees to the elevation for its own sky sphere; we want the real angle above the horizon
	OutElevation = Data.CorrectedElevation - 180.f;
	OutAzimuth = Data.Azimuth;
}

EDayPhase UTimeOfDaySubsystem::PhaseForElevation(float ElevationDeg)
{
	if (ElevationDeg > SunriseElevation)
	{
		return EDayPhase::Day;
	}
	if (ElevationDeg > -6.f)
	{
		return EDayPhase::CivilTwilight;
	}
	return ElevationDeg > -12.f ? EDayPhase::NauticalTwilight : EDayPhase::Night;
}

FRotator UTimeOfDaySubsystem::SunLightRotation(float ElevationDeg, float AzimuthDeg, float InNorthOffsetDeg)
{
	return FRotator(-ElevationDeg, AzimuthDeg + 180.f + InNorthOffsetDeg, 0.f);
}

void UTimeOfDaySubsystem::SetLocation(double InLatitude, double InLongitude, float InNorthOffsetDeg)
{
	Latitude = InLatitude;
	Longitude = InLongitude;
	NorthOffsetDeg = InNorthOffsetDeg;
	Update(true);
}

void UTimeOfDaySubsystem::RegisterRig(ULightingRigComponent* Rig)
{
	LightingRig = Rig;
	Update(true);
}

void UTimeOfDaySubsystem::RegisterListener(UObject* Listener)
{
	if (Listener && Listener->Implements<UDayNightListener>())
	{
		Listeners.AddUnique(Listener);
	}
}

void UTimeOfDaySubsystem::HandleTimeSet(FDateTime NewTimeUTC)
{
	Update(true);
}

void UTimeOfDaySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UATCSimClockSubsystem* Clock = GetWorld()->GetSubsystem<UATCSimClockSubsystem>();
	if (Clock && (!bHasUpdated || FMath::Abs((Clock->GetSimTimeUTC() - LastUpdate).GetTotalSeconds()) >= 1.0))
	{
		Update(false);
	}
}

void UTimeOfDaySubsystem::Update(bool bJumped)
{
	const UATCSimClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UATCSimClockSubsystem>() : nullptr;
	if (!Clock)
	{
		return;
	}

	const FDateTime Now = Clock->GetSimTimeUTC();
	const float OldElevation = Elevation;
	ComputeSun(Latitude, Longitude, Now, Elevation, Azimuth);

	FSunPositionData Data;
	USunPositionFunctionLibrary::GetSunPosition(Latitude, Longitude, 0.f, false, Now.GetYear(), Now.GetMonth(), Now.GetDay(), 12, 0, 0, Data);
	const FDateTime Midnight(Now.GetYear(), Now.GetMonth(), Now.GetDay());
	SunriseUTC = Midnight + Data.SunriseTime;
	SunsetUTC = Midnight + Data.SunsetTime;

	// Sunrise and sunset only when the clock runs through them, not when someone jumps in time
	if (bHasUpdated && !bJumped)
	{
		if (OldElevation <= SunriseElevation && Elevation > SunriseElevation)
		{
			OnSunrise.Broadcast();
		}
		else if (OldElevation > SunriseElevation && Elevation <= SunriseElevation)
		{
			OnSunset.Broadcast();
		}
	}

	const EDayPhase NewPhase = PhaseForElevation(Elevation);
	if (NewPhase != Phase || !bHasUpdated)
	{
		Phase = NewPhase;
		OnDayPhaseChanged.Broadcast(Phase);
		for (const TWeakObjectPtr<UObject>& Listener : Listeners)
		{
			if (Listener.IsValid())
			{
				IDayNightListener::Execute_OnDayPhaseChanged(Listener.Get(), Phase);
			}
		}
	}

	if (ULightingRigComponent* Rig = LightingRig.Get())
	{
		const bool bVisibleStep = FMath::Abs(Elevation - PushedElevation) >= SunStepDeg
			|| FMath::Abs(FMath::FindDeltaAngleDegrees(PushedAzimuth, Azimuth)) >= SunStepDeg;
		if (bJumped || !bHasPushed || bVisibleStep)
		{
			// Airport lighting fades in from 2 degrees above the horizon to civil twilight
			const float NightAmount = 1.f - FMath::SmoothStep(-6.f, 2.f, Elevation);
			Rig->SetSun(SunLightRotation(Elevation, Azimuth, NorthOffsetDeg), Elevation, NightAmount);
			PushedElevation = Elevation;
			PushedAzimuth = Azimuth;
			bHasPushed = true;
		}
	}

	LastUpdate = Now;
	bHasUpdated = true;
}
