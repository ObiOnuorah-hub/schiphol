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
	if (!RenderTarget)
	{
		RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, RenderTargetSize.X, RenderTargetSize.Y, RTF_RGBA8_SRGB, FLinearColor::Black);
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
