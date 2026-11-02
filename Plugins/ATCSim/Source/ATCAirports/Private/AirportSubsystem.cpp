#include "AirportSubsystem.h"
#include "AircraftIdentityComponent.h"
#include "AircraftSpawnPoint.h"
#include "AircraftSpawnSubsystem.h"
#include "AirportData.h"
#include "ATCAircraft.h"
#include "AircraftTypeData.h"
#include "ATCSimClockSubsystem.h"
#include "ATCSimSettings.h"
#include "ATCUnits.h"
#include "EnvironmentTypes.h"
#include "LightingRigComponent.h"
#include "ScenarioData.h"
#include "TimeOfDaySubsystem.h"
#include "TrafficDirector.h"
#include "TrafficMixData.h"
#include "TrafficSubsystem.h"
#include "WeatherSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "StereoRendering.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

namespace
{
	bool IsVRActive()
	{
		return GEngine && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
	}

	TAutoConsoleVariable<int32> CVarRenderProfile(TEXT("atc.RenderProfile"), 0,
		TEXT("Render profile when a map starts playing and at scenario start: 0 = VR when a headset (or stereo) is active, 1 = always desktop, 2 = always VR (to measure on a monitor)"));
}

void UAirportSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	MapLoadedHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UAirportSubsystem::HandleMapLoaded);
	ActorsInitializedHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &UAirportSubsystem::HandleActorsInitialized);
}

void UAirportSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapLoadedHandle);
	FWorldDelegates::OnWorldInitializedActors.Remove(ActorsInitializedHandle);
	Super::Deinitialize();
}

void UAirportSubsystem::StartScenario(UScenarioData* Scenario)
{
	if (!Scenario)
	{
		return;
	}

	UWorld* World = GetGameInstance()->GetWorld();
	const UAirportData* Airport = Scenario->Airport.LoadSynchronous();
	const FString MapPackage = Airport ? Airport->Map.ToSoftObjectPath().GetLongPackageName() : FString();
	const bool bMapLoaded = World && (MapPackage.IsEmpty() || UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == MapPackage);

	if (bMapLoaded)
	{
		ApplyScenario(World, Scenario);
		return;
	}

	// Different airport: open its map first, HandleMapLoaded continues
	UE_LOG(LogATC, Display, TEXT("Scenario %s: opening %s"), *Scenario->GetName(), *MapPackage);
	PendingScenario = Scenario;
	if (APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr)
	{
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StartCameraFade(0.f, 1.f, 0.3f, FLinearColor::Black, false, true);
		}
	}
	UGameplayStatics::OpenLevelBySoftObjectPtr(World, Airport->Map);
}

void UAirportSubsystem::HandleMapLoaded(UWorld* World)
{
	if (PendingScenario && World && World->IsGameWorld())
	{
		UScenarioData* Scenario = PendingScenario;
		PendingScenario = nullptr;
		ApplyScenario(World, Scenario);
	}
}

void UAirportSubsystem::HandleActorsInitialized(const FActorsInitializedParams& Params)
{
	UWorld* World = Params.World;
	if (!World || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	// Before the first frame, also without a scenario: the EHAM map's screen space GI breaks in stereo (an engine ensure about
	// one AO texture per view, then now and then a crash in a render task)
	ApplyRenderProfile();
	// A moment after BeginPlay: a follow camera in the map (the EHAM spline aircraft) takes the view on its first tick
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &UAirportSubsystem::ApplyPlayTower, TWeakObjectPtr<UWorld>(World), 0), 0.2f, false);
}

