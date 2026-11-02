#include "GMICameraSubsystem.h"
#include "ATCUnits.h"
#include "DynamicRHI.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GMICamera.h"
#include "GMICameraPanel.h"
#include "GMICameraSettings.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Paths.h"
#include "MonitorDisplay.h"
#include "Slate/WidgetRenderer.h"
#include "PTZControllerComponent.h"
#include "RenderingThread.h"
#include "RenderTimer.h"

namespace
{
	TAutoConsoleVariable<float> CVarCameraRateScale(TEXT("atc.CameraRateScale"), 1.f,
		TEXT("Multiplies the frame rate of the live GMI cameras (the VR render profile sets 0.75: 15 Hz)"));

	TAutoConsoleVariable<int32> CVarCameraCaptures(TEXT("atc.CameraCaptures"), 1,
		TEXT("0 = the GMI cameras render nothing (to measure what they cost), 1 = normal"));
}

bool UGMICameraSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UGMICameraSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGMICameraSubsystem, STATGROUP_Tickables);
}

void UGMICameraSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	SetupInput(InWorld.GetFirstPlayerController());
}

void UGMICameraSubsystem::Deinitialize()
{
	if (APlayerController* Controller = InputController.Get())
	{
		if (InputComponent)
		{
			Controller->PopInputComponent(InputComponent);
		}
		const ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer();
		if (UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr)
		{
			Input->RemoveMappingContext(InputContext);
		}
	}
	if (Panel)
	{
		Panel->RemoveFromParent();
		Panel = nullptr;
	}
	Super::Deinitialize();
}

void UGMICameraSubsystem::RegisterCamera(AGMICamera* Camera)
{
	if (Camera && !Find(Camera))
	{
		FTrackedCamera& Entry = Tracked.AddDefaulted_GetRef();
		Entry.Camera = Camera;
	}
}

void UGMICameraSubsystem::UnregisterCamera(AGMICamera* Camera)
{
	Tracked.RemoveAll([Camera](const FTrackedCamera& Entry) { return Entry.Camera == Camera; });
}

void UGMICameraSubsystem::RegisterMonitor(AMonitorDisplay* Monitor)
{
	Monitors.AddUnique(Monitor);
}

void UGMICameraSubsystem::UnregisterMonitor(AMonitorDisplay* Monitor)
{
	Monitors.Remove(Monitor);
}

UGMICameraSubsystem::FTrackedCamera* UGMICameraSubsystem::Find(const AGMICamera* Camera)
{
	return Tracked.FindByPredicate([Camera](const FTrackedCamera& Entry) { return Entry.Camera.Get() == Camera; });
}

const UGMICameraSubsystem::FTrackedCamera* UGMICameraSubsystem::Find(const AGMICamera* Camera) const
{
	return Tracked.FindByPredicate([Camera](const FTrackedCamera& Entry) { return Entry.Camera.Get() == Camera; });
}

TArray<AGMICamera*> UGMICameraSubsystem::GetCameras() const
{
	TArray<AGMICamera*> Result;
	for (const FTrackedCamera& Entry : Tracked)
	{
		if (AGMICamera* Camera = Entry.Camera.Get())
		{
			Result.Add(Camera);
		}
	}
	Result.Sort([](const AGMICamera& A, const AGMICamera& B) { return A.CameraId.LexicalLess(B.CameraId); });
	return Result;
}

bool UGMICameraSubsystem::IsLive(const AGMICamera* Camera) const
{
	const FTrackedCamera* Entry = Find(Camera);
	return Entry && Entry->State.bLive;
}

