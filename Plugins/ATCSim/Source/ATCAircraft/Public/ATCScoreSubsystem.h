#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "ATCScoreSubsystem.generated.h"

/** What a session brought so far, and the score (0-100) that follows from it. */
USTRUCT(BlueprintType)
struct ATCAIRCRAFT_API FATCSessionScore
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ATC|Score")
	int32 Landings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ATC|Score")
	int32 Takeoffs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ATC|Score")
	int32 Pushbacks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ATC|Score")
	int32 GoArounds = 0;

	/** Two aircraft on the ground that touched (collision monitor). */
	UPROPERTY(BlueprintReadOnly, Category = "ATC|Score")
	int32 Conflicts = 0;

	/** Aircraft that waited for each other until one backed off. */
	UPROPERTY(BlueprintReadOnly, Category = "ATC|Score")
	int32 Deadlocks = 0;

	/** Waits at a stop bar (holding point or runway crossing) until the clearance came. */
	UPROPERTY(BlueprintReadOnly, Category = "ATC|Score")
	int32 StopBarWaits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ATC|Score", meta = (Units = "s"))
	float StopBarWaitSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ATC|Score", meta = (Units = "s"))
	float LongestWaitSeconds = 0.f;

	/** Simulation minutes since the start. */
	UPROPERTY(BlueprintReadOnly, Category = "ATC|Score")
	float Minutes = 0.f;

	float AverageWaitSeconds() const { return StopBarWaits > 0 ? StopBarWaitSeconds / StopBarWaits : 0.f; }

	/** 100, minus 10 per go-around, 20 per conflict, 5 per deadlock and 1 per 5 s that the average stop-bar wait is over 30 s. */
	int32 GetScore() const;

	/** One short line for the tower screen. */
	FString ToLine() const;

	/** The whole report, a line per item. */
	FString ToReport() const;
};

/**
 * Keeps the score of a session: landings, takeoffs, go-arounds, conflicts, deadlocks and how long aircraft wait at a
 * stop bar for their clearance (the trainee's, or the simulated colleague's). Shown on the tower's radio screen, by
 * ATC.Score, and saved to Saved/ATC/Score_<time>.txt when the session ends.
 */
UCLASS()
class ATCAIRCRAFT_API UATCScoreSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** The score now. */
	UFUNCTION(BlueprintPure, Category = "ATC|Score")
	FATCSessionScore GetScore() const;

	/** From zero again (a scenario starts). */
	UFUNCTION(BlueprintCallable, Category = "ATC|Score")
	void ResetScore();

	/** Writes the report to Saved/ATC/Score_<time>.txt; returns the file. */
	FString SaveReport() const;

	/** One aircraft event; bound to the traffic subsystem (tests call it directly). */
	UFUNCTION()
	void HandleAircraftEvent(AActor* Aircraft, FName Callsign, FGameplayTag EventTag, const FString& Detail);

	/** The score line changed (a few times a minute at most). */
	FSimpleMulticastDelegate OnScoreChanged;

private:
	void Refresh();
	double Now() const;

	FATCSessionScore Counts;
	/** Callsign -> time it stopped at a stop bar for its clearance. */
	TMap<FName, double> WaitingSince;
	double StartTime = 0.0;
	int32 OverlapsAtStart = 0;
	int32 DeadlocksAtStart = 0;
	FString LastLine;
	FTimerHandle RefreshTimer;
};
