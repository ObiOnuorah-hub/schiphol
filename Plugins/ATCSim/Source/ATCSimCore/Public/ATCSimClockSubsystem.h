#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ATCSimClockSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimTimeSet, FDateTime, NewTimeUTC);

/**
 * Simulation clock in UTC (aviation works in Zulu time). Lives in Core so every building block can read the
 * scenario time, for example the time stamp on a camera monitor, without depending on the day/night module.
 *
 * With real time on (the default, UATCSimSettings::bRealTimeClock) the clock is the computer's own UTC time: day and
 * night in the tower are day and night outside, and the clock never drifts on hitches or a paused editor.
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

	/** Jump to a time (UTC). Day/night recalculates the sun immediately. Leaves real time. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Clock")
	void SetSimTimeUTC(FDateTime NewTimeUTC);

	UFUNCTION(BlueprintPure, Category = "ATC|Clock")
	FDateTime GetSimTimeUTC() const { return SimTimeUTC; }

	/** 1 = real time, 60 = one simulated minute per second. Only the clock runs faster, traffic does not. Not 1 leaves real time. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Clock")
	void SetTimeScale(float NewTimeScale);

	UFUNCTION(BlueprintPure, Category = "ATC|Clock")
	float GetTimeScale() const { return TimeScale; }

	UFUNCTION(BlueprintCallable, Category = "ATC|Clock")
	void SetPaused(bool bNewPaused);

	/** Back to the real time of this computer (UTC), at speed 1. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Clock")
	void FollowRealTime();

	/** True while the clock is the real UTC time. */
	UFUNCTION(BlueprintPure, Category = "ATC|Clock")
	bool IsRealTime() const { return bFollowRealTime; }

	/** "06:31:05Z" */
	UFUNCTION(BlueprintPure, Category = "ATC|Clock")
	FString GetZuluTimeString() const { return SimTimeUTC.ToString(TEXT("%H:%M:%SZ")); }

	/**
	 * The simulation time in the local time of this computer ("07:31:05 LT"). For Schiphol on a Dutch computer that is
	 * the time on the wall, summer time included.
	 */
	UFUNCTION(BlueprintPure, Category = "ATC|Clock")
	FString GetLocalTimeString() const;

	UPROPERTY(BlueprintAssignable, Category = "ATC|Clock")
	FOnSimTimeSet OnSimTimeSet;

private:
	FDateTime SimTimeUTC;
	float TimeScale = 1.f;
	bool bPaused = false;
	bool bFollowRealTime = false;
};
