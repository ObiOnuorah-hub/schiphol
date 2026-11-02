#include "WeatherSubsystem.h"
#include "ATCUnits.h"
#include "LightingRigComponent.h"

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
}

void UWeatherSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bTransitioning)
	{
		return;
	}

	Alpha = FMath::Min(Alpha + DeltaTime / Duration, 1.f);
	Current = FWeatherState::Lerp(Start, Target, FMath::SmoothStep(0.f, 1.f, Alpha));
	Push();
	if (Alpha >= 1.f)
	{
		bTransitioning = false;
		NotifyChanged();
	}
}

void UWeatherSubsystem::RegisterRig(ULightingRigComponent* Rig)
{
	LightingRig = Rig;
	Push();
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
