#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RadioLogDisplay.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

/** Screen in the tower with the last radio transmissions, newest at the bottom. Faces the actor's +X. */
UCLASS(Blueprintable)
class ATCAIRPORTS_API ARadioLogDisplay : public AActor
{
	GENERATED_BODY()

public:
	ARadioLogDisplay();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Radio")
	TObjectPtr<UStaticMeshComponent> Panel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ATC|Radio")
	TObjectPtr<UTextRenderComponent> Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Radio")
	float WidthCm = 60.f;

	/** Characters per line before it wraps; the text size follows from it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Radio")
	int32 LineLength = 44;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Radio")
	int32 MaxLines = 14;

private:
	void Layout();

	UFUNCTION()
	void HandleRadioMessage(const FString& Line);

	TArray<FString> Lines;
};
