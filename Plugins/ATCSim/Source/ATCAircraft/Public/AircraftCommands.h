#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PushbackControllerComponent.h"
#include "AircraftCommands.generated.h"

class ATaxiRoute;
class UAircraftTypeData;

UINTERFACE(BlueprintType, Blueprintable)
class ATCAIRCRAFT_API UAircraftCommands : public UInterface
{
	GENERATED_BODY()
};

/** Commands for an aircraft. UI, scenario and VR input all use this, never the concrete aircraft class. */
class ATCAIRCRAFT_API IAircraftCommands
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ATC|Commands")
	bool RequestPushback(const FPushbackRequest& Request);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ATC|Commands")
	bool StartTaxi(ATaxiRoute* Route);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ATC|Commands")
	bool HoldPosition();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ATC|Commands")
	bool ContinueTaxi();

	/** 0 = no limit. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ATC|Commands")
	void SetSpeedLimit(float Knots);
};

UINTERFACE(BlueprintType, Blueprintable)
class ATCAIRCRAFT_API UAircraftInfo : public UInterface
{
	GENERATED_BODY()
};

/** Read-only information about an aircraft, for UI and logging. */
class ATCAIRCRAFT_API IAircraftInfo
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ATC|Info")
	FName GetCallsign() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ATC|Info")
	UAircraftTypeData* GetAircraftType() const;

	/** "Taxiing 18 kt" */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ATC|Info")
	FText GetStatusText() const;
};