void UGMICameraSubsystem::PlanCaptures(TArray<FGMICaptureState>& States, float DeltaTime, int32 MaxLive, float BackgroundRateHz, int32 MaxPerFrame,
	TArray<int32>& OutCapture)
{
	TArray<int32> Viewed;
	for (int32 Index = 0; Index < States.Num(); ++Index)
	{
		States[Index].bLive = false;
		if (States[Index].bViewed)
		{
			Viewed.Add(Index);
		}
		else
		{
			// Nobody sees it: no pictures, and the first one comes at once when a screen comes into view again
			States[Index].Credit = 1.f;
		}
	}
	Viewed.StableSort([&States](int32 A, int32 B) { return States[A].Priority > States[B].Priority; });
	for (int32 Rank = 0; Rank < Viewed.Num(); ++Rank)
	{
		FGMICaptureState& State = States[Viewed[Rank]];
		State.bLive = Rank < MaxLive;
		const float Rate = State.bLive ? State.LiveRateHz : FMath::Min(BackgroundRateHz, State.LiveRateHz);
		// At most two pictures owed: a camera that had to wait catches up with one picture, not with a burst
		State.Credit = FMath::Min(State.Credit + Rate * DeltaTime, 2.f);
	}

	TArray<int32> Due;
	for (const int32 Index : Viewed)
	{
		if (States[Index].Credit >= 1.f)
		{
			Due.Add(Index);
		}
	}
	Due.StableSort([&States](int32 A, int32 B) { return States[A].Credit > States[B].Credit; });
	OutCapture.Reset();
	for (const int32 Index : Due)
	{
		if (OutCapture.Num() >= MaxPerFrame)
		{
			break;
		}
		States[Index].Credit -= 1.f;
		OutCapture.Add(Index);
	}
}

void UGMICameraSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Real time: what a capture costs is per real second, also in slow or fast motion
	const double Now = FPlatformTime::Seconds();
	const float RealDelta = LastTickTime > 0.0 ? float(FMath::Min(Now - LastTickTime, 0.5)) : DeltaTime;
	LastTickTime = Now;

	Tracked.RemoveAll([](const FTrackedCamera& Entry) { return !Entry.Camera.IsValid(); });
	Monitors.RemoveAll([](const TWeakObjectPtr<AMonitorDisplay>& Monitor) { return !Monitor.IsValid(); });
	if (!InputController.IsValid())
	{
		SetupInput(GetWorld()->GetFirstPlayerController());
	}
	if (!SelectedCamera.IsValid() && Tracked.Num() > 0)
	{
		// The camera on the first screen, otherwise the first camera
		AGMICamera* First = nullptr;
		int32 FirstNumber = MAX_int32;
		for (const TWeakObjectPtr<AMonitorDisplay>& Monitor : Monitors)
		{
			if (Monitor->SourceCamera && Monitor->MonitorNumber < FirstNumber)
			{
				First = Monitor->SourceCamera;
				FirstNumber = Monitor->MonitorNumber;
			}
		}
		SelectCamera(First ? First : GetCameras()[0]);
	}

	PriorityTimer -= RealDelta;
	if (PriorityTimer <= 0.f)
	{
		PriorityTimer = 0.25f;
		RefreshPriorities();
	}

	const UGMICameraSettings* Settings = GetDefault<UGMICameraSettings>();
	const float RateScale = FMath::Max(CVarCameraRateScale.GetValueOnGameThread(), 0.f);
	TArray<FGMICaptureState> States;
	for (FTrackedCamera& Entry : Tracked)
	{
		Entry.State.LiveRateHz = Entry.Camera->CaptureRateHz * RateScale;
		States.Add(Entry.State);
	}
	TArray<int32> Capture;
	PlanCaptures(States, RealDelta, Settings->MaxLiveCameras, Settings->BackgroundRateHz, Settings->MaxCapturesPerFrame, Capture);
	for (int32 Index = 0; Index < Tracked.Num(); ++Index)
	{
		Tracked[Index].State = States[Index];
	}
	if (CVarCameraCaptures.GetValueOnGameThread() == 0)
	{
		Capture.Reset();
	}
	for (const int32 Index : Capture)
	{
		Tracked[Index].Camera->CaptureNow();
	}
	TickStats(Capture);
}

