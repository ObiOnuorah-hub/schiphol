#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ATCSimClockSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimTimeSet, FDateTime, NewTimeUTC);

/**
 * Simulation clock in UTC (aviation works in Zulu time). Lives in Core so every building block can read the
 * scenario time, for example the time stamp on a camera monitor, without depending on the day/night module.
 */
UCLASS()
class ATCSIMCORE_API UATCSimClockSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Jump to a time (UTC). Day/night recalculates the sun immediately. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Clock")
	void SetSimTimeUTC(FDateTime NewTimeUTC);

	UFUNCTION(BlueprintPure, Category = "ATC|Clock")
	FDateTime GetSimTimeUTC() const { return SimTimeUTC; }

	/** 1 = real time, 60 = one simulated minute per second. Only the clock runs faster, traffic does not. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Clock")
	void SetTimeScale(float NewTimeScale) { TimeScale = FMath::Max(NewTimeScale, 0.f); }

	UFUNCTION(BlueprintPure, Category = "ATC|Clock")
	float GetTimeScale() const { return TimeScale; }

	UFUNCTION(BlueprintCallable, Category = "ATC|Clock")
	void SetPaused(bool bNewPaused) { bPaused = bNewPaused; }

	/** "06:31:05Z" */
	UFUNCTION(BlueprintPure, Category = "ATC|Clock")
	FString GetZuluTimeString() const { return SimTimeUTC.ToString(TEXT("%H:%M:%SZ")); }

	UPROPERTY(BlueprintAssignable, Category = "ATC|Clock")
	FOnSimTimeSet OnSimTimeSet;

private:
	FDateTime SimTimeUTC;
	float TimeScale = 1.f;
	bool bPaused = false;
};