void UAirportSubsystem::ApplyPlayTower(TWeakObjectPtr<UWorld> WeakWorld, int32 Attempt)
{
	// A scenario puts the player in its own tower
	UWorld* World = WeakWorld.Get();
	if (!World || AppliedWorld == World || PendingScenario)
	{
		return;
	}
	UAirportData* Airport = UAirportData::FindForWorld(World);
	if (!Airport)
	{
		return;
	}
	if (Attempt == 0)
	{
		ActiveAirport = Airport;
		ApplyLiveEnvironment(World, Airport);
	}
	if (!Airport->PlayTower.IsValid())
	{
		return;
	}
	// Play in the editor can take a few seconds before the game mode has spawned the player's pawn
	const APlayerController* Controller = World->GetFirstPlayerController();
	if ((!Controller || !Controller->GetPawn()) && Attempt < 600)
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UAirportSubsystem::ApplyPlayTower, WeakWorld, Attempt + 1));
		return;
	}
	ActiveAirport = Airport;
	GoToTower(Airport->PlayTower);
	UE_LOG(LogATC, Display, TEXT("Play without a scenario: %s, %s"), *Airport->ICAO.ToString(), *Airport->PlayTower.ToString());
}

void UAirportSubsystem::ApplyScenario(UWorld* World, UScenarioData* Scenario)
{
	if (!World || !Scenario || AppliedWorld == World)
	{
		return;
	}
	AppliedWorld = World;
	ActiveScenario = Scenario;
	ActiveAirport = Scenario->Airport.LoadSynchronous();

	// The scenario takes over the lighting of the map
	EnsureEnvironmentRig(World);

	// Clock, sun and weather right away, so the first frame already looks right
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	const bool bLive = Scenario->bLiveEnvironment;
	if (UATCSimClockSubsystem* Clock = World->GetSubsystem<UATCSimClockSubsystem>())
	{
		if (bLive && Settings->bRealTimeClock)
		{
			Clock->FollowRealTime();
		}
		else
		{
			Clock->SetTimeScale(Scenario->TimeScale);
			Clock->SetSimTimeUTC(Scenario->StartTimeUTC);
		}
	}
	if (UTimeOfDaySubsystem* TimeOfDay = World->GetSubsystem<UTimeOfDaySubsystem>(); TimeOfDay && ActiveAirport)
	{
		TimeOfDay->SetLocation(ActiveAirport->Latitude, ActiveAirport->Longitude, ActiveAirport->NorthOffsetDeg);
	}
	UWeatherPresetData* Weather = !Scenario->Weather.IsNull() ? Scenario->Weather.LoadSynchronous()
		: ActiveAirport ? ActiveAirport->DefaultWeather.LoadSynchronous() : nullptr;
	if (UWeatherSubsystem* WeatherSystem = World->GetSubsystem<UWeatherSubsystem>())
	{
		if (Weather)
		{
			WeatherSystem->SetWeather(Weather, 0.f);
		}
		// Live: the preset is the picture until the METAR is in (a second or so); fixed: it stays
		WeatherSystem->SetMetarStation(ActiveAirport ? ActiveAirport->ICAO.ToString() : FString());
		WeatherSystem->SetWeatherMode(bLive ? Settings->WeatherMode : EATCWeatherMode::Fixed);
		WeatherSystem->RestartAutomaticWeather();
	}

	if (UDataLayerManager* Layers = UDataLayerManager::GetDataLayerManager(World))
	{
		for (const TSoftObjectPtr<UDataLayerAsset>& LayerAsset : Scenario->ActiveDataLayers)
		{
			if (const UDataLayerInstance* Instance = Layers->GetDataLayerInstanceFromAsset(LayerAsset.LoadSynchronous()))
			{
				Layers->SetDataLayerInstanceRuntimeState(Instance, EDataLayerRuntimeState::Activated);
			}
		}
	}

	ApplyRenderProfile();

	// Tower view and traffic next frame: by then every stand and route has registered itself in BeginPlay
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UAirportSubsystem::ApplyDeferred, TWeakObjectPtr<UWorld>(World), 0));
}