void UGMICameraSubsystem::RefreshPriorities()
{
	FVector Eye, Forward;
	const bool bView = GetView(Eye, Forward);
	const bool bPanel = IsPanelShown();
	for (FTrackedCamera& Entry : Tracked)
	{
		Entry.State.bViewed = bPanel || Entry.Camera->IsViewed();
		Entry.State.Priority = 0.f;
	}
	// A screen counts more the bigger it is in view and the nearer the middle of the view
	for (const TWeakObjectPtr<AMonitorDisplay>& Weak : Monitors)
	{
		const AMonitorDisplay* Monitor = Weak.Get();
		FTrackedCamera* Entry = Monitor && bView ? Find(Monitor->SourceCamera) : nullptr;
		if (!Entry)
		{
			continue;
		}
		const FVector ToScreen = Monitor->GetPictureCenter() - Eye;
		const float Distance = FMath::Max(ToScreen.Size(), 30.f);
		const FVector2D Size = Monitor->GetPictureSize();
		const float Facing = FMath::Max(Forward | (ToScreen / Distance), 0.f);
		Entry->State.Priority = FMath::Max(Entry->State.Priority, Size.X * Size.Y / (Distance * Distance) * Facing * Facing);
	}
	if (FTrackedCamera* Selected = Find(SelectedCamera.Get()))
	{
		Selected->State.Priority += 1000.f;
	}
}

bool UGMICameraSubsystem::GetView(FVector& OutLocation, FVector& OutForward) const
{
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (!Controller || !Controller->PlayerCameraManager)
	{
		return false;
	}
	OutLocation = Controller->PlayerCameraManager->GetCameraLocation();
	OutForward = Controller->PlayerCameraManager->GetCameraRotation().Vector();
	return true;
}

AGMICamera* UGMICameraSubsystem::CameraInView() const
{
	FVector Eye, Forward;
	if (!GetView(Eye, Forward))
	{
		return nullptr;
	}
	// The view ray hits the picture (or just beside it)
	AGMICamera* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<AMonitorDisplay>& Weak : Monitors)
	{
		const AMonitorDisplay* Monitor = Weak.Get();
		const FVector Normal = Monitor ? Monitor->GetActorForwardVector() : FVector::ZeroVector;
		const float Along = Forward | Normal;
		if (!Monitor || !Monitor->SourceCamera || Along > -0.1f)
		{
			continue;
		}
		const FVector Center = Monitor->GetPictureCenter();
		const float Distance = ((Center - Eye) | Normal) / Along;
		const FVector Hit = Eye + Forward * Distance - Center;
		const FVector2D Size = Monitor->GetPictureSize() * 0.5f * 1.25f;
		if (Distance > 0.f && Distance < BestDistance && FMath::Abs(Hit | Monitor->GetActorRightVector()) <= Size.X && FMath::Abs(Hit | Monitor->GetActorUpVector()) <= Size.Y)
		{
			Best = Monitor->SourceCamera;
			BestDistance = Distance;
		}
	}
	return Best;
}

AGMICamera* UGMICameraSubsystem::OperatedCamera()
{
	// Looking at a camera's screen picks that camera, like pointing at it
	AGMICamera* Looked = CameraInView();
	if (Looked && Looked != SelectedCamera.Get())
	{
		SelectCamera(Looked);
	}
	return SelectedCamera.Get();
}

void UGMICameraSubsystem::SelectCamera(AGMICamera* Camera)
{
	if (Camera == SelectedCamera.Get())
	{
		return;
	}
	SelectedCamera = Camera;
	if (Camera)
	{
		UE_LOG(LogATC, Display, TEXT("GMI: operating %s"), *Camera->CameraId.ToString());
	}
	PriorityTimer = 0.f;
	RefreshLabels();
}

