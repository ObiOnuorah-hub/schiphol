#include "GMICamera.h"
#include "GMICameraSubsystem.h"
#include "PTZControllerComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "HAL/IConsoleManager.h"

// The tower screens show a camera picture only a few hundred pixels wide, also at 3440x1440: 960x540 instead of 1280x720
// makes a picture about 1 ms cheaper on the GPU (one capture was 4.5 ms, 18-11-2026)
static TAutoConsoleVariable<float> CVarCameraResolutionScale(TEXT("atc.CameraResolutionScale"), 0.75f,
	TEXT("Size of the camera pictures relative to RenderTargetSize"), ECVF_Default);
// Shadows are the biggest part of a camera picture (1.4 of 4.5 ms GPU, 18-11-2026). Off: frames slower than 60 Hz while
// looking at the tower screens at 3440x1440 went from about 6 to 2 % (the pictures are small on the screens)
static TAutoConsoleVariable<int32> CVarCameraShadows(TEXT("atc.CameraShadows"), 0,
	TEXT("1: camera pictures with the sun's shadows, 0: without (cheaper)"), ECVF_Default);

AGMICamera::AGMICamera()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Mount"));

	PanPivot = CreateDefaultSubobject<USceneComponent>(TEXT("PanPivot"));
	PanPivot->SetupAttachment(RootComponent);

	TiltPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TiltPivot"));
	TiltPivot->SetupAttachment(PanPivot);

	Housing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Housing"));
	Housing->SetupAttachment(TiltPivot);
	Housing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Housing->SetRelativeLocation(FVector(-25.f, 0.f, 0.f));
	Housing->SetRelativeScale3D(FVector(0.5f, 0.25f, 0.25f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Housing->SetStaticMesh(Cube.Object);
	}

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(TiltPivot);
	Capture->bCaptureEveryFrame = false;      // UGMICameraSubsystem captures at CaptureRateHz
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	// Already tonemapped: the monitor material only has to show it
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	// The heavy and the screen-space parts of a frame are not worth their cost in a 1280x720 camera picture
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetScreenSpaceReflections(false);
	Capture->ShowFlags.SetScreenSpaceAO(false);
	Capture->ShowFlags.SetContactShadows(false);
	// FXAA instead of TSR: temporal anti-aliasing needs the frames just before, and a camera at 5 to 20 Hz has none
	Capture->ShowFlags.SetTemporalAA(false);
	Capture->LODDistanceFactor = 2.f;
	// A camera sets its exposure in a moment, not in seconds like an eye. A capture only adapts by the frame time of
	// the frames it renders, so a camera whose screen just came into view would otherwise start dark
	Capture->PostProcessSettings.bOverride_AutoExposureSpeedUp = true;
	Capture->PostProcessSettings.AutoExposureSpeedUp = 20.f;
	Capture->PostProcessSettings.bOverride_AutoExposureSpeedDown = true;
	Capture->PostProcessSettings.AutoExposureSpeedDown = 20.f;

	PTZ = CreateDefaultSubobject<UPTZControllerComponent>(TEXT("PTZ"));
}

UTextureRenderTarget2D* AGMICamera::GetRenderTarget()
{
	const float Scale = FMath::Clamp(CVarCameraResolutionScale.GetValueOnGameThread(), 0.25f, 1.f);
	const int32 Width = FMath::Max(FMath::RoundToInt32(RenderTargetSize.X * Scale), 16);
	const int32 Height = FMath::Max(FMath::RoundToInt32(RenderTargetSize.Y * Scale), 16);
	if (!RenderTarget)
	{
		RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, Width, Height, RTF_RGBA8_SRGB, FLinearColor::Black);
	}
	else if (RenderTarget->SizeX != Width || RenderTarget->SizeY != Height)
	{
		RenderTarget->ResizeTarget(Width, Height);
	}
	Capture->TextureTarget = RenderTarget;
	return RenderTarget;
}

void AGMICamera::BeginPlay()
{
	Super::BeginPlay();
	GetRenderTarget();
	Capture->HideComponent(Housing);
	Capture->MaxViewDistanceOverride = MaxViewDistanceM > 0.f ? MaxViewDistanceM * 100.f : -1.f;
	PTZ->Setup(PanPivot, TiltPivot, Capture);
	if (UGMICameraSubsystem* Cameras = GetWorld()->GetSubsystem<UGMICameraSubsystem>())
	{
		Cameras->RegisterCamera(this);
	}
}

void AGMICamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGMICameraSubsystem* Cameras = GetWorld() ? GetWorld()->GetSubsystem<UGMICameraSubsystem>() : nullptr)
	{
		Cameras->UnregisterCamera(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AGMICamera::CaptureNow()
{
	if (Capture->TextureTarget)
	{
		GetRenderTarget();   // follows atc.CameraResolutionScale
		Capture->ShowFlags.SetDynamicShadows(CVarCameraShadows.GetValueOnGameThread() != 0);
		Capture->CaptureScene();
		++CaptureCount;
	}
}

void AGMICamera::AddViewer(UPrimitiveComponent* Screen)
{
	Viewers.AddUnique(Screen);
}

void AGMICamera::RemoveViewer(UPrimitiveComponent* Screen)
{
	Viewers.Remove(Screen);
}

bool AGMICamera::IsViewed() const
{
	// On screen only: a scene capture that happens to see a monitor does not count
	const UWorld* World = GetWorld();
	for (const TWeakObjectPtr<UPrimitiveComponent>& Screen : Viewers)
	{
		if (World && Screen.IsValid() && World->TimeSince(Screen->GetLastRenderTimeOnScreen()) <= FMath::Max(0.25f, World->DeltaTimeSeconds * 2.f))
		{
			return true;
		}
	}
	return false;
}
