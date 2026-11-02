#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MonitorDisplay.generated.h"

class AGMICamera;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Screen in the tower that shows a GMI camera. The screen faces the actor's +X. Either a monitor of its own (a frame
 * with the picture and a label under it) or laid over a screen of the tower model (HeightCm > 0).
 */
UCLASS(Blueprintable)
class ATCCAMERA_API AMonitorDisplay : public AActor
{
	GENERATED_BODY()

public:
	AMonitorDisplay();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Monitor")
	TObjectPtr<UStaticMeshComponent> Screen;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Monitor")
	TObjectPtr<UStaticMeshComponent> Frame;

	/** Camera ID, UTC time, focal length and preset under the picture, like on real camera feeds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Monitor")
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Monitor")
	TObjectPtr<AGMICamera> SourceCamera;

	/** Unlit material with a texture parameter for the feed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Monitor")
	TSoftObjectPtr<UMaterialInterface> ScreenMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Monitor")
	FName FeedParameter = TEXT("Feed");

	/** Width of the screen in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Monitor", meta = (Units = "cm"))
	float WidthCm = 100.f;

	/**
	 * Laid over a screen of the tower model (ATC.SetupTowerScreens): WidthCm x HeightCm is that whole screen. It turns
	 * black, the 16:9 picture fills it from the top and the label goes under the picture. 0 = a monitor of its own,
	 * 16:9, with a frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Monitor", meta = (Units = "cm"))
	float HeightCm = 0.f;

	/** Which screen of the tower this is (ATC.Monitor <number> <camera>). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Monitor")
	int32 MonitorNumber = 1;

	/** Show another camera on this screen (camera switcher in the tower). */
	UFUNCTION(BlueprintCallable, Category = "ATC|Monitor")
	void SetSourceCamera(AGMICamera* NewCamera);

	/** Rewrites the label; yellow while this screen's camera is the one the joystick moves. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Monitor")
	void RefreshLabel();

	/** Middle of the picture (world). */
	FVector GetPictureCenter() const;

	/** Size of the picture (cm). */
	FVector2D GetPictureSize() const;

private:
	void LayOut();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScreenInstance;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BlackMaterial;

	FVector2D PictureSize = FVector2D(100.f, 56.25f);
	float LabelSizeCm = 5.f;
	FTimerHandle LabelTimer;
};