void UGMICameraSubsystem::SelectNextCamera(int32 Direction)
{
	// Cameras that are on a screen; without screens, all of them
	TArray<AGMICamera*> Candidates;
	for (const TWeakObjectPtr<AMonitorDisplay>& Monitor : Monitors)
	{
		if (Monitor.IsValid() && Monitor->SourceCamera)
		{
			Candidates.AddUnique(Monitor->SourceCamera);
		}
	}
	if (Candidates.Num() == 0)
	{
		Candidates = GetCameras();
	}
	if (Candidates.Num() == 0)
	{
		return;
	}
	Candidates.Sort([](const AGMICamera& A, const AGMICamera& B) { return A.CameraId.LexicalLess(B.CameraId); });
	const int32 Current = Candidates.IndexOfByKey(SelectedCamera.Get());
	const int32 Step = Direction >= 0 ? 1 : -1;
	SelectCamera(Candidates[(FMath::Max(Current, Step > 0 ? -1 : 0) + Step + Candidates.Num()) % Candidates.Num()]);
}

void UGMICameraSubsystem::RefreshLabels()
{
	for (const TWeakObjectPtr<AMonitorDisplay>& Monitor : Monitors)
	{
		if (Monitor.IsValid())
		{
			Monitor->RefreshLabel();
		}
	}
	if (IsPanelShown())
	{
		Panel->Refresh(SelectedCamera.Get());
	}
}

// ---------------------------------------------------------------- Operator input

void UGMICameraSubsystem::AddDefaultMappings(UInputMappingContext* Context, const FGMIInputActions& InActions)
{
	auto Map = [Context](const UInputAction* Action, const FKey& Key, float Scale = 1.f, bool bStick = false)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		if (bStick)
		{
			UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(Context);
			DeadZone->Type = EDeadZoneType::Axial;
			DeadZone->LowerThreshold = 0.15f;
			Mapping.Modifiers.Add(DeadZone);
		}
		if (Scale < 0.f)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
		if (FMath::Abs(Scale) != 1.f)
		{
			UInputModifierScalar* Scalar = NewObject<UInputModifierScalar>(Context);
			Scalar->Scalar = FVector(FMath::Abs(Scale));
			Mapping.Modifiers.Add(Scalar);
		}
	};
	// Motion controllers of the VR headsets the project supports: the left hand (the right one teleports and turns)
	const TCHAR* Hands[] = { TEXT("OculusTouch_Left"), TEXT("ValveIndex_Left"), TEXT("MixedReality_Left") };

	Map(InActions.Pan, EKeys::Right);
	Map(InActions.Pan, EKeys::Left, -1.f);
	Map(InActions.Pan, EKeys::Gamepad_RightX, 1.f, true);
	Map(InActions.Tilt, EKeys::Up);
	Map(InActions.Tilt, EKeys::Down, -1.f);
	Map(InActions.Tilt, EKeys::Gamepad_RightY, 1.f, true);
	for (const TCHAR* Hand : Hands)
	{
		Map(InActions.Pan, FKey(*FString::Printf(TEXT("%s_Thumbstick_X"), Hand)), 1.f, true);
		Map(InActions.Tilt, FKey(*FString::Printf(TEXT("%s_Thumbstick_Y"), Hand)), 1.f, true);
		Map(InActions.NextCamera, FKey(*FString::Printf(TEXT("%s_Thumbstick_Click"), Hand)));
	}
	Map(InActions.Pan, FKey(TEXT("Vive_Left_Trackpad_X")), 1.f, true);
	Map(InActions.Tilt, FKey(TEXT("Vive_Left_Trackpad_Y")), 1.f, true);

	Map(InActions.Zoom, EKeys::PageUp);
	Map(InActions.Zoom, EKeys::PageDown, -1.f);
	Map(InActions.Zoom, EKeys::Add);
	Map(InActions.Zoom, EKeys::Subtract, -1.f);
	Map(InActions.Zoom, EKeys::Gamepad_RightTriggerAxis);
	Map(InActions.Zoom, EKeys::Gamepad_LeftTriggerAxis, -1.f);
	Map(InActions.Zoom, FKey(TEXT("OculusTouch_Left_Y_Click")));
	Map(InActions.Zoom, FKey(TEXT("OculusTouch_Left_X_Click")), -1.f);
	Map(InActions.Zoom, FKey(TEXT("ValveIndex_Left_B_Click")));
	Map(InActions.Zoom, FKey(TEXT("ValveIndex_Left_A_Click")), -1.f);

	Map(InActions.NextCamera, EKeys::Tab);
	Map(InActions.NextCamera, EKeys::Gamepad_RightShoulder);
	Map(InActions.NextCamera, EKeys::Gamepad_LeftShoulder, -1.f);

	Map(InActions.NextPreset, EKeys::Period);
	Map(InActions.NextPreset, EKeys::Comma, -1.f);
	Map(InActions.NextPreset, EKeys::Gamepad_DPad_Right);
	Map(InActions.NextPreset, EKeys::Gamepad_DPad_Left, -1.f);

	const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Numbers); ++Index)
	{
		Map(InActions.Preset, Numbers[Index], float(Index + 1));
	}

	Map(InActions.Lens, EKeys::L);
	Map(InActions.Lens, EKeys::Gamepad_FaceButton_Top);
}

