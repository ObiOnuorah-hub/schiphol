#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GMICamera.generated.h"

class UPrimitiveComponent;
class UPTZControllerComponent;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * Airport camera on a mast or a roof: a pan pivot, a tilt pivot and a scene capture that renders into a render
 * target. The PTZ controller moves it; UGMICameraSubsystem decides when it renders (only while one of its screens is
 * seen, and at most two cameras at full rate).
 */
UCLASS(Blueprintable)
class ATCCAMERA_API AGMICamera : public AActor
{
	GENERATED_BODY()

public:
	AGMICamera();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The render target this camera fills; created at runtime when none is set. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	UTextureRenderTarget2D* GetRenderTarget();

	/** Renders one picture now. The camera subsystem calls this at the camera's rate; call it yourself for a still. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Camera")
	void CaptureNow();

	/** A screen that shows this camera. The camera only renders while one of its screens was on screen just now. */
	void AddViewer(UPrimitiveComponent* Screen);
	void RemoveViewer(UPrimitiveComponent* Screen);
	bool IsViewed() const;
	const TArray<TWeakObjectPtr<UPrimitiveComponent>>& GetViewers() const { return Viewers; }

	/** Pictures rendered since BeginPlay. */
	int32 GetCaptureCount() const { return CaptureCount; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Camera")
	TObjectPtr<USceneComponent> PanPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Camera")
	TObjectPtr<USceneComponent> TiltPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Camera")
	TObjectPtr<UStaticMeshComponent> Housing;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Camera")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Camera")
	TObjectPtr<UPTZControllerComponent> PTZ;

	/** Shown on the monitor, for example GMI-01. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ATC|Camera")
	FName CameraId = TEXT("GMI-01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera")
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/** 1280x720: on a 40 cm screen in VR more resolution is not visible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ATC|Camera")
	FIntPoint RenderTargetSize = FIntPoint(1280, 720);

	/** Pictures per second while the camera is live; 15 to 20 is smooth for a camera feed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera|Capture", meta = (Units = "Hz", ClampMin = "1", ClampMax = "60"))
	float CaptureRateHz = 20.f;

	/** Nothing further than this is drawn: the airport boundary as seen from this camera. 0 = no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Camera|Capture", meta = (Units = "m"))
	float MaxViewDistanceM = 0.f;

private:
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Viewers;
	int32 CaptureCount = 0;
};
