#include "MonitorDisplay.h"
#include "ATCSimClockSubsystem.h"
#include "GMICamera.h"
#include "GMICameraSubsystem.h"
#include "PTZControllerComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AMonitorDisplay::AMonitorDisplay()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Black(TEXT("/Engine/EngineDebugMaterials/BlackUnlitMaterial.BlackUnlitMaterial"));
	BlackMaterial = Black.Object;

	// A camera that happens to look into the tower does not need to see the screens (and must not count as their viewer)
	Screen = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Screen"));
	Screen->SetupAttachment(RootComponent);
	Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Screen->SetCastShadow(false);
	Screen->bHiddenInSceneCapture = true;
	if (Plane.Succeeded())
	{
		Screen->SetStaticMesh(Plane.Object);
	}

	Frame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Frame"));
	Frame->SetupAttachment(RootComponent);
	Frame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Frame->bHiddenInSceneCapture = true;
	if (Cube.Succeeded())
	{
		Frame->SetStaticMesh(Cube.Object);
	}

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetHorizontalAlignment(EHTA_Left);
	Label->SetTextRenderColor(FColor(230, 230, 230));
	Label->SetText(FText::FromString(TEXT("GMI")));
	Label->bHiddenInSceneCapture = true;

	ScreenMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/ATCSim/Materials/M_Monitor.M_Monitor")));
}

void AMonitorDisplay::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	LayOut();
}

void AMonitorDisplay::LayOut()
{
	// The plane faces +Z with U along +X: turn it a quarter around its normal, then pitch -90 to face +X.
	// A viewer in front looks along -X, so U (left to right in the picture) must run along -Y.
	const FQuat Facing = FQuat(FRotator(-90.f, 0.f, 0.f)) * FQuat(FRotator(0.f, -90.f, 0.f));
	if (HeightCm <= 0.f)
	{
		// A monitor of its own: the picture in a frame, the label under it
		PictureSize = FVector2D(WidthCm, WidthCm * 9.f / 16.f);
		LabelSizeCm = WidthCm * 0.05f;
		Screen->SetRelativeTransform(FTransform(Facing, FVector(1.f, 0.f, 0.f), FVector(PictureSize.X / 100.f, PictureSize.Y / 100.f, 1.f)));
		Frame->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(-3.f, 0.f, 0.f), FVector(0.05f, (PictureSize.X + 6.f) / 100.f, (PictureSize.Y + 6.f) / 100.f)));
		Label->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(2.f, PictureSize.X * 0.5f, -PictureSize.Y * 0.5f - 2.f)));
		Label->SetVerticalAlignment(EVRTA_TextTop);
		Label->SetWorldSize(LabelSizeCm);
		return;
	}

	// Over a screen of the tower model: that whole screen black, the picture as wide as fits from the top, the label
	// in the strip under it. Everything a few millimetres in front of the real screen
	const float Margin = FMath::Max(0.5f, WidthCm * 0.02f);
	LabelSizeCm = FMath::Clamp(HeightCm * 0.07f, 1.2f, 3.5f);
	const float PictureWidth = FMath::Min(WidthCm - 2.f * Margin, (HeightCm - LabelSizeCm - 3.f * Margin) * 16.f / 9.f);
	PictureSize = FVector2D(PictureWidth, PictureWidth * 9.f / 16.f);
	const float PictureZ = HeightCm * 0.5f - Margin - PictureSize.Y * 0.5f;
	Frame->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.2f, 0.f, 0.f), FVector(0.001f, WidthCm / 100.f, HeightCm / 100.f)));
	if (BlackMaterial)
	{
		Frame->SetMaterial(0, BlackMaterial);
	}
	Screen->SetRelativeTransform(FTransform(Facing, FVector(0.4f, 0.f, PictureZ), FVector(PictureSize.X / 100.f, PictureSize.Y / 100.f, 1.f)));
	Label->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.5f, PictureSize.X * 0.5f, -HeightCm * 0.5f + Margin)));
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(LabelSizeCm);
}

void AMonitorDisplay::BeginPlay()
{
	Super::BeginPlay();

	SetSourceCamera(SourceCamera);
	if (UGMICameraSubsystem* Cameras = GetWorld()->GetSubsystem<UGMICameraSubsystem>())
	{
		Cameras->RegisterMonitor(this);
	}
	GetWorldTimerManager().SetTimer(LabelTimer, this, &AMonitorDisplay::RefreshLabel, 1.f, true);
}

void AMonitorDisplay::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGMICameraSubsystem* Cameras = GetWorld() ? GetWorld()->GetSubsystem<UGMICameraSubsystem>() : nullptr)
	{
		Cameras->UnregisterMonitor(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AMonitorDisplay::SetSourceCamera(AGMICamera* NewCamera)
{
	if (SourceCamera && SourceCamera != NewCamera)
	{
		SourceCamera->RemoveViewer(Screen);
	}
	SourceCamera = NewCamera;
	UMaterialInterface* Material = ScreenMaterial.LoadSynchronous();
	if (Material && SourceCamera)
	{
		if (!ScreenInstance)
		{
			ScreenInstance = Screen->CreateDynamicMaterialInstance(0, Material);
		}
		ScreenInstance->SetTextureParameterValue(FeedParameter, SourceCamera->GetRenderTarget());
		// The camera only renders while one of its screens is seen
		SourceCamera->AddViewer(Screen);
	}
	RefreshLabel();
}

void AMonitorDisplay::RefreshLabel()
{
	const UWorld* World = GetWorld();
	const UATCSimClockSubsystem* Clock = World ? World->GetSubsystem<UATCSimClockSubsystem>() : nullptr;
	const UGMICameraSubsystem* Cameras = World ? World->GetSubsystem<UGMICameraSubsystem>() : nullptr;
	FString Text = SourceCamera ? SourceCamera->CameraId.ToString() : TEXT("NO SIGNAL");
	if (Clock)
	{
		Text += TEXT("  ") + Clock->GetSimTimeUTC().ToString(TEXT("%d-%m-%Y %H:%M:%SZ"));
	}
	if (SourceCamera)
	{
		Text += FString::Printf(TEXT("  %.0f mm"), SourceCamera->PTZ->FocalLengthMm);
		const FGameplayTag Preset = SourceCamera->PTZ->GetCurrentPreset();
		if (Preset.IsValid())
		{
			// Cam.EHAM.Threshold18R -> Threshold18R
			FString Name = Preset.ToString();
			Name.Split(TEXT("."), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			Text += TEXT("  ") + Name;
		}
	}
	const bool bSelected = Cameras && SourceCamera && Cameras->GetSelectedCamera() == SourceCamera;
	Label->SetTextRenderColor(bSelected ? FColor(255, 205, 40) : FColor(230, 230, 230));
	Label->SetText(FText::FromString(Text));

	// Never wider than the picture: a long label gets smaller letters
	Label->SetWorldSize(LabelSizeCm);
	const float Width = Label->GetTextLocalSize().Y;
	if (Width > PictureSize.X && Width > 0.f)
	{
		Label->SetWorldSize(LabelSizeCm * PictureSize.X / Width);
	}
}

FVector AMonitorDisplay::GetPictureCenter() const
{
	return Screen->GetComponentLocation();
}

FVector2D AMonitorDisplay::GetPictureSize() const
{
	return PictureSize;
}
