#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "AircraftIdentityComponent.generated.h"

class AStand;

/** Who the aircraft is: callsign, home stand and the route the scenario assigned to it. */
UCLASS(ClassGroup = (ATC), meta = (BlueprintSpawnableComponent))
class ATCAIRCRAFT_API UAircraftIdentityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** For example KLM1023. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Identity")
	FName Callsign;

	/** Route used for auto-taxi after pushback and for Cmd.Taxi without a parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Identity")
	FGameplayTag AssignedTaxiRoute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Identity")
	bool bAutoTaxiAfterPushback = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Identity")
	TObjectPtr<AStand> Stand;

	/** Remove the aircraft this long after it arrives at the end of its route (a departure has taken off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Identity")
	bool bRemoveAtRouteEnd = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Identity", meta = (EditCondition = "bRemoveAtRouteEnd", Units = "s"))
	float RemoveDelaySeconds = 30.f;
};