bool UGMICameraSubsystem::LoadInput()
{
	if (InputContext && Actions.IsComplete())
	{
		return true;
	}
	const UGMICameraSettings* Settings = GetDefault<UGMICameraSettings>();
	InputContext = Settings->InputContext.LoadSynchronous();
	Actions.Pan = Settings->PanAction.LoadSynchronous();
	Actions.Tilt = Settings->TiltAction.LoadSynchronous();
	Actions.Zoom = Settings->ZoomAction.LoadSynchronous();
	Actions.NextCamera = Settings->NextCameraAction.LoadSynchronous();
	Actions.NextPreset = Settings->NextPresetAction.LoadSynchronous();
	Actions.Preset = Settings->PresetAction.LoadSynchronous();
	Actions.Lens = Settings->LensAction.LoadSynchronous();
	if (InputContext && Actions.IsComplete())
	{
		return true;
	}

	// No input assets (ATC.CreateCameraInput not run): the same mapping, made here
	UE_LOG(LogATC, Display, TEXT("GMI: no input assets in /ATCSim/Input, using the default camera mapping"));
	auto Make = [this](const TCHAR* Name)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = EInputActionValueType::Axis1D;
		Action->bConsumeInput = false;
		return Action;
	};
	Actions.Pan = Make(TEXT("IA_GMI_Pan"));
	Actions.Tilt = Make(TEXT("IA_GMI_Tilt"));
	Actions.Zoom = Make(TEXT("IA_GMI_Zoom"));
	Actions.NextCamera = Make(TEXT("IA_GMI_NextCamera"));
	Actions.NextPreset = Make(TEXT("IA_GMI_NextPreset"));
	Actions.Preset = Make(TEXT("IA_GMI_Preset"));
	Actions.Lens = Make(TEXT("IA_GMI_Lens"));
	InputContext = NewObject<UInputMappingContext>(this, TEXT("IMC_GMI"));
	AddDefaultMappings(InputContext, Actions);
	return true;
}

void UGMICameraSubsystem::SetupInput(APlayerController* Controller)
{
	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input || !LoadInput())
	{
		return;
	}
	// Above the project's own mappings, without taking their keys away (the actions do not consume input)
	Input->AddMappingContext(InputContext, 1);
	InputComponent = NewObject<UEnhancedInputComponent>(Controller, TEXT("GMICameraInput"));
	InputComponent->BindAction(Actions.Pan, ETriggerEvent::Triggered, this, &UGMICameraSubsystem::OnPan);
	InputComponent->BindAction(Actions.Tilt, ETriggerEvent::Triggered, this, &UGMICameraSubsystem::OnTilt);
	InputComponent->BindAction(Actions.Zoom, ETriggerEvent::Triggered, this, &UGMICameraSubsystem::OnZoom);
	InputComponent->BindAction(Actions.NextCamera, ETriggerEvent::Started, this, &UGMICameraSubsystem::OnNextCamera);
	InputComponent->BindAction(Actions.NextPreset, ETriggerEvent::Started, this, &UGMICameraSubsystem::OnNextPreset);
	InputComponent->BindAction(Actions.Preset, ETriggerEvent::Started, this, &UGMICameraSubsystem::OnPreset);
	InputComponent->BindAction(Actions.Lens, ETriggerEvent::Started, this, &UGMICameraSubsystem::OnLens);
	Controller->PushInputComponent(InputComponent);
	InputController = Controller;
}

