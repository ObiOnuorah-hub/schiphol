#include "MonitorDisplay.h"
#include "ATCSimClockSubsystem.h"
#include "GMICamera.h"
#include "GMICameraSubsystem.h"
#include "PTZControllerComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "ATCUnits.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// The label has two lines of at most this many characters ("GMI-TWR  Threshold36L", "03-11-2026 13:57:39Z  269 mm").
	// Roboto letters are at most about 0.62 of their height wide: sized on that the label always fits under the picture
	// and keeps one size (measuring the text itself came out far too small and changed with every second shown)
	constexpr float LabelChars = 30.f;
	constexpr float LetterWidth = 0.62f;

	// The touch buttons right of the label, in label heights: big enough to aim at with the eyes in VR
	constexpr float ButtonWidth = 3.4f;
	constexpr float ButtonHeight = 1.8f;
	constexpr float ButtonGap = 0.4f;
	constexpr float ButtonsWidth = AMonitorDisplay::NumButtons * ButtonWidth + (AMonitorDisplay::NumButtons - 1) * ButtonGap + 1.f;
	const TCHAR* const ButtonTexts[AMonitorDisplay::NumButtons] = { TEXT("<CAM"), TEXT("CAM>"), TEXT("<PRE"), TEXT("PRE>") };

	// Brightness of a button face: idle, looked at, pressed
	constexpr float ButtonIdle = 0.08f;
	constexpr float ButtonHover = 0.35f;
	constexpr float ButtonPressed = 0.9f;
}

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

	for (int32 Index = 0; Index < NumButtons; ++Index)
	{
		UStaticMeshComponent* Face = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("ButtonFace%d"), Index));
		Face->SetupAttachment(RootComponent);
		Face->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Face->SetCastShadow(false);
		Face->bHiddenInSceneCapture = true;
		if (Cube.Succeeded())
		{
			Face->SetStaticMesh(Cube.Object);
		}
		ButtonFaces.Add(Face);

		UTextRenderComponent* Text = CreateDefaultSubobject<UTextRenderComponent>(*FString::Printf(TEXT("ButtonLabel%d"), Index));
		Text->SetupAttachment(RootComponent);
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetTextRenderColor(FColor(230, 230, 230));
		Text->SetText(FText::FromString(ButtonTexts[Index]));
		Text->bHiddenInSceneCapture = true;
		ButtonLabels.Add(Text);
	}

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
		LabelSizeCm = FMath::Min(WidthCm * 0.05f, PictureSize.X / (LabelChars * LetterWidth + ButtonsWidth));
		Screen->SetRelativeTransform(FTransform(Facing, FVector(1.f, 0.f, 0.f), FVector(PictureSize.X / 100.f, PictureSize.Y / 100.f, 1.f)));
		Frame->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(-3.f, 0.f, 0.f), FVector(0.05f, (PictureSize.X + 6.f) / 100.f, (PictureSize.Y + 6.f) / 100.f)));
		Label->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(2.f, PictureSize.X * 0.5f, -PictureSize.Y * 0.5f - 2.f)));
		Label->SetVerticalAlignment(EVRTA_TextTop);
		Label->SetWorldSize(LabelSizeCm);
		PlaceButtons(-PictureSize.Y * 0.5f - 2.f - LabelSizeCm, 2.f);
		return;
	}

	// Over a screen of the tower model: that whole screen black, the picture as wide as fits from the top, the two label
	// lines in the strip under it. Everything a few millimetres in front of the real screen
	const float Margin = FMath::Max(0.5f, WidthCm * 0.02f);
	LabelSizeCm = FMath::Clamp(HeightCm * 0.06f, 1.f, 3.f);
	const float PictureWidth = FMath::Min(WidthCm - 2.f * Margin, (HeightCm - 2.f * LabelSizeCm - 3.f * Margin) * 16.f / 9.f);
	PictureSize = FVector2D(PictureWidth, PictureWidth * 9.f / 16.f);
	LabelSizeCm = FMath::Min(LabelSizeCm, PictureSize.X / (LabelChars * LetterWidth + ButtonsWidth));
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
	PlaceButtons(-HeightCm * 0.5f + Margin + LabelSizeCm, 0.45f);
}

