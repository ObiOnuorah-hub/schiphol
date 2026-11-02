#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Stand.generated.h"

class ATaxiRoute;
class UArrowComponent;
class AStand;

/** One way to push back from a stand, named the way the controller says it ("push back approved, face north"). */
USTRUCT(BlueprintType)
struct FPushbackOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stand")
	FGameplayTag OptionTag;

	/** Spline from the parking point (main gear) to the end of the pushback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stand")
	TObjectPtr<ATaxiRoute> Route;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stand")
	FText Phraseology;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStandVacated, AStand*, Stand);

/** A gate or parking position. The arrow shows where the nose points; its origin is the main gear. */
UCLASS(Blueprintable)
class ATCAIRCRAFT_API AStand : public AActor
{
	GENERATED_BODY()

public:
	AStand();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stand")
	TObjectPtr<USceneComponent> ParkingPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stand")
	TObjectPtr<UArrowComponent> NoseDirection;

	/** For example Stand.EHAM.D7. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stand")
	FGameplayTag StandTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stand")
	TArray<FPushbackOption> PushbackOptions;

	UFUNCTION(BlueprintPure, Category = "Stand")
	ATaxiRoute* FindPushbackRoute(FGameplayTag OptionTag) const;

	UFUNCTION(BlueprintPure, Category = "Stand")
	FTransform GetParkingTransform() const { return ParkingPoint->GetComponentTransform(); }

	UFUNCTION(BlueprintPure, Category = "Stand")
	AActor* GetOccupant() const { return Occupant.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Stand")
	void SetOccupant(AActor* NewOccupant) { Occupant = NewOccupant; }

	UFUNCTION(BlueprintCallable, Category = "Stand")
	void Vacate();

	UPROPERTY(BlueprintAssignable, Category = "Stand")
	FOnStandVacated OnStandVacated;

private:
	TWeakObjectPtr<AActor> Occupant;
};
