#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GMICameraPanel.generated.h"

class AGMICamera;
class UTextBlock;
class UUniformGridPanel;

/**
 * Instructor panel: every GMI camera feed (the same render targets as the tower screens) with its ID, focal length and
 * preset under it, two per row in the top right corner of the desktop window. The operated camera is marked yellow.
 */
UCLASS()
class ATCCAMERA_API UGMICameraPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetCameras(const TArray<AGMICamera*>& InCameras, const AGMICamera* Selected);

	/** Rewrites the captions. */
	void Refresh(const AGMICamera* Selected);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UUniformGridPanel> Grid;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> Captions;

	TArray<TWeakObjectPtr<AGMICamera>> Cameras;
	TWeakObjectPtr<const AGMICamera> SelectedCamera;
	float CaptionTimer = 0.f;
};