void UAirportSubsystem::ApplyDeferred(TWeakObjectPtr<UWorld> WeakWorld, int32 Attempt)
{
	UWorld* World = WeakWorld.Get();
	if (!World || !ActiveScenario)
	{
		return;
	}

	const APlayerController* Controller = World->GetFirstPlayerController();
	if ((!Controller || !Controller->GetPawn()) && Attempt < 600)
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UAirportSubsystem::ApplyDeferred, WeakWorld, Attempt + 1));
		return;
	}

	GoToTower(ActiveScenario->TowerPosition);

	UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>();
	if (Traffic)
	{
		// The map's traffic director may already run from plain Play: a scenario with its own traffic starts at an empty airport
		if (!ActiveScenario->bStartTrafficDirector)
		{
			bool bStopped = false;
			for (TActorIterator<ATrafficDirector> It(World); It; ++It)
			{
				bStopped |= It->IsRunning();
				It->StopTraffic();
			}
			UAircraftSpawnSubsystem* Spawner = World->GetSubsystem<UAircraftSpawnSubsystem>();
			if (bStopped && Spawner)
			{
				TArray<FName> Callsigns = Spawner->GetQueuedCallsigns();
				for (AActor* Actor : Traffic->GetAllAircraft())
				{
					Callsigns.Add(IAircraftInfo::Execute_GetCallsign(Actor));
				}
				for (const FName& Callsign : Callsigns)
				{
					Spawner->Despawn(Callsign);
				}
			}
		}

		for (const FScenarioAircraft& Entry : ActiveScenario->Aircraft)
		{
			if (AATCAircraft* Aircraft = Traffic->SpawnAircraft(Entry.AircraftClass.LoadSynchronous(), Entry.Type.LoadSynchronous(), Entry.Callsign,
				Entry.Stand, Entry.TaxiRoute, Entry.bAutoTaxiAfterPushback))
			{
				Aircraft->Identity->bRemoveAtRouteEnd = Entry.bRemoveAtRouteEnd;
			}
		}
		ScheduleSpawns(World);
		if (ActiveScenario->bStartTrafficDirector)
		{
			for (TActorIterator<ATrafficDirector> It(World); It; ++It)
			{
				It->bAutoClearances = ActiveScenario->bAutoClearances;
				if (ActiveScenario->ArrivalsPerHour >= 0.f)
				{
					It->ArrivalsPerHour = ActiveScenario->ArrivalsPerHour;
				}
				if (!It->IsRunning())
				{
					It->StartTraffic();
				}
			}
		}

		for (const FScenarioEvent& Event : ActiveScenario->Events)
		{
			const TWeakObjectPtr<UTrafficSubsystem> WeakTraffic(Traffic);
			auto Fire = [WeakTraffic, Event]()
			{
				if (UTrafficSubsystem* Target = WeakTraffic.Get())
				{
					Target->SendCommand(Event.Callsign, Event.Command, Event.Parameter);
				}
			};
			if (Event.TimeOffsetSeconds <= 0.f)
			{
				Fire();
			}
			else
			{
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, Fire), Event.TimeOffsetSeconds, false);
			}
		}
	}

	UE_LOG(LogATC, Display, TEXT("Scenario %s started: %s, %s, %d aircraft, %d events, %d spawns"), *ActiveScenario->GetName(),
		ActiveAirport ? *ActiveAirport->ICAO.ToString() : TEXT("no airport"), *ActiveScenario->TowerPosition.ToString(),
		ActiveScenario->Aircraft.Num(), ActiveScenario->Events.Num(), ActiveScenario->Spawns.Num());
	OnScenarioStarted.Broadcast(ActiveScenario);
}

void UAirportSubsystem::EnsureEnvironmentRig(UWorld* World)
{
	UWeatherSubsystem* WeatherSystem = World ? World->GetSubsystem<UWeatherSubsystem>() : nullptr;
	if (WeatherSystem && !WeatherSystem->HasRig())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		World->SpawnActor<AEnvironmentRig>(AEnvironmentRig::StaticClass(), FTransform::Identity, Params);
		UE_LOG(LogATC, Display, TEXT("Environment rig added to %s"), *World->GetMapName());
	}
}