void UGMICameraSubsystem::OnPan(const FInputActionValue& Value)
{
	if (AGMICamera* Camera = OperatedCamera())
	{
		Camera->PTZ->AddPanInput(Value.Get<float>());
	}
}

void UGMICameraSubsystem::OnTilt(const FInputActionValue& Value)
{
	if (AGMICamera* Camera = OperatedCamera())
	{
		Camera->PTZ->AddTiltInput(Value.Get<float>());
	}
}

void UGMICameraSubsystem::OnZoom(const FInputActionValue& Value)
{
	if (AGMICamera* Camera = OperatedCamera())
	{
		Camera->PTZ->AddZoomInput(Value.Get<float>());
	}
}

void UGMICameraSubsystem::OnNextCamera(const FInputActionValue& Value)
{
	SelectNextCamera(Value.Get<float>() < 0.f ? -1 : 1);
}

void UGMICameraSubsystem::OnNextPreset(const FInputActionValue& Value)
{
	if (AGMICamera* Camera = OperatedCamera())
	{
		Camera->PTZ->NextPreset(Value.Get<float>() < 0.f ? -1 : 1);
		RefreshLabels();
	}
}

void UGMICameraSubsystem::OnPreset(const FInputActionValue& Value)
{
	const int32 Number = FMath::RoundToInt(Value.Get<float>());
	AGMICamera* Camera = OperatedCamera();
	if (Camera && Number >= 1)
	{
		Camera->PTZ->GoToPresetIndex(Number - 1);
		RefreshLabels();
	}
}

void UGMICameraSubsystem::OnLens(const FInputActionValue& Value)
{
	if (AGMICamera* Camera = OperatedCamera())
	{
		Camera->PTZ->NextLens();
	}
}

// ---------------------------------------------------------------- Instructor panel and measuring

void UGMICameraSubsystem::ShowPanel(bool bShow)
{
	if (!bShow)
	{
		if (Panel)
		{
			Panel->RemoveFromParent();
		}
		return;
	}
	if (!Panel)
	{
		APlayerController* Controller = GetWorld()->GetFirstPlayerController();
		Panel = Controller ? CreateWidget<UGMICameraPanel>(Controller, UGMICameraPanel::StaticClass()) : CreateWidget<UGMICameraPanel>(GetWorld(), UGMICameraPanel::StaticClass());
	}
	if (Panel && !Panel->IsInViewport())
	{
		Panel->AddToViewport(50);
	}
	if (Panel)
	{
		Panel->SetCameras(GetCameras(), SelectedCamera.Get());
	}
	PriorityTimer = 0.f;
}

bool UGMICameraSubsystem::IsPanelShown() const
{
	return Panel && Panel->IsInViewport();
}

void UGMICameraSubsystem::SavePanelImage(const FString& Name)
{
	ShowPanel(true);
	if (!Panel)
	{
		return;
	}
	const TSharedRef<SWidget> Widget = Panel->TakeWidget();
	Widget->SlatePrepass(1.f);
	const FVector2D Size = Widget->GetDesiredSize();
	// 8 bits, so the export is a PNG
	UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, FMath::CeilToInt(Size.X), FMath::CeilToInt(Size.Y), RTF_RGBA8_SRGB, FLinearColor::Black);
	FWidgetRenderer Renderer(true);
	if (Target)
	{
		Renderer.DrawWidget(Target, Widget, Size, 0.f);
		FlushRenderingCommands();
		UKismetRenderingLibrary::ExportRenderTarget(GetWorld(), Target, FPaths::ProjectSavedDir() / TEXT("Screenshots/ATC"), Name + TEXT(".png"));
	}
	UE_LOG(LogATC, Display, TEXT("ATC.CameraUI: panel %.0fx%.0f px with %d cameras saved as %s.png"), Size.X, Size.Y, GetCameras().Num(), *Name);
}

