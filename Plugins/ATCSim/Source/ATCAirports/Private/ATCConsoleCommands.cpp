// Console commands: a minimal instructor interface and the tools used to test the building blocks.
// Type them in the console (~) during Play.

#include "AirportSubsystem.h"
#include "AircraftCommands.h"
#include "AircraftIdentityComponent.h"
#include "AircraftMovementComponent.h"
#include "AircraftSpawnPoint.h"
#include "AircraftSpawnSubsystem.h"
#include "AircraftTypeData.h"
#include "ATCAircraft.h"
#include "MonitorDisplay.h"
#include "RadioSubsystem.h"
#include "TrafficDirector.h"
#include "ATCGameplayTags.h"
#include "ATCSimClockSubsystem.h"
#include "ATCUnits.h"
#include "EnvironmentTypes.h"
#include "GMICamera.h"
#include "GMICameraSubsystem.h"
#include "GroundServiceSubsystem.h"
#include "PTZControllerComponent.h"
#include "ScenarioData.h"
#include "TaxiNetworkSubsystem.h"
#include "TaxiRoute.h"
#include "TimeOfDaySubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "TrafficSubsystem.h"
#include "WeatherSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace ATCCommands
{
	FGameplayTag Tag(const FString& Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(*Name), false);
	}

	/** Loads an asset by full path or by short name from a default folder: "Mist" -> /Game/Environment/Weather/DA_Weather_Mist. */
	template<typename T>
	T* LoadByName(const FString& Name, const FString& Folder, const FString& Prefix)
	{
		if (Name.StartsWith(TEXT("/")))
		{
			return LoadObject<T>(nullptr, *Name);
		}
		const FString AssetName = Name.StartsWith(Prefix) ? Name : Prefix + Name;
		return LoadObject<T>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, *AssetName, *AssetName));
	}

	void Scenario(const TArray<FString>& Args, UWorld* World)
	{
		UScenarioData* Data = Args.Num() > 0 ? LoadByName<UScenarioData>(Args[0], TEXT("/Game/Scenarios"), TEXT("DA_Scenario_")) : nullptr;
		UAirportSubsystem* Airports = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UAirportSubsystem>() : nullptr;
		if (!Data || !Airports)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.Scenario <Name>: scenario not found (looked in /Game/Scenarios)."));
			return;
		}
		Airports->StartScenario(Data);
	}

	void Weather(const TArray<FString>& Args, UWorld* World)
	{
		UWeatherPresetData* Preset = Args.Num() > 0 ? LoadByName<UWeatherPresetData>(Args[0], TEXT("/Game/Environment/Weather"), TEXT("DA_Weather_")) : nullptr;
		UWeatherSubsystem* WeatherSystem = World ? World->GetSubsystem<UWeatherSubsystem>() : nullptr;
		if (!Preset || !WeatherSystem)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.Weather <Clear|Overcast|Rain|Mist|Snow> [seconds] [rate 0..1]: preset not found."));
			return;
		}
		const float Seconds = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 20.f;
		// The instructor's choice stays: live or dynamic weather would replace it within minutes
		WeatherSystem->SetWeatherMode(EATCWeatherMode::Fixed);
		if (Args.Num() > 2)
		{
			// Same preset, other amount of rain or snow (0..1): light shower to downpour
			FWeatherState State = Preset->State;
			State.PrecipitationRate = FMath::Clamp(FCString::Atof(*Args[2]), 0.f, 1.f);
			WeatherSystem->SetWeatherState(State, Seconds);
			UE_LOG(LogATC, Display, TEXT("Weather -> %s with precipitation rate %.2f (weather mode fixed; ATC.WeatherMode live for the real weather)"),
				*Preset->GetName(), State.PrecipitationRate);
			return;
		}
		WeatherSystem->SetWeather(Preset, Seconds);
		UE_LOG(LogATC, Display, TEXT("Weather mode fixed; ATC.WeatherMode live for the real weather again"));
	}

	/** ATC.WeatherMode [fixed|dynamic|live]: where the weather comes from; alone: what it is now. */
	void WeatherMode(const TArray<FString>& Args, UWorld* World)
	{
		UWeatherSubsystem* WeatherSystem = World ? World->GetSubsystem<UWeatherSubsystem>() : nullptr;
		if (!WeatherSystem)
		{
			return;
		}
		if (Args.Num() > 0)
		{
			const FString Name = Args[0].ToLower();
			if (Name != TEXT("fixed") && Name != TEXT("dynamic") && Name != TEXT("live"))
			{
				UE_LOG(LogATC, Warning, TEXT("ATC.WeatherMode <fixed|dynamic|live>"));
				return;
			}
			WeatherSystem->SetWeatherMode(Name == TEXT("fixed") ? EATCWeatherMode::Fixed : Name == TEXT("dynamic") ? EATCWeatherMode::Dynamic : EATCWeatherMode::Live);
			WeatherSystem->RestartAutomaticWeather();
		}
		UE_LOG(LogATC, Display, TEXT("ATC.WeatherMode: %s"), *WeatherSystem->Describe());
	}

	/**
	 * ATC.Metar: the METAR live weather uses. ATC.Metar refresh: ask for it now. ATC.Metar EHAM 021225Z 22012KT 4000 RA ...:
	 * this METAR as the weather (also offline; the weather mode becomes fixed so it stays).
	 */
	void Metar(const TArray<FString>& Args, UWorld* World)
	{
		UWeatherSubsystem* WeatherSystem = World ? World->GetSubsystem<UWeatherSubsystem>() : nullptr;
		if (!WeatherSystem)
		{
			return;
		}
		if (Args.Num() == 1 && Args[0].Equals(TEXT("refresh"), ESearchCase::IgnoreCase))
		{
			WeatherSystem->SetWeatherMode(EATCWeatherMode::Live);
			WeatherSystem->RefreshLiveWeather();
			UE_LOG(LogATC, Display, TEXT("ATC.Metar: asking %s for the METAR"), *WeatherSystem->GetMetarStation());
			return;
		}
		if (Args.Num() > 1)
		{
			const FString Text = FString::Join(Args, TEXT(" "));
			if (WeatherSystem->ApplyMetar(Text, 20.f))
			{
				WeatherSystem->SetWeatherMode(EATCWeatherMode::Fixed);
			}
			return;
		}
		UE_LOG(LogATC, Display, TEXT("ATC.Metar: %s"), *WeatherSystem->Describe());
	}

	/** ATC.Vehicles [0|1]: ground vehicles (baggage, fuel, catering, pushback tugs) off or on; alone: who is served now. */
	void Vehicles(const TArray<FString>& Args, UWorld* World)
	{
		UGroundServiceSubsystem* Ground = World ? World->GetSubsystem<UGroundServiceSubsystem>() : nullptr;
		if (!Ground)
		{
			return;
		}
		if (Args.Num() > 0)
		{
			Ground->SetEnabled(Args[0] != TEXT("0"));
		}
		TArray<FString> Lines;
		Ground->Describe().ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			UE_LOG(LogATC, Display, TEXT("ATC.Vehicles: %s"), *Line);
		}
	}

	/** ATC.RealTime: the clock back to the real time of this computer (UTC), at normal speed. */
	void RealTime(const TArray<FString>& Args, UWorld* World)
	{
		if (UATCSimClockSubsystem* Clock = World ? World->GetSubsystem<UATCSimClockSubsystem>() : nullptr)
		{
			Clock->FollowRealTime();
			UE_LOG(LogATC, Display, TEXT("ATC.RealTime: %s (%s)"), *Clock->GetZuluTimeString(), *Clock->GetLocalTimeString());
		}
	}

	void Time(const TArray<FString>& Args, UWorld* World)
	{
		UATCSimClockSubsystem* Clock = World ? World->GetSubsystem<UATCSimClockSubsystem>() : nullptr;
		int32 Hour = 0, Minute = 0;
		FString HourText, MinuteText;
		if (!Clock || Args.Num() == 0 || !Args[0].Split(TEXT(":"), &HourText, &MinuteText))
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.Time <HH:MM> [YYYY-MM-DD]: time in UTC."));
			return;
		}
		Hour = FCString::Atoi(*HourText);
		Minute = FCString::Atoi(*MinuteText);

		FDateTime Date = Clock->GetSimTimeUTC();
		if (Args.Num() > 1)
		{
			TArray<FString> Parts;
			Args[1].ParseIntoArray(Parts, TEXT("-"));
			if (Parts.Num() == 3)
			{
				Date = FDateTime(FCString::Atoi(*Parts[0]), FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2]));
			}
		}
		Clock->SetSimTimeUTC(FDateTime(Date.GetYear(), Date.GetMonth(), Date.GetDay(), FMath::Clamp(Hour, 0, 23), FMath::Clamp(Minute, 0, 59)));
		UE_LOG(LogATC, Display, TEXT("Clock set to %s"), *Clock->GetSimTimeUTC().ToString(TEXT("%Y-%m-%d %H:%MZ")));
	}

	void TimeScale(const TArray<FString>& Args, UWorld* World)
	{
		if (UATCSimClockSubsystem* Clock = World ? World->GetSubsystem<UATCSimClockSubsystem>() : nullptr; Clock && Args.Num() > 0)
		{
			Clock->SetTimeScale(FCString::Atof(*Args[0]));
		}
	}

	void Command(const TArray<FString>& Args, UWorld* World)
	{
		UTrafficSubsystem* Traffic = World ? World->GetSubsystem<UTrafficSubsystem>() : nullptr;
		if (!Traffic || Args.Num() < 2)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.Cmd <Callsign> <Pushback|Taxi|Hold|Continue> [Parameter tag]"));
			return;
		}
		const FString Verb = Args[1].ToLower();
		const FGameplayTag Cmd = Verb == TEXT("pushback") ? TAG_ATC_Cmd_Pushback.GetTag()
			: Verb == TEXT("taxi") ? TAG_ATC_Cmd_Taxi.GetTag()
			: Verb == TEXT("hold") ? TAG_ATC_Cmd_HoldPosition.GetTag()
			: TAG_ATC_Cmd_Continue.GetTag();
		Traffic->SendCommand(FName(*Args[0]), Cmd, Args.Num() > 2 ? Tag(Args[2]) : FGameplayTag());
	}

	void Tower(const TArray<FString>& Args, UWorld* World)
	{
		if (UAirportSubsystem* Airports = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UAirportSubsystem>() : nullptr)
		{
			Airports->GoToTower(Args.Num() > 0 ? Tag(Args[0]) : FGameplayTag());
		}
	}

	/**
	 * ATC.Camera [camera id] <preset tag | lens number | next | pan tilt [focal mm]>: a GMI camera to a preset, a lens
	 * or a direction. With a camera ID that camera also becomes the one the joystick moves; without one it is every
	 * camera (a preset only moves the cameras that have it). ATC.Camera alone lists the cameras.
	 */
	void Camera(const TArray<FString>& Args, UWorld* World)
	{
		UGMICameraSubsystem* Cameras = World ? World->GetSubsystem<UGMICameraSubsystem>() : nullptr;
		TArray<AGMICamera*> All;
		for (TActorIterator<AGMICamera> It(World); It; ++It)
		{
			All.Add(*It);
		}
		if (Args.Num() == 0)
		{
			for (const AGMICamera* Camera : All)
			{
				const UPTZControllerComponent* PTZ = Camera->PTZ;
				UE_LOG(LogATC, Display, TEXT("ATC.Camera: %-8s pan %6.1f tilt %5.1f %4.0f mm (%.1f deg) %s, %d presets, live %d, viewed %d%s"),
					*Camera->CameraId.ToString(), PTZ->PanDeg, PTZ->TiltDeg, PTZ->FocalLengthMm, PTZ->GetFieldOfView(), *PTZ->GetCurrentPreset().ToString(),
					PTZ->Presets.Num(), Cameras && Cameras->IsLive(Camera) ? 1 : 0, Camera->IsViewed() ? 1 : 0,
					Cameras && Cameras->GetSelectedCamera() == Camera ? TEXT(", operated") : TEXT(""));
			}
			return;
		}

		TArray<FString> Rest = Args;
		TArray<AGMICamera*> Targets = All.FilterByPredicate([&Args](const AGMICamera* Camera)
		{
			return Camera->CameraId.ToString().Equals(Args[0], ESearchCase::IgnoreCase);
		});
		if (Targets.Num() > 0)
		{
			Rest.RemoveAt(0);
			if (Cameras)
			{
				Cameras->SelectCamera(Targets[0]);
			}
		}
		else
		{
			Targets = All;
		}
		for (AGMICamera* Camera : Targets)
		{
			UPTZControllerComponent* PTZ = Camera->PTZ;
			PTZ->bCyclePresets = false;
			if (Rest.Num() == 0)
			{
				continue;
			}
			if (Rest[0] == TEXT("next"))
			{
				PTZ->NextPreset(1);
			}
			else if (Rest.Num() > 1 && Rest[0].IsNumeric() && Rest[1].IsNumeric())
			{
				PTZ->SetPanTilt(FCString::Atof(*Rest[0]), FCString::Atof(*Rest[1]));
				if (Rest.Num() > 2)
				{
					PTZ->SetFocalLength(FCString::Atof(*Rest[2]));
				}
			}
			else if (Rest[0].IsNumeric())
			{
				PTZ->SelectLens(FCString::Atoi(*Rest[0]));
			}
			else
			{
				PTZ->GoToPreset(Tag(Rest[0]));
			}
		}
	}

	/** ATC.LookAt <callsign | Screen<number>> [field of view]: look at an aircraft (binoculars: a small field of view) or at a tower screen. */
	void LookAt(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		if (!Controller || !Controller->PlayerCameraManager || Args.Num() == 0)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.LookAt <callsign | Screen<number>> [field of view]"));
			return;
		}
		TOptional<FVector> Target;
		if (Args[0].StartsWith(TEXT("Screen")))
		{
			for (TActorIterator<AMonitorDisplay> It(World); It; ++It)
			{
				if (It->MonitorNumber == FCString::Atoi(*Args[0].RightChop(6)))
				{
					Target = It->GetPictureCenter();
				}
			}
		}
		else if (UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>())
		{
			if (const AActor* Aircraft = Traffic->FindAircraft(FName(*Args[0])))
			{
				Target = Aircraft->GetActorLocation() + FVector(0.f, 0.f, 400.f);
			}
		}
		if (!Target.IsSet())
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.LookAt: %s not found"), *Args[0]);
			return;
		}
		const FRotator Look = (Target.GetValue() - Controller->PlayerCameraManager->GetCameraLocation()).Rotation();
		Controller->SetControlRotation(Look);
		// The VR pawn's camera turns with the pawn, not with the control rotation: turn the pawn too
		if (APawn* Pawn = Controller->GetPawn())
		{
			Pawn->SetActorRotation(FRotator(Look.Pitch, Look.Yaw, 0.f));
		}
		if (Args.Num() > 1)
		{
			Controller->PlayerCameraManager->SetFOV(FCString::Atof(*Args[1]));
		}
	}

	/** Leaves the tower: a view beside and above an aircraft, for the instructor and for checking traffic. */
	void Watch(const TArray<FString>& Args, UWorld* World)
	{
		UTrafficSubsystem* Traffic = World ? World->GetSubsystem<UTrafficSubsystem>() : nullptr;
		AActor* Target = Traffic && Args.Num() > 0 ? Traffic->FindAircraft(FName(*Args[0])) : nullptr;
		// "landing" / "takeoff": the first aircraft that is landing or taking off right now
		for (AActor* Actor : Traffic && !Target && Args.Num() > 0 ? Traffic->GetAllAircraft() : TArray<AActor*>())
		{
			const AATCAircraft* Aircraft = Cast<AATCAircraft>(Actor);
			if (Aircraft && ((Args[0] == TEXT("landing") && Aircraft->IsLanding()) || (Args[0] == TEXT("takeoff") && Aircraft->IsTakingOff())))
			{
				Target = Actor;
				break;
			}
		}
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Target || !Pawn)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.Watch <Callsign> [distance m]: aircraft or player not found."));
			return;
		}
		const float Distance = (Args.Num() > 1 ? FCString::Atof(*Args[1]) : 120.f) * ATCUnits::MetersToCm;
		// Front left of the aircraft, a little higher than its tail
		const FVector Aim = Target->GetActorLocation() + FVector(0.f, 0.f, 500.f);
		const FVector From = Aim + Target->GetActorRotation().RotateVector(FVector(0.6f, -0.8f, 0.f)) * Distance + FVector(0.f, 0.f, Distance * 0.3f);
		// The VR pawn's camera turns with the pawn, not with the control rotation: turn both
		const FRotator Look = (Aim - From).Rotation();
		Pawn->SetActorLocationAndRotation(From, FRotator(0.f, Look.Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		Controller->SetControlRotation(Look);
		Controller->SetViewTarget(Pawn);
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->UnlockFOV();
		}
	}

	/**
	 * ATC.Spawn <Spawn.*|Route.*> [type] [callsign] [start m] [end stop]: one aircraft through the spawn manager.
	 * "-" keeps the default (spawn point or traffic mix). Example: ATC.Spawn Route.EHAM.G4.Rwy24 A320 EZY12AB 900
	 */
	void Spawn(const TArray<FString>& Args, UWorld* World)
	{
		UAircraftSpawnSubsystem* Spawner = World ? World->GetSubsystem<UAircraftSpawnSubsystem>() : nullptr;
		const FGameplayTag Where = Args.Num() > 0 ? Tag(Args[0]) : FGameplayTag();
		if (!Spawner || !Where.IsValid())
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.Spawn <Spawn.*|Route.*> [type] [callsign] [start m] [end stop]"));
			return;
		}
		auto Given = [&Args](int32 Index) { return Args.Num() > Index && Args[Index] != TEXT("-"); };

		FAircraftSpawnRequest Request;
		if (const AAircraftSpawnPoint* Point = Spawner->FindSpawnPoint(Where))
		{
			Request = Point->MakeRequest();
		}
		else
		{
			Request.RouteTag = Where;
		}
		if (Given(1))
		{
			Request.Type = LoadByName<UAircraftTypeData>(Args[1], TEXT("/Game/Aircraft/") + Args[1], TEXT("DA_AircraftType_"));
		}
		if (Given(2))
		{
			Request.Callsign = FName(*Args[2]);
		}
		if (Given(3))
		{
			Request.StartDistanceM = FCString::Atof(*Args[3]);
		}
		if (Given(4))
		{
			Request.EndStop = FName(*FString::Join(TArrayView<const FString>(Args).RightChop(4), TEXT(" ")));
		}
		const FName Callsign = Spawner->RequestSpawn(Request);
		UE_LOG(LogATC, Display, TEXT("ATC.Spawn: %s"), Callsign.IsNone() ? TEXT("failed") : *Callsign.ToString());
	}

	/** ATC.Traffic: every aircraft with its state, route and place, and the spawn requests that are waiting. */
	void Traffic(const TArray<FString>& Args, UWorld* World)
	{
		UTrafficSubsystem* TrafficSystem = World ? World->GetSubsystem<UTrafficSubsystem>() : nullptr;
		if (!TrafficSystem)
		{
			return;
		}
		for (AActor* Actor : TrafficSystem->GetAllAircraft())
		{
			const AATCAircraft* Aircraft = Cast<AATCAircraft>(Actor);
			if (!Aircraft)
			{
				continue;
			}
			const ATaxiRoute* Route = Aircraft->TaxiController->Route;
			UE_LOG(LogATC, Display, TEXT("ATC.Traffic: %-8s %-5s %-32s %s %.0f m"), *Aircraft->Identity->Callsign.ToString(),
				Aircraft->AircraftType ? *Aircraft->AircraftType->ICAOType.ToString() : TEXT("?"),
				*IAircraftInfo::Execute_GetStatusText(Aircraft).ToString(), Route ? *Route->RouteTag.ToString() : TEXT("(no route)"),
				Aircraft->Movement->Distance / 100.f);
		}
		if (const UAircraftSpawnSubsystem* Spawner = World->GetSubsystem<UAircraftSpawnSubsystem>())
		{
			for (const FName& Waiting : Spawner->GetQueuedCallsigns())
			{
				UE_LOG(LogATC, Display, TEXT("ATC.Traffic: %-8s waiting to spawn"), *Waiting.ToString());
			}
		}
	}

	/** ATC.Despawn <callsign|all> */
	void Despawn(const TArray<FString>& Args, UWorld* World)
	{
		UAircraftSpawnSubsystem* Spawner = World ? World->GetSubsystem<UAircraftSpawnSubsystem>() : nullptr;
		UTrafficSubsystem* TrafficSystem = World ? World->GetSubsystem<UTrafficSubsystem>() : nullptr;
		if (!Spawner || !TrafficSystem || Args.Num() == 0)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.Despawn <callsign|all>"));
			return;
		}
		TArray<FName> Callsigns;
		if (Args[0] == TEXT("all"))
		{
			Callsigns = Spawner->GetQueuedCallsigns();
			for (AActor* Actor : TrafficSystem->GetAllAircraft())
			{
				Callsigns.Add(IAircraftInfo::Execute_GetCallsign(Actor));
			}
		}
		else
		{
			Callsigns.Add(FName(*Args[0]));
		}
		for (const FName& Callsign : Callsigns)
		{
			Spawner->Despawn(Callsign);
		}
	}

	/** ATC.TrafficDirector <start|stop|status|auto 0/1|rate N>: the map's traffic program. */
	void TrafficDirector(const TArray<FString>& Args, UWorld* World)
	{
		const FString Action = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("status"));
		int32 Count = 0;
		for (TActorIterator<ATrafficDirector> It(World); It; ++It, ++Count)
		{
			if (Action == TEXT("start"))
			{
				It->StartTraffic();
			}
			else if (Action == TEXT("stop"))
			{
				It->StopTraffic();
			}
			else if (Action == TEXT("auto") && Args.Num() > 1)
			{
				It->bAutoClearances = Args[1] != TEXT("0");
				UE_LOG(LogATC, Display, TEXT("ATC.TrafficDirector: clearances %s"), It->bAutoClearances ? TEXT("automatic") : TEXT("by the trainee"));
			}
			else if (Action == TEXT("rate") && Args.Num() > 1)
			{
				It->ArrivalsPerHour = FMath::Max(FCString::Atof(*Args[1]), 0.f);
				UE_LOG(LogATC, Display, TEXT("ATC.TrafficDirector: %.0f arrivals an hour (from the next one)"), It->ArrivalsPerHour);
			}
			else
			{
				TArray<FString> Lines;
				It->Describe().ParseIntoArrayLines(Lines);
				for (const FString& Line : Lines)
				{
					UE_LOG(LogATC, Display, TEXT("ATC.TrafficDirector: %s"), *Line);
				}
			}
		}
		if (Count == 0)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.TrafficDirector: this map has no traffic director."));
		}
	}

	/** ATC.Turn <yaw> [pitch]: turn the view by degrees (look around in the cab without a mouse; tests, demos). */
	void Turn(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		if (!Controller || Args.Num() == 0)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.Turn <yaw degrees> [pitch degrees]"));
			return;
		}
		FRotator Rotation = Controller->GetControlRotation();
		Rotation.Yaw += FCString::Atof(*Args[0]);
		Rotation.Pitch = Args.Num() > 1 ? FCString::Atof(*Args[1]) : Rotation.Pitch;
		Controller->SetControlRotation(Rotation);
		// The VR pawn's camera turns with the pawn, not with the control rotation: turn the pawn too (and tilt it only when asked)
		if (APawn* Pawn = Controller->GetPawn())
		{
			Pawn->SetActorRotation(FRotator(Args.Num() > 1 ? Rotation.Pitch : 0.f, Rotation.Yaw, 0.f));
		}
	}

	/** ATC.Key <key> [seconds]: holds a key as if it were pressed on the keyboard, through the normal input mapping (demos, tests). */
	void Key(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		const FKey Pressed(Args.Num() > 0 ? FName(*Args[0]) : NAME_None);
		if (!Controller || !Pressed.IsValid())
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.Key <key, for example Right, PageUp, Tab, Two, L> [seconds]"));
			return;
		}
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Pressed, IE_Pressed, 1.f));
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Controller, [Controller, Pressed]()
		{
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Pressed, IE_Released, 0.f));
		}), FMath::Max(Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.1f, 0.01f), false);
	}

	/** ATC.Radio [0|1]: radio chatter off or on; without an argument the last transmissions in the log. */
	void Radio(const TArray<FString>& Args, UWorld* World)
	{
		URadioSubsystem* RadioSystem = World ? World->GetSubsystem<URadioSubsystem>() : nullptr;
		if (!RadioSystem)
		{
			return;
		}
		if (Args.Num() > 0)
		{
			RadioSystem->SetEnabled(Args[0] != TEXT("0"));
			UE_LOG(LogATC, Display, TEXT("ATC.Radio: %s"), RadioSystem->IsEnabled() ? TEXT("on") : TEXT("off"));
			return;
		}
		for (const FString& Line : RadioSystem->GetRecentLines())
		{
			UE_LOG(LogATC, Display, TEXT("ATC.Radio: %s"), *Line);
		}
	}

	/** ATC.Monitor <screen number> <camera id | next>: which camera a tower screen shows. */
	void Monitor(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.Monitor <screen number> <camera id, for example GMI-G7 | next>"));
			return;
		}
		TArray<AGMICamera*> Cameras;
		for (TActorIterator<AGMICamera> It(World); It; ++It)
		{
			Cameras.Add(*It);
		}
		Cameras.Sort([](const AGMICamera& A, const AGMICamera& B) { return A.CameraId.LexicalLess(B.CameraId); });
		for (TActorIterator<AMonitorDisplay> It(World); It; ++It)
		{
			if (It->MonitorNumber != FCString::Atoi(*Args[0]))
			{
				continue;
			}
			AGMICamera* Chosen = nullptr;
			if (Args[1] == TEXT("next") && Cameras.Num() > 0)
			{
				Chosen = Cameras[(Cameras.IndexOfByKey(It->SourceCamera.Get()) + 1) % Cameras.Num()];
			}
			for (AGMICamera* Camera : Cameras)
			{
				Chosen = Camera->CameraId.ToString().Equals(Args[1], ESearchCase::IgnoreCase) ? Camera : Chosen;
			}
			if (Chosen)
			{
				It->SetSourceCamera(Chosen);
				UE_LOG(LogATC, Display, TEXT("ATC.Monitor: screen %d shows %s"), It->MonitorNumber, *Chosen->CameraId.ToString());
			}
		}
	}

	/** ATC.Lights [0|1]: taxiway lights of every route off or on; without an argument the state of each route in the log. */
	void Lights(const TArray<FString>& Args, UWorld* World)
	{
		for (TActorIterator<ATaxiRoute> It(World); It; ++It)
		{
			UInstancedStaticMeshComponent* Component = It->Lights;
			if (Args.Num() > 0)
			{
				Component->SetVisibility(Args[0] != TEXT("0"));
				continue;
			}
			UE_LOG(LogATC, Display, TEXT("ATC.Lights: %s %d lights, visible %d, mesh %s, material %s"), *It->RouteTag.ToString(), Component->GetInstanceCount(),
				Component->IsVisible(), *GetNameSafe(Component->GetStaticMesh()), *GetNameSafe(Component->GetMaterial(0)));
		}
	}

	/** Runs a console command after a delay; for scripted demos and tests: ATC.After 30 ATC.Weather Rain */
	void After(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.After <seconds> <command>"));
			return;
		}
		FString Command;
		for (int32 Index = 1; Index < Args.Num(); ++Index)
		{
			Command += (Index > 1 ? TEXT(" ") : TEXT("")) + Args[Index];
		}
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Command]()
		{
			if (UWorld* Target = WeakWorld.Get())
			{
				GEngine->Exec(Target, *Command);
			}
		}), FMath::Max(FCString::Atof(*Args[0]), 0.01f), false);
	}

	void Status(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		if (const UATCSimClockSubsystem* Clock = World->GetSubsystem<UATCSimClockSubsystem>())
		{
			UE_LOG(LogATC, Display, TEXT("ATC.Status: clock %s (%s, %s, x%.0f)"), *Clock->GetSimTimeUTC().ToString(TEXT("%Y-%m-%d %H:%M:%SZ")),
				*Clock->GetLocalTimeString(), Clock->IsRealTime() ? TEXT("real time") : TEXT("set by hand or scenario"), Clock->GetTimeScale());
		}
		if (const APlayerController* Controller = World->GetFirstPlayerController())
		{
			const APawn* Pawn = Controller->GetPawn();
			const AActor* ViewTarget = Controller->PlayerCameraManager ? Controller->PlayerCameraManager->GetViewTarget() : nullptr;
			UE_LOG(LogATC, Display, TEXT("ATC.Status: player %s at %s, view target %s, camera at %s"), Pawn ? *Pawn->GetName() : TEXT("none"),
				Pawn ? *Pawn->GetActorLocation().ToCompactString() : TEXT("-"), ViewTarget ? *ViewTarget->GetName() : TEXT("none"),
				Controller->PlayerCameraManager ? *Controller->PlayerCameraManager->GetCameraLocation().ToCompactString() : TEXT("-"));
		}
		if (const UTimeOfDaySubsystem* TimeOfDay = World->GetSubsystem<UTimeOfDaySubsystem>())
		{
			UE_LOG(LogATC, Display, TEXT("ATC.Status: sun elevation %.1f azimuth %.1f phase %s, sunrise %s sunset %s"), TimeOfDay->GetSunElevation(),
				TimeOfDay->GetSunAzimuth(), *UEnum::GetValueAsString(TimeOfDay->GetDayPhase()),
				*TimeOfDay->GetSunriseUTC().ToString(TEXT("%H:%MZ")), *TimeOfDay->GetSunsetUTC().ToString(TEXT("%H:%MZ")));
		}
		if (const UWeatherSubsystem* WeatherSystem = World->GetSubsystem<UWeatherSubsystem>())
		{
			const FWeatherState State = WeatherSystem->GetCurrentWeather();
			UE_LOG(LogATC, Display, TEXT("ATC.Status: weather clouds %.2f base %.0f ft visibility %.0f m %s %.2f, wind %03.0f/%.0f kt"), State.CloudCoverage,
				State.CloudBaseFt, State.VisibilityM, *UEnum::GetValueAsString(State.Precipitation), State.PrecipitationRate, State.WindDirectionDeg, State.WindSpeedKt);
			UE_LOG(LogATC, Display, TEXT("ATC.Status: weather %s"), *WeatherSystem->Describe());
		}
		if (const UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>())
		{
			for (AActor* Aircraft : Traffic->GetAllAircraft())
			{
				UE_LOG(LogATC, Display, TEXT("ATC.Status: %s %s at %s"), *IAircraftInfo::Execute_GetCallsign(Aircraft).ToString(),
					*IAircraftInfo::Execute_GetStatusText(Aircraft).ToString(), *Aircraft->GetActorLocation().ToCompactString());
			}
		}
	}

	/** ATC.Claims [0|1]: the reserved taxiway stretches drawn over the taxiways, one colour per aircraft, red where one waits. */
	void Claims(const TArray<FString>& Args, UWorld* World)
	{
		UTaxiNetworkSubsystem* Network = World ? World->GetSubsystem<UTaxiNetworkSubsystem>() : nullptr;
		if (!Network)
		{
			return;
		}
		Network->bDrawClaims = Args.Num() == 0 ? !Network->bDrawClaims : Args[0] != TEXT("0");
		UE_LOG(LogATC, Display, TEXT("ATC.Claims: %s"), Network->bDrawClaims ? TEXT("shown") : TEXT("hidden"));
	}

	/**
	 * ATC.Network: the taxi network, every reservation, who waits for whom, and what the collision monitor saw.
	 * ATC.Network plot [callsign] [half size m]: also a picture around that aircraft (or the tower) in Saved/ATC.
	 */
	void NetworkStatus(const TArray<FString>& Args, UWorld* World)
	{
		UTaxiNetworkSubsystem* Network = World ? World->GetSubsystem<UTaxiNetworkSubsystem>() : nullptr;
		if (!Network)
		{
			return;
		}
		if (Args.Num() > 0 && Args[0] == TEXT("plot"))
		{
			const UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>();
			const AActor* Around = Traffic && Args.Num() > 1 ? Traffic->FindAircraft(FName(*Args[1])) : nullptr;
			const float HalfSize = (Args.Num() > 2 ? FCString::Atof(*Args[2]) : 1500.f) * ATCUnits::MetersToCm;
			// Not an aircraft: the whole airport, under that name (ATC.Network plot noon)
			Network->Plot(Args.Num() > 1 ? FString::Printf(TEXT("network_%s"), *Args[1]) : FString(TEXT("network")),
				Around ? Around->GetActorLocation() : FVector::ZeroVector, Around ? HalfSize : FMath::Max(HalfSize, 250000.f));
		}
		TArray<FString> Lines;
		Network->Describe().ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			UE_LOG(LogATC, Display, TEXT("ATC.Network: %s"), *Line);
		}
	}

	void Shot(const TArray<FString>& Args, UWorld* World)
	{
		const FString Name = Args.Num() > 0 ? Args[0] : TEXT("ATC");
		const FString File = FPaths::ProjectSavedDir() / TEXT("Screenshots/ATC") / Name + TEXT(".png");
		// "ui": with the widgets on the screen (instructor panel)
		FScreenshotRequest::RequestScreenshot(File, Args.Num() > 1 && Args[1] == TEXT("ui"), false);
		UE_LOG(LogATC, Display, TEXT("ATC.Shot: %s"), *File);
	}

	FAutoConsoleCommandWithWorldAndArgs ScenarioCommand(TEXT("ATC.Scenario"), TEXT("ATC.Scenario <Name>: start a scenario from /Game/Scenarios"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Scenario));
	FAutoConsoleCommandWithWorldAndArgs WeatherCommand(TEXT("ATC.Weather"), TEXT("ATC.Weather <Clear|Overcast|Rain|Mist|Snow> [seconds] [precipitation rate 0..1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Weather));
	FAutoConsoleCommandWithWorldAndArgs WeatherModeCommand(TEXT("ATC.WeatherMode"),
		TEXT("ATC.WeatherMode [fixed|dynamic|live]: presets only, weather that changes by itself, or the real weather from the METAR"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WeatherMode));
	FAutoConsoleCommandWithWorldAndArgs MetarCommand(TEXT("ATC.Metar"),
		TEXT("ATC.Metar [refresh | <METAR text>]: the live weather's METAR, ask for it now, or use this METAR as the weather"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Metar));
	FAutoConsoleCommandWithWorldAndArgs VehiclesCommand(TEXT("ATC.Vehicles"),
		TEXT("ATC.Vehicles [0|1]: ground vehicles at parked aircraft and pushback tugs off or on; alone: who is being served"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Vehicles));
	FAutoConsoleCommandWithWorldAndArgs RealTimeCommand(TEXT("ATC.RealTime"), TEXT("ATC.RealTime: the clock back to the real time (UTC) at normal speed"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RealTime));
	FAutoConsoleCommandWithWorldAndArgs TimeCommand(TEXT("ATC.Time"), TEXT("ATC.Time <HH:MM> [YYYY-MM-DD]: simulation time in UTC (leaves real time; ATC.RealTime goes back)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Time));
	FAutoConsoleCommandWithWorldAndArgs TimeScaleCommand(TEXT("ATC.TimeScale"), TEXT("ATC.TimeScale <factor>: speed of the clock (sun)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TimeScale));
	FAutoConsoleCommandWithWorldAndArgs CmdCommand(TEXT("ATC.Cmd"), TEXT("ATC.Cmd <Callsign> <Pushback|Taxi|Hold|Continue> [Parameter tag]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));
	FAutoConsoleCommandWithWorldAndArgs TowerCommand(TEXT("ATC.Tower"), TEXT("ATC.Tower <Tower.* tag>: go to a tower position"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Tower));
	FAutoConsoleCommandWithWorldAndArgs KeyCommand(TEXT("ATC.Key"), TEXT("ATC.Key <key> [seconds]: hold a key as if pressed on the keyboard (Right, PageUp, Tab, Two, L ...)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Key));
	FAutoConsoleCommandWithWorldAndArgs CameraCommand(TEXT("ATC.Camera"), TEXT("ATC.Camera [camera id] <Cam.* preset tag | lens index | next | pan tilt [focal mm]>; alone: list the cameras"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Camera));
	FAutoConsoleCommandWithWorldAndArgs LookAtCommand(TEXT("ATC.LookAt"), TEXT("ATC.LookAt <Callsign | Screen<number>> [FOV]: turn the view towards an aircraft (binoculars) or a tower screen"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LookAt));
	FAutoConsoleCommandWithWorldAndArgs WatchCommand(TEXT("ATC.Watch"), TEXT("ATC.Watch <Callsign|landing|takeoff> [distance m]: view an aircraft from close by (leave with ATC.Tower)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Watch));
	FAutoConsoleCommandWithWorldAndArgs TurnCommand(TEXT("ATC.Turn"), TEXT("ATC.Turn <yaw> [pitch]: turn the view by degrees"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Turn));
	FAutoConsoleCommandWithWorldAndArgs RadioCommand(TEXT("ATC.Radio"), TEXT("ATC.Radio [0|1]: radio chatter off or on; without argument the last transmissions"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Radio));
	FAutoConsoleCommandWithWorldAndArgs MonitorCommand(TEXT("ATC.Monitor"), TEXT("ATC.Monitor <screen number> <camera id | next>: camera on a tower screen"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Monitor));
	FAutoConsoleCommandWithWorldAndArgs TrafficDirectorCommand(TEXT("ATC.TrafficDirector"),
		TEXT("ATC.TrafficDirector <start|stop|status|auto 0/1|rate N>: the map's traffic program (arrivals, turnarounds, departures)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TrafficDirector));
	FAutoConsoleCommandWithWorldAndArgs SpawnCommand(TEXT("ATC.Spawn"), TEXT("ATC.Spawn <Spawn.*|Route.*> [type] [callsign] [start m] [end stop]: spawn an aircraft on a route"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Spawn));
	FAutoConsoleCommandWithWorldAndArgs TrafficCommand(TEXT("ATC.Traffic"), TEXT("List every aircraft with state, route and place"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Traffic));
	FAutoConsoleCommandWithWorldAndArgs DespawnCommand(TEXT("ATC.Despawn"), TEXT("ATC.Despawn <callsign|all>: remove aircraft"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Despawn));
	FAutoConsoleCommandWithWorldAndArgs LightsCommand(TEXT("ATC.Lights"), TEXT("ATC.Lights [0|1]: taxiway lights off or on; without argument their state in the log"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Lights));
	FAutoConsoleCommandWithWorldAndArgs AfterCommand(TEXT("ATC.After"), TEXT("ATC.After <seconds> <command>: run a console command later"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&After));
	FAutoConsoleCommandWithWorldAndArgs StatusCommand(TEXT("ATC.Status"), TEXT("Log clock, sun, weather and all aircraft"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Status));
	FAutoConsoleCommandWithWorldAndArgs ShotCommand(TEXT("ATC.Shot"), TEXT("ATC.Shot <name> [ui]: screenshot to Saved/Screenshots/ATC (ui: with the widgets)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Shot));
	FAutoConsoleCommandWithWorldAndArgs ClaimsCommand(TEXT("ATC.Claims"), TEXT("ATC.Claims [0|1]: show the reserved taxiway stretches (one colour per aircraft, red = waiting)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Claims));
	FAutoConsoleCommandWithWorldAndArgs NetworkCommand(TEXT("ATC.Network"),
		TEXT("ATC.Network [plot [callsign] [half size m]]: log the taxi network, every reservation and queue, and the collision monitor; plot = picture in Saved/ATC"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NetworkStatus));
}