void UAirportSubsystem::ApplyLiveEnvironment(UWorld* World, UAirportData* Airport)
{
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	if (!World || !Airport || !Settings->bLiveEnvironmentOnPlay)
	{
		return;
	}
	EnsureEnvironmentRig(World);
	UATCSimClockSubsystem* Clock = World->GetSubsystem<UATCSimClockSubsystem>();
	if (Clock && Settings->bRealTimeClock)
	{
		Clock->FollowRealTime();
	}
	if (UTimeOfDaySubsystem* TimeOfDay = World->GetSubsystem<UTimeOfDaySubsystem>())
	{
		TimeOfDay->SetLocation(Airport->Latitude, Airport->Longitude, Airport->NorthOffsetDeg);
	}
	if (UWeatherSubsystem* WeatherSystem = World->GetSubsystem<UWeatherSubsystem>())
	{
		if (UWeatherPresetData* Weather = Airport->DefaultWeather.LoadSynchronous())
		{
			WeatherSystem->SetWeather(Weather, 0.f);
		}
		WeatherSystem->SetMetarStation(Airport->ICAO.ToString());
		WeatherSystem->SetWeatherMode(Settings->WeatherMode);
		WeatherSystem->RestartAutomaticWeather();
	}
	UE_LOG(LogATC, Display, TEXT("Live environment for %s: clock %s, weather %s"), *Airport->ICAO.ToString(),
		Clock ? *FString::Printf(TEXT("%s (%s)"), *Clock->GetZuluTimeString(), Clock->IsRealTime() ? TEXT("real time") : TEXT("set")) : TEXT("-"),
		Settings->WeatherMode == EATCWeatherMode::Live ? TEXT("live (METAR)") : Settings->WeatherMode == EATCWeatherMode::Dynamic ? TEXT("dynamic") : TEXT("fixed"));
}

namespace
{
	/** KLM1001 + 2 = KLM1003: the trailing number counts up; without a number, -2, -3 is added. */
	FName NthCallsign(FName First, int32 Index)
	{
		if (First.IsNone() || Index == 0)
		{
			return First;
		}
		const FString Text = First.ToString();
		int32 Split = Text.Len();
		while (Split > 0 && FChar::IsDigit(Text[Split - 1]))
		{
			--Split;
		}
		if (Split == Text.Len())
		{
			return FName(*FString::Printf(TEXT("%s-%d"), *Text, Index + 1));
		}
		return FName(*FString::Printf(TEXT("%s%d"), *Text.Left(Split), FCString::Atoi(*Text.Mid(Split)) + Index));
	}
}

void UAirportSubsystem::ScheduleSpawns(UWorld* World)
{
	for (const FScenarioSpawn& Spawn : ActiveScenario->Spawns)
	{
		for (int32 Index = 0; Index < FMath::Max(Spawn.Count, 1); ++Index)
		{
			const TWeakObjectPtr<UWorld> WeakWorld(World);
			auto Fire = [WeakWorld, Spawn, Index]()
			{
				UWorld* Target = WeakWorld.Get();
				UAircraftSpawnSubsystem* Spawner = Target ? Target->GetSubsystem<UAircraftSpawnSubsystem>() : nullptr;
				if (!Spawner)
				{
					return;
				}
				FAircraftSpawnRequest Request;
				if (Spawn.SpawnPoint.IsValid())
				{
					const AAircraftSpawnPoint* Point = Spawner->FindSpawnPoint(Spawn.SpawnPoint);
					if (!Point)
					{
						UE_LOG(LogATC, Warning, TEXT("Scenario spawn: spawn point %s not in this map."), *Spawn.SpawnPoint.ToString());
						return;
					}
					Request = Point->MakeRequest();
				}
				else
				{
					Request.RouteTag = Spawn.Route;
					Request.StartDistanceM = Spawn.StartDistanceM;
					Request.EndStop = Spawn.EndStop;
				}
				Request.Callsign = NthCallsign(Spawn.Callsign, Index);
				if (!Spawn.Type.IsNull())
				{
					Request.Type = Spawn.Type.LoadSynchronous();
				}
				if (!Spawn.TrafficMix.IsNull())
				{
					Request.TrafficMix = Spawn.TrafficMix.LoadSynchronous();
				}
				Spawner->RequestSpawn(Request);
			};
			const float Delay = Spawn.TimeOffsetSeconds + Index * Spawn.IntervalSeconds;
			if (Delay <= 0.f)
			{
				Fire();
			}
			else
			{
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, Fire), Delay, false);
			}
		}
	}
}

