#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GMICameraSubsystem.generated.h"

class AGMICamera;
class AMonitorDisplay;
class APlayerController;
class UEnhancedInputComponent;
class UGMICameraPanel;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** The operator's actions for the GMI cameras (all Axis1D: buttons give +1 or -1 through modifiers). */
USTRUCT()
struct ATCCAMERA_API FGMIInputActions
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UInputAction> Pan;

	UPROPERTY()
	TObjectPtr<UInputAction> Tilt;

	UPROPERTY()
	TObjectPtr<UInputAction> Zoom;

	UPROPERTY()
	TObjectPtr<UInputAction> NextCamera;

	UPROPERTY()
	TObjectPtr<UInputAction> NextPreset;

	UPROPERTY()
	TObjectPtr<UInputAction> Preset;

	UPROPERTY()
	TObjectPtr<UInputAction> Lens;

	bool IsComplete() const { return Pan && Tilt && Zoom && NextCamera && NextPreset && Preset && Lens; }
};

/** One camera as the capture planner sees it. */
struct ATCCAMERA_API FGMICaptureState
{
	/** One of its screens (or the instructor panel) was on screen just now. */
	bool bViewed = false;
	/** How much its screens matter now: bigger and nearer the middle of the view is more; the operated camera highest. */
	float Priority = 0.f;
	float LiveRateHz = 20.f;
	/** Frames since its last picture; -1 = the next picture comes at once (its screen just came into view). */
	int32 FramesSincePicture = -1;
	/**
	 * Frames between two pictures: the rate in whole frames, so the pictures come at an even pace (20 Hz at 55 fps
	 * would otherwise be 3, 3, 2, 3, 3, 2 frames apart: a picture that judders).
	 */
	int32 IntervalFrames = 0;
	/** Frame time, smoothed over about a second (the interval must not flip with every frame). */
	float FrameSeconds = 0.f;
	/** Set by the planner: renders at LiveRateHz. */
	bool bLive = false;
};

/**
 * Every GMI camera and tower screen of a world.
 *
 * Render budget: each capture renders the scene again, so at most MaxLiveCameras are live at their own rate (the
 * camera the operator moves first, then the screens biggest in view), the others whose screen is in view get
 * BackgroundRateHz, and a camera whose screens nobody sees does not render at all.
 *
 * Operator: Enhanced Input (joystick, keys or VR controller) moves the selected camera, always through its PTZ
 * controller. Looking at a camera's screen selects that camera. The instructor gets every feed in a UMG panel.
 */
UCLASS()
class ATCCAMERA_API UGMICameraSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterCamera(AGMICamera* Camera);
	void UnregisterCamera(AGMICamera* Camera);
	void RegisterMonitor(AMonitorDisplay* Monitor);
	void UnregisterMonitor(AMonitorDisplay* Monitor);

	/** Every camera of the world, by ID. */
	TArray<AGMICamera*> GetCameras() const;

	/** The camera the joystick moves; its screens get a yellow label. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void SelectCamera(AGMICamera* Camera);

	/** The next (1) or previous (-1) camera that is on a screen, by ID. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void SelectNextCamera(int32 Direction = 1);

	UFUNCTION(BlueprintPure, Category = "ATC|Camera")
	AGMICamera* GetSelectedCamera() const { return SelectedCamera.Get(); }

	/** Renders at its own rate right now (one of the MaxLiveCameras). */
	bool IsLive(const AGMICamera* Camera) const;

	/** Instructor panel on the desktop window with every camera feed (UMG). A headset does not show it. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void ShowPanel(bool bShow);

	bool IsPanelShown() const;

	/** The instructor panel as a picture in Saved/Screenshots/ATC (a screenshot of the window may leave the widgets out). */
	void SavePanelImage(const FString& Name);

	/** Measures for Seconds of real time: pictures per camera and GPU, render and game thread time per frame (ATC.CameraStats). */
	void StartStats(float Seconds);

	/**
	 * One frame of capture planning, without the world (tests): the MaxLive viewed cameras with the highest priority
	 * are live, the other viewed ones get BackgroundRateHz, the rest nothing. Every camera renders every IntervalFrames
	 * frames; OutCapture gets the cameras to render now, most overdue first, at most MaxPerFrame. A camera that is just
	 * due waits one frame when another one already renders in this frame (one capture per frame where it can: an even
	 * GPU load instead of a heavy frame now and then), and keeps its rhythm after that.
	 */
	static void PlanCaptures(TArray<FGMICaptureState>& States, float DeltaTime, int32 MaxLive, float BackgroundRateHz, int32 MaxPerFrame,
		TArray<int32>& OutCapture);

	/**
	 * Default operator mapping. Keys: arrows pan and tilt, PageUp/PageDown or +/- zoom, Tab next camera, comma and
	 * period presets, 1-9 a preset, L the next lens. Gamepad: right stick, triggers, shoulders, d-pad, Y. VR: left
	 * thumbstick, X/Y (A/B on Index) zoom, thumbstick click next camera.
	 */
	static void AddDefaultMappings(UInputMappingContext* Context, const FGMIInputActions& InActions);

private:
	struct FTrackedCamera
	{
		TWeakObjectPtr<AGMICamera> Camera;
		FGMICaptureState State;
		int32 StatsCaptures = 0;
		int32 StatsLiveFrames = 0;
	};

	FTrackedCamera* Find(const AGMICamera* Camera);
	const FTrackedCamera* Find(const AGMICamera* Camera) const;
	void SetupInput(APlayerController* Controller);
	bool LoadInput();
	void RefreshPriorities();
	void RefreshLabels();
	void TickStats(const TArray<int32>& Captured);
	bool GetView(FVector& OutLocation, FVector& OutForward) const;
	/** The camera of the screen the view points at, if any. */
	AGMICamera* CameraInView() const;
	/** The camera an input is for: the screen looked at, else the selected one. */
	AGMICamera* OperatedCamera();

	void OnPan(const FInputActionValue& Value);
	void OnTilt(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void OnNextCamera(const FInputActionValue& Value);
	void OnNextPreset(const FInputActionValue& Value);
	void OnPreset(const FInputActionValue& Value);
	void OnLens(const FInputActionValue& Value);

	TArray<FTrackedCamera> Tracked;
	TArray<TWeakObjectPtr<AMonitorDisplay>> Monitors;
	TWeakObjectPtr<AGMICamera> SelectedCamera;

	UPROPERTY(Transient)
	FGMIInputActions Actions;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> InputContext;

	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> InputComponent;

	TWeakObjectPtr<APlayerController> InputController;

	UPROPERTY(Transient)
	TObjectPtr<UGMICameraPanel> Panel;

	double LastTickTime = 0.0;
	float PriorityTimer = 0.f;

	double StatsStart = 0.0;
	double StatsUntil = 0.0;
	int32 StatsFrames = 0;
	double StatsGPUMs = 0.0;
	double StatsRenderMs = 0.0;
	double StatsGameMs = 0.0;
};
