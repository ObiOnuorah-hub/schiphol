#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LensSetData.h"
#include "PTZControllerComponent.generated.h"

class USceneCaptureComponent2D;

/**
 * Pan, tilt and optical zoom of a camera, with limits, presets and smooth transitions. Input (joystick, keys, VR
 * controller) only ever reaches this component, never the capture itself; when the capture renders is up to
 * UGMICameraSubsystem.
 */
UCLASS(ClassGroup = (ATC), meta = (BlueprintSpawnableComponent))
class ATCCAMERA_API UPTZControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPTZControllerComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Called by the camera actor: the two pivots and the capture this component drives. */
	void Setup(USceneComponent* InPanPivot, USceneComponent* InTiltPivot, USceneCaptureComponent2D* InCapture);

	/** Joystick input, -1..1, scaled with the field of view: slower when zoomed in, like a real PTZ camera. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void AddPanInput(float Axis) { PanInput += Axis; }

	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void AddTiltInput(float Axis) { TiltInput += Axis; }

	/** Positive = zoom in. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void AddZoomInput(float Axis) { ZoomInput += Axis; }

	/** Zoom to a lens of the lens set (0 = first) in ZoomDuration; its sensor width counts from then on. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void SelectLens(int32 LensIndex);

	/** The next lens of the set; after the last one the first again. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void NextLens();

	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void SetFocalLength(float NewFocalLengthMm, bool bSmooth = true);

	/** Pan, tilt and zoom to a preset together in TransitionTime, ease in and ease out. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	bool GoToPreset(FGameplayTag PresetTag);

	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void GoToPresetIndex(int32 Index);

	/** The next (1) or previous (-1) preset. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void NextPreset(int32 Direction = 1);

	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void SetPanTilt(float NewPanDeg, float NewTiltDeg, bool bSmooth = true);

	UFUNCTION(BlueprintPure, Category = "ATC|Camera")
	float GetFieldOfView() const { return ULensLibrary::FocalLengthToFOV(FocalLengthMm, SensorWidthMm); }

	/** The preset the camera is at or on its way to; empty after manual input. */
	UFUNCTION(BlueprintPure, Category = "ATC|Camera")
	FGameplayTag GetCurrentPreset() const;

	/** The lens last selected; empty after zooming by hand or going to a preset. */
	FText GetLensName() const;

	bool IsMoving() const { return bTurning || bZooming; }

	/** A span of 360 degrees or more: no cable stop, the camera turns all the way round (a dome camera). */
	bool IsContinuousPan() const { return PanLimitsDeg.Y - PanLimitsDeg.X >= 360.f; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera")
	TObjectPtr<ULensSetData> LensSet;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera")
	TArray<FCameraPreset> Presets;

	/** Every input and every preset is clamped to these. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera|Limits")
	FVector2D PanLimitsDeg = FVector2D(-170.f, 170.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera|Limits")
	FVector2D TiltLimitsDeg = FVector2D(-45.f, 15.f);

	/** Zoom range of the camera: lenses, presets and zoom input stay within it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera|Limits", meta = (Units = "mm"))
	FVector2D FocalLimitsMm = FVector2D(24.f, 400.f);

	/** Degrees per second at the widest lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera|Speed")
	float PanSpeedDegS = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera|Speed")
	float TiltSpeedDegS = 25.f;

	/** Focal length factor per second at full zoom input (2 = doubles every second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera|Speed")
	float ZoomRatePerSecond = 2.f;

	/** Going to a preset: pan, tilt and zoom move together in this time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera|Speed", meta = (Units = "s"))
	float TransitionTime = 1.5f;

	/** Changing lenses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera|Speed", meta = (Units = "s"))
	float ZoomDuration = 1.f;

	/** Showcase: switch to the next preset every PresetCycleSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera")
	bool bCyclePresets = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera")
	float PresetCycleSeconds = 12.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "ATC|Camera")
	float PanDeg = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "ATC|Camera")
	float TiltDeg = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera")
	float FocalLengthMm = 50.f;

private:
	void StartTurn(float ToPan, float ToTilt, float Duration);
	void StartZoom(float ToFocalMm, float Duration);
	void TickZoom(float DeltaTime);
	void Apply();

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> PanPivot;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> TiltPivot;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> Capture;

	float PanInput = 0.f;
	float TiltInput = 0.f;
	float ZoomInput = 0.f;

	float SensorWidthMm = 36.f;
	int32 LensIndex = INDEX_NONE;
	int32 PresetIndex = INDEX_NONE;
	bool bAtPreset = false;

	bool bTurning = false;
	float TurnAlpha = 0.f;
	float TurnDuration = 1.f;
	float StartPan = 0.f, StartTilt = 0.f;
	float TargetPan = 0.f, TargetTilt = 0.f;

	bool bZooming = false;
	float ZoomAlpha = 0.f;
	float ZoomTime = 1.f;
	float StartFocalMm = 50.f, TargetFocalMm = 50.f;

	float CycleTimer = 0.f;
};