bool UAirportSubsystem::GoToTower(FGameplayTag TowerTag)
{
	UWorld* World = GetGameInstance()->GetWorld();
	const FTowerPosition* Tower = ActiveAirport ? ActiveAirport->FindTower(TowerTag) : nullptr;
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Tower || !Pawn)
	{
		UE_LOG(LogATC, Warning, TEXT("GoToTower %s: %s."), *TowerTag.ToString(), !Tower ? TEXT("no such tower position at this airport")
			: Controller ? TEXT("the player controller has no pawn") : TEXT("no player controller"));
		return false;
	}

	// VR tracks the real head height from the floor; a desktop view gets the eye height
	FVector Location = Tower->Transform.GetLocation();
	if (!IsVRActive())
	{
		Location.Z += Tower->EyeHeightM * ATCUnits::MetersToCm - Pawn->BaseEyeHeight;
	}
	const FRotator View = Tower->Transform.Rotator();
	Pawn->SetActorLocationAndRotation(Location, FRotator(0.f, View.Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	Controller->SetControlRotation(View);
	// A follow camera in the map (the EHAM spline aircraft) may have taken the view: the tower is the player's own eye
	if (Controller->GetViewTarget() != Pawn)
	{
		Controller->SetViewTarget(Pawn);
	}
	if (Controller->PlayerCameraManager)
	{
		Controller->PlayerCameraManager->UnlockFOV();
		Controller->PlayerCameraManager->StartCameraFade(1.f, 0.f, 0.75f, FLinearColor::Black);
	}
	return true;
}

void UAirportSubsystem::ApplyRenderProfile()
{
	const UATCSimSettings* Settings = GetDefault<UATCSimSettings>();
	const int32 Mode = CVarRenderProfile.GetValueOnGameThread();
	const bool bVR = Mode == 2 || (Mode == 0 && IsVRActive());
	for (const TPair<FString, FString>& Pair : bVR ? Settings->VRProfile : Settings->DesktopProfile)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Pair.Key))
		{
			Variable->Set(*Pair.Value, ECVF_SetByCode);
		}
	}
	// A post process volume can choose its own GI and reflections (the EHAM map turns on screen space GI there):
	// that wins over the console variables, so in VR those overrides are switched off too
	int32 Overrides = 0;
	if (bVR && GetGameInstance()->GetWorld())
	{
		for (TActorIterator<APostProcessVolume> It(GetGameInstance()->GetWorld()); It; ++It)
		{
			FPostProcessSettings& Post = It->Settings;
			if (Post.bOverride_DynamicGlobalIlluminationMethod && Post.DynamicGlobalIlluminationMethod != EDynamicGlobalIlluminationMethod::None)
			{
				Post.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
				++Overrides;
			}
			if (Post.bOverride_ReflectionMethod && Post.ReflectionMethod != EReflectionMethod::None)
			{
				Post.ReflectionMethod = EReflectionMethod::None;
				++Overrides;
			}
		}
	}
	UE_LOG(LogATC, Display, TEXT("Render profile: %s (%d post process overrides switched off)"), bVR ? TEXT("VR") : TEXT("desktop"), Overrides);
}

AScenarioStarter::AScenarioStarter()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AScenarioStarter::BeginPlay()
{
	Super::BeginPlay();

	UAirportSubsystem* Airports = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAirportSubsystem>() : nullptr;
	if (Airports && Scenario && !Airports->PendingScenario)
	{
		Airports->ApplyScenario(GetWorld(), Scenario);
	}
}
