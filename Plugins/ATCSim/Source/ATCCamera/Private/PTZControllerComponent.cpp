#include "PTZControllerComponent.h"
#include "Components/SceneCaptureComponent2D.h"

UPTZControllerComponent::UPTZControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UPTZControllerComponent::Setup(USceneComponent* InPanPivot, USceneComponent* InTiltPivot, USceneCaptureComponent2D* InCapture)
{
	PanPivot = InPanPivot;
	TiltPivot = InTiltPivot;
	Capture = InCapture;
	Apply();
}

void UPTZControllerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (LensSet && LensSet->Lenses.Num() > 0)
	{
		SensorWidthMm = LensSet->Lenses[0].SensorWidthMm;
	}
	StartZoom(FocalLengthMm, 0.f);
	if (Presets.Num() > 0)
	{
		// Within the limits like every other preset, so the first joystick input does not make it jump
		const FCameraPreset& First = Presets[0];
		StartTurn(First.PanDeg, First.TiltDeg, 0.f);
		StartZoom(First.FocalLengthMm, 0.f);
		PresetIndex = 0;
		bAtPreset = true;
	}
	Apply();
}

void UPTZControllerComponent::SelectLens(int32 Index)
{
	if (LensSet && LensSet->Lenses.IsValidIndex(Index))
	{
		LensIndex = Index;
		bAtPreset = false;
		SensorWidthMm = LensSet->Lenses[Index].SensorWidthMm;
		StartZoom(LensSet->Lenses[Index].FocalLengthMm, ZoomDuration);
	}
}

void UPTZControllerComponent::NextLens()
{
	if (LensSet && LensSet->Lenses.Num() > 0)
	{
		SelectLens((LensIndex + 1) % LensSet->Lenses.Num());
	}
}

void UPTZControllerComponent::SetFocalLength(float NewFocalLengthMm, bool bSmooth)
{
	LensIndex = INDEX_NONE;
	bAtPreset = false;
	StartZoom(NewFocalLengthMm, bSmooth ? ZoomDuration : 0.f);
}

void UPTZControllerComponent::SetPanTilt(float NewPanDeg, float NewTiltDeg, bool bSmooth)
{
	bAtPreset = false;
	StartTurn(NewPanDeg, NewTiltDeg, bSmooth ? TransitionTime : 0.f);
}

bool UPTZControllerComponent::GoToPreset(FGameplayTag PresetTag)
{
	for (int32 Index = 0; Index < Presets.Num(); ++Index)
	{
		if (Presets[Index].Tag == PresetTag)
		{
			GoToPresetIndex(Index);
			return true;
		}
	}
	return false;
}

void UPTZControllerComponent::GoToPresetIndex(int32 Index)
{
	if (Presets.IsValidIndex(Index))
	{
		const FCameraPreset& Preset = Presets[Index];
		PresetIndex = Index;
		bAtPreset = true;
		LensIndex = INDEX_NONE;
		// Pan, tilt and zoom arrive together
		StartTurn(Preset.PanDeg, Preset.TiltDeg, TransitionTime);
		StartZoom(Preset.FocalLengthMm, TransitionTime);
	}
}

void UPTZControllerComponent::NextPreset(int32 Direction)
{
	if (Presets.Num() > 0)
	{
		const int32 Step = Direction >= 0 ? 1 : -1;
		const int32 From = PresetIndex == INDEX_NONE ? (Step > 0 ? -1 : 0) : PresetIndex;
		GoToPresetIndex((From + Step + Presets.Num()) % Presets.Num());
	}
}

FGameplayTag UPTZControllerComponent::GetCurrentPreset() const
{
	return bAtPreset && Presets.IsValidIndex(PresetIndex) ? Presets[PresetIndex].Tag : FGameplayTag();
}

FText UPTZControllerComponent::GetLensName() const
{
	return LensSet && LensSet->Lenses.IsValidIndex(LensIndex) ? LensSet->Lenses[LensIndex].Name : FText::GetEmpty();
}

void UPTZControllerComponent::StartTurn(float ToPan, float ToTilt, float Duration)
{
	// A preset or a set direction takes over from the joystick, also while that is still slowing down
	PanRate = TiltRate = 0.f;
	StartPan = PanDeg;
	StartTilt = TiltDeg;
	TargetTilt = FMath::Clamp(ToTilt, TiltLimitsDeg.X, TiltLimitsDeg.Y);
	if (IsContinuousPan())
	{
		TargetPan = StartPan + FMath::FindDeltaAngleDegrees(StartPan, ToPan);
	}
	else
	{
		// Shortest way round, but only when that way stays within the limits (a mast camera cannot turn through its cable stop)
		const float Clamped = FMath::Clamp(ToPan, PanLimitsDeg.X, PanLimitsDeg.Y);
		const float Shortest = StartPan + FMath::FindDeltaAngleDegrees(StartPan, Clamped);
		TargetPan = (Shortest >= PanLimitsDeg.X && Shortest <= PanLimitsDeg.Y) ? Shortest : Clamped;
	}

	if (Duration <= 0.f)
	{
		PanDeg = IsContinuousPan() ? FRotator::NormalizeAxis(TargetPan) : TargetPan;
		TiltDeg = TargetTilt;
		bTurning = false;
		Apply();
		return;
	}
	TurnDuration = Duration;
	TurnAlpha = 0.f;
	bTurning = true;
}