void AMonitorDisplay::PlaceButtons(float CenterZ, float FrontX)
{
	// Right-aligned with the picture, level with the two label lines. Left in the picture is +Y
	ButtonSize = FVector2D(ButtonWidth, ButtonHeight) * LabelSizeCm;
	const float Gap = ButtonGap * LabelSizeCm;
	const float Total = NumButtons * ButtonSize.X + (NumButtons - 1) * Gap;
	ButtonFrontX = FrontX;
	ButtonCenters.SetNum(NumButtons);
	for (int32 Index = 0; Index < NumButtons && Index < ButtonFaces.Num(); ++Index)
	{
		const float Y = -PictureSize.X * 0.5f + Total - Index * (ButtonSize.X + Gap) - ButtonSize.X * 0.5f;
		ButtonCenters[Index] = FVector2D(Y, CenterZ);
		ButtonFaces[Index]->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(FrontX, Y, CenterZ), FVector(0.001f, ButtonSize.X / 100.f, ButtonSize.Y / 100.f)));
		ButtonLabels[Index]->SetRelativeLocation(FVector(FrontX + 0.1f, Y, CenterZ));
		ButtonLabels[Index]->SetWorldSize(ButtonSize.Y * 0.5f);
	}
}

int32 AMonitorDisplay::ButtonAt(const FVector& Eye, const FVector& Forward, float& OutDistance) const
{
	// Where the ray meets the plane of the buttons, then which button is there (in the actor's space)
	const FVector Normal = GetActorForwardVector();
	const float Along = Forward | Normal;
	if (Along > -0.1f || ButtonCenters.Num() != NumButtons)
	{
		return INDEX_NONE;
	}
	const FTransform& Transform = GetActorTransform();
	OutDistance = ((Transform.TransformPosition(FVector(ButtonFrontX, 0.f, 0.f)) - Eye) | Normal) / Along;
	if (OutDistance <= 0.f)
	{
		return INDEX_NONE;
	}
	const FVector Local = Transform.InverseTransformPosition(Eye + Forward * OutDistance);
	for (int32 Index = 0; Index < NumButtons; ++Index)
	{
		if (FMath::Abs(Local.Y - ButtonCenters[Index].X) <= ButtonSize.X * 0.5f && FMath::Abs(Local.Z - ButtonCenters[Index].Y) <= ButtonSize.Y * 0.5f)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FVector AMonitorDisplay::GetButtonLocation(int32 Index) const
{
	return ButtonCenters.IsValidIndex(Index) ? GetActorTransform().TransformPosition(FVector(ButtonFrontX, ButtonCenters[Index].X, ButtonCenters[Index].Y))
		: GetActorLocation();
}

void AMonitorDisplay::SetHoveredButton(int32 Index)
{
	if (HoveredButton != Index)
	{
		HoveredButton = Index;
		UpdateButtonLook();
	}
}

void AMonitorDisplay::PressButton(int32 Index)
{
	UGMICameraSubsystem* Cameras = GetWorld() ? GetWorld()->GetSubsystem<UGMICameraSubsystem>() : nullptr;
	if (!Cameras || Index < 0 || Index >= NumButtons)
	{
		return;
	}
	if (Index <= 1)
	{
		// The previous / next camera (in camera ID order) on this screen; it becomes the camera the joystick moves
		const TArray<AGMICamera*> All = Cameras->GetCameras();
		if (All.Num() > 0)
		{
			const int32 Current = All.IndexOfByKey(SourceCamera.Get());
			const int32 Next = Current == INDEX_NONE ? 0 : (Current + (Index == 0 ? -1 : 1) + All.Num()) % All.Num();
			SetSourceCamera(All[Next]);
			Cameras->SelectCamera(All[Next]);
		}
	}
	else if (SourceCamera)
	{
		SourceCamera->PTZ->NextPreset(Index == 2 ? -1 : 1);
		Cameras->SelectCamera(SourceCamera);
	}
	RefreshLabel();
	UE_LOG(LogATC, Display, TEXT("%s: %s, now %s"), *GetName(), ButtonTexts[Index], SourceCamera ? *SourceCamera->CameraId.ToString() : TEXT("no camera"));

	// A short flash, like a key that lights up
	PressedButton = Index;
	UpdateButtonLook();
	GetWorldTimerManager().SetTimer(PressTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		PressedButton = INDEX_NONE;
		UpdateButtonLook();
	}), 0.15f, false);
}