void UGMICameraSubsystem::StartStats(float Seconds)
{
	StatsStart = FPlatformTime::Seconds();
	StatsUntil = StatsStart + Seconds;
	StatsFrames = 0;
	StatsGPUMs = StatsRenderMs = StatsGameMs = 0.0;
	for (FTrackedCamera& Entry : Tracked)
	{
		Entry.StatsCaptures = 0;
		Entry.StatsLiveFrames = 0;
	}
}

void UGMICameraSubsystem::TickStats(const TArray<int32>& Captured)
{
	if (StatsUntil <= 0.0)
	{
		return;
	}
	++StatsFrames;
	StatsGPUMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	StatsRenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	StatsGameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
	for (const int32 Index : Captured)
	{
		++Tracked[Index].StatsCaptures;
	}
	for (FTrackedCamera& Entry : Tracked)
	{
		Entry.StatsLiveFrames += Entry.State.bLive ? 1 : 0;
	}

	const double Now = FPlatformTime::Seconds();
	if (Now < StatsUntil || StatsFrames == 0)
	{
		return;
	}
	const double Seconds = Now - StatsStart;
	UE_LOG(LogATC, Display, TEXT("ATC.CameraStats: %.1f s, %d frames (%.0f fps): GPU %.2f ms, render thread %.2f ms, game thread %.2f ms per frame, captures %s"),
		Seconds, StatsFrames, StatsFrames / Seconds, StatsGPUMs / StatsFrames, StatsRenderMs / StatsFrames, StatsGameMs / StatsFrames,
		CVarCameraCaptures.GetValueOnGameThread() != 0 ? TEXT("on") : TEXT("off"));
	for (const FTrackedCamera& Entry : Tracked)
	{
		UE_LOG(LogATC, Display, TEXT("ATC.CameraStats: %-8s live %3.0f%% of the frames, %5.1f pictures/s, viewed %d, %s %.0f mm"),
			*Entry.Camera->CameraId.ToString(), 100.0 * Entry.StatsLiveFrames / StatsFrames, Entry.StatsCaptures / Seconds, Entry.State.bViewed ? 1 : 0,
			Entry.Camera == SelectedCamera ? TEXT("operated") : TEXT(""), Entry.Camera->PTZ->FocalLengthMm);
	}
	StatsUntil = 0.0;
}

namespace
{
	/** ATC.CameraUI [0|1]: the instructor panel with every camera feed. */
	FAutoConsoleCommandWithWorldAndArgs CameraUICommand(TEXT("ATC.CameraUI"), TEXT("ATC.CameraUI [0|1|save [name]]: instructor panel with every GMI camera feed (desktop window); save: as a picture in Saved/Screenshots/ATC"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UGMICameraSubsystem* Cameras = World ? World->GetSubsystem<UGMICameraSubsystem>() : nullptr;
			if (Cameras && Args.Num() > 0 && Args[0] == TEXT("save"))
			{
				Cameras->SavePanelImage(Args.Num() > 1 ? Args[1] : TEXT("camera_panel"));
			}
			else if (Cameras)
			{
				Cameras->ShowPanel(Args.Num() > 0 ? Args[0] != TEXT("0") : !Cameras->IsPanelShown());
			}
		}));

	/** ATC.CameraStats [seconds]: what the cameras cost; compare with atc.CameraCaptures 0. */
	FAutoConsoleCommandWithWorldAndArgs CameraStatsCommand(TEXT("ATC.CameraStats"), TEXT("ATC.CameraStats [seconds]: pictures per camera and GPU time per frame in the log"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UGMICameraSubsystem* Cameras = World ? World->GetSubsystem<UGMICameraSubsystem>() : nullptr)
			{
				Cameras->StartStats(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 10.f);
			}
		}));
}
