#include "ATCAmbienceSubsystem.h"
#include "ATCSoundComponent.h"
#include "EnvironmentTypes.h"
#include "WeatherSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarAmbienceVolume(TEXT("atc.AmbienceVolume"), 1.f,
		TEXT("Volume of the tower cab sound (wind on the glass, air conditioning), 0..1; 0 = off"));
}

bool UATCAmbienceSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FATCSoundParams UATCAmbienceSubsystem::WindParams(float WindSpeedKt)
{
	const float Kt = FMath::Clamp(WindSpeedKt, 0.f, 60.f);
	FATCSoundParams Params;
	Params.NoiseLevel = FMath::Min(0.01f + 0.006f * Kt, 0.35f);   // 8 kt a soft rustle, 30 kt a howl
	Params.NoiseCutoffHz = 300.f + 15.f * Kt;
	Params.Gusts = 0.8f;
	Params.GlideSeconds = 4.f;
	return Params;
}

void UATCAmbienceSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters Spawn;
	Spawn.Name = TEXT("ATC_CabSound");
	Spawn.ObjectFlags |= RF_Transient;
	AActor* Holder = InWorld.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
	if (!Holder)
	{
		return;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Holder, TEXT("Root"));
	Holder->SetRootComponent(Root);
	Root->RegisterComponent();

	Wind = NewObject<UATCSoundComponent>(Holder, TEXT("Wind"));
	Room = NewObject<UATCSoundComponent>(Holder, TEXT("AirConditioning"));
	for (UATCSoundComponent* Sound : { Wind.Get(), Room.Get() })
	{
		Sound->SetupAttachment(Root);
		Sound->MakeAmbient();
		Sound->RegisterComponent();
	}
	SetVolume(CVarAmbienceVolume.GetValueOnGameThread());

	// Air conditioning: a steady low hiss with the hum of its fan
	FATCSoundParams Hum;
	Hum.NoiseLevel = 0.025f;
	Hum.NoiseCutoffHz = 140.f;
	Hum.ToneHz = 100.f;
	Hum.ToneLevel = 0.008f;
	Hum.GlideSeconds = 3.f;
	Room->SetParams(Hum);

	Refresh();
	InWorld.GetTimerManager().SetTimer(RefreshTimer, FTimerDelegate::CreateUObject(this, &UATCAmbienceSubsystem::Refresh), 3.f, true);
}

void UATCAmbienceSubsystem::SetVolume(float Volume)
{
	for (UATCSoundComponent* Sound : { Wind.Get(), Room.Get() })
	{
		if (Sound)
		{
			Sound->SetVolumeMultiplier(FMath::Clamp(Volume, 0.f, 1.f));
		}
	}
}

void UATCAmbienceSubsystem::Refresh()
{
	if (!Wind)
	{
		return;
	}
	SetVolume(CVarAmbienceVolume.GetValueOnGameThread());
	const UWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UWeatherSubsystem>() : nullptr;
	Wind->SetParams(WindParams(Weather ? Weather->GetCurrentWeather().WindSpeedKt : 8.f));
}