void UPTZControllerComponent::StartZoom(float ToFocalMm, float Duration)
{
	ZoomRate = 0.f;
	StartFocalMm = FocalLengthMm;
	TargetFocalMm = FMath::Clamp(ToFocalMm, FocalLimitsMm.X, FocalLimitsMm.Y);
	if (Duration <= 0.f)
	{
		FocalLengthMm = TargetFocalMm;
		bZooming = false;
		Apply();
		return;
	}
	ZoomTime = Duration;
	ZoomAlpha = 0.f;
	bZooming = true;
}

void UPTZControllerComponent::TickZoom(float DeltaTime)
{
	ZoomAlpha = FMath::Min(ZoomAlpha + DeltaTime / ZoomTime, 1.f);
	const float T = FMath::SmoothStep(0.f, 1.f, ZoomAlpha);
	// Logarithmic: 24 -> 50 mm feels as fast as 50 -> 100 mm, like a real zoom lens
	FocalLengthMm = ULensLibrary::InterpolateFocalLength(StartFocalMm, TargetFocalMm, T);
	bZooming = ZoomAlpha < 1.f;
}

void UPTZControllerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bCyclePresets && Presets.Num() > 1)
	{
		CycleTimer += DeltaTime;
		if (CycleTimer >= PresetCycleSeconds)
		{
			CycleTimer = 0.f;
			NextPreset(1);
		}
	}

	const bool bHasInput = !FMath::IsNearlyZero(PanInput) || !FMath::IsNearlyZero(TiltInput) || !FMath::IsNearlyZero(ZoomInput);
	if (bHasInput)
	{
		// Manual input takes over from a running preset or lens change
		bTurning = bZooming = false;
		bAtPreset = false;
	}

	// The joystick speed: slower when zoomed in, or the picture flies away. It follows the stick in InputSmoothingSeconds,
	// so the picture starts and stops smoothly (the first frames of a key press are not a jump to full speed)
	const float Scale = GetFieldOfView() / ULensLibrary::FocalLengthToFOV(FocalLimitsMm.X, SensorWidthMm);
	const float WantPan = FMath::Clamp(PanInput, -1.f, 1.f) * PanSpeedDegS * Scale;
	const float WantTilt = FMath::Clamp(TiltInput, -1.f, 1.f) * TiltSpeedDegS * Scale;
	const float WantZoom = FMath::Clamp(ZoomInput, -1.f, 1.f);
	const float Follow = InputSmoothingSeconds > 0.f ? 1.f - FMath::Exp(-DeltaTime / InputSmoothingSeconds) : 1.f;
	PanRate += (WantPan - PanRate) * Follow;
	TiltRate += (WantTilt - TiltRate) * Follow;
	ZoomRate += (WantZoom - ZoomRate) * Follow;
	if (!bHasInput)
	{
		// Come to rest: what is left after the stick was let go is less than the eye sees
		PanRate = FMath::Abs(PanRate) < 0.005f * Scale ? 0.f : PanRate;
		TiltRate = FMath::Abs(TiltRate) < 0.005f * Scale ? 0.f : TiltRate;
		ZoomRate = FMath::Abs(ZoomRate) < 0.002f ? 0.f : ZoomRate;
	}

	if (!bTurning && (PanRate != 0.f || TiltRate != 0.f))
	{
		const float NewPan = PanDeg + PanRate * DeltaTime;
		PanDeg = IsContinuousPan() ? FRotator::NormalizeAxis(NewPan) : FMath::Clamp(NewPan, PanLimitsDeg.X, PanLimitsDeg.Y);
		TiltDeg = FMath::Clamp(TiltDeg + TiltRate * DeltaTime, TiltLimitsDeg.X, TiltLimitsDeg.Y);
	}
	else if (bTurning)
	{
		TurnAlpha = FMath::Min(TurnAlpha + DeltaTime / TurnDuration, 1.f);
		const float T = FMath::SmoothStep(0.f, 1.f, TurnAlpha);
		PanDeg = FMath::Lerp(StartPan, TargetPan, T);
		TiltDeg = FMath::Lerp(StartTilt, TargetTilt, T);
		bTurning = TurnAlpha < 1.f;
		if (!bTurning && IsContinuousPan())
		{
			PanDeg = FRotator::NormalizeAxis(PanDeg);
		}
	}
	if (!bZooming && ZoomRate != 0.f)
	{
		LensIndex = INDEX_NONE;
		FocalLengthMm = FMath::Clamp(FocalLengthMm * FMath::Pow(ZoomRatePerSecond, ZoomRate * DeltaTime), FocalLimitsMm.X, FocalLimitsMm.Y);
	}
	else if (bZooming)
	{
		TickZoom(DeltaTime);
	}
	PanInput = TiltInput = ZoomInput = 0.f;
	Apply();
}

void UPTZControllerComponent::Apply()
{
	if (PanPivot)
	{
		PanPivot->SetRelativeRotation(FRotator(0.f, PanDeg, 0.f));
	}
	if (TiltPivot)
	{
		TiltPivot->SetRelativeRotation(FRotator(TiltDeg, 0.f, 0.f));
	}
	if (Capture)
	{
		Capture->FOVAngle = GetFieldOfView();
	}
}