void AMonitorDisplay::UpdateButtonLook()
{
	for (int32 Index = 0; Index < ButtonInstances.Num(); ++Index)
	{
		const float Brightness = Index == PressedButton ? ButtonPressed : Index == HoveredButton ? ButtonHover : ButtonIdle;
		ButtonInstances[Index]->SetScalarParameterValue(TEXT("Brightness"), Brightness);
		ButtonLabels[Index]->SetTextRenderColor(Index == HoveredButton || Index == PressedButton ? FColor(255, 205, 40) : FColor(230, 230, 230));
	}
}

void AMonitorDisplay::BeginPlay()
{
	Super::BeginPlay();

	// The clock in the label changes every second: opaque text would leave a ghost of the old digits in the temporal
	// anti-aliasing (ATC.CreateTextMaterial)
	if (UMaterialInterface* TextMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/ATCSim/Materials/MI_TextTranslucent.MI_TextTranslucent"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		Label->SetTextMaterial(TextMaterial);
		for (UTextRenderComponent* Text : ButtonLabels)
		{
			Text->SetTextMaterial(TextMaterial);
		}
	}
	// Button faces: the screen material with a white picture, lit like a screen (brightness = idle / looked at / pressed)
	UMaterialInterface* Material = ScreenMaterial.LoadSynchronous();
	UTexture* White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	ButtonInstances.Reset();
	for (UStaticMeshComponent* Face : ButtonFaces)
	{
		if (Material)
		{
			UMaterialInstanceDynamic* Instance = Face->CreateDynamicMaterialInstance(0, Material);
			if (White)
			{
				Instance->SetTextureParameterValue(FeedParameter, White);
			}
			ButtonInstances.Add(Instance);
		}
	}
	UpdateButtonLook();
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
	// Line 1: camera and preset, line 2: time and focal length
	FString Text = SourceCamera ? SourceCamera->CameraId.ToString() : TEXT("NO SIGNAL");
	FString Second = Clock ? Clock->GetSimTimeUTC().ToString(TEXT("%d-%m-%Y %H:%M:%SZ")) : FString();
	if (SourceCamera)
	{
		const FGameplayTag Preset = SourceCamera->PTZ->GetCurrentPreset();
		if (Preset.IsValid())
		{
			// Cam.EHAM.Threshold18R -> Threshold18R
			FString Name = Preset.ToString();
			Name.Split(TEXT("."), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			Text += TEXT("  ") + Name;
		}
		Second += FString::Printf(TEXT("%s%.0f mm"), Second.IsEmpty() ? TEXT("") : TEXT("  "), SourceCamera->PTZ->FocalLengthMm);
	}
	if (!Second.IsEmpty())
	{
		Text += TEXT("\n") + Second;
	}
	const bool bSelected = Cameras && SourceCamera && Cameras->GetSelectedCamera() == SourceCamera;
	Label->SetTextRenderColor(bSelected ? FColor(255, 205, 40) : FColor(230, 230, 230));
	Label->SetText(FText::FromString(Text));
	Label->SetWorldSize(LabelSizeCm);
}

FVector AMonitorDisplay::GetPictureCenter() const
{
	return Screen->GetComponentLocation();
}

FVector2D AMonitorDisplay::GetPictureSize() const
{
	return PictureSize;
}
