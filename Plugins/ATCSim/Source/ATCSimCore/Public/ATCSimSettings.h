#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ATCSimSettings.generated.h"

/** Project-wide defaults for the ATC building blocks: Project Settings > Plugins > ATC Sim. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ATC Sim"))
class ATCSIMCORE_API UATCSimSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UATCSimSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Console variables applied when a scenario starts with a VR headset active. Lighter rendering for 90 Hz. */
	UPROPERTY(Config, EditAnywhere, Category = "Render profiles")
	TMap<FString, FString> VRProfile;

	/** Console variables applied when a scenario starts without a headset (instructor screen). Empty = keep the project settings. */
	UPROPERTY(Config, EditAnywhere, Category = "Render profiles")
	TMap<FString, FString> DesktopProfile;

	/** Safety distance: taxiing aircraft stop this far behind the aircraft ahead (nose to tail, along the path). */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float TaxiSeparationM = 60.f;

	/** How far past its braking distance a taxiing aircraft looks for traffic and reserves its route. */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float TrafficLookAheadM = 150.f;

	/** Spawning waits until no other aircraft is this close to the new one (along its heading). */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float SpawnClearanceM = 80.f;

	/** Room that must stay between the wingtips of two aircraft on crossing or neighbouring taxiways. */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float WingtipClearanceM = 7.5f;

	/**
	 * Two parallel taxiways for the same trip (a route with Alternatives): with equal traffic each aircraft takes the
	 * right-hand one, so the two directions each keep to their own taxiway. Opposing traffic on a lane always counts first.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic")
	bool bKeepRightOnParallelTaxiways = true;

	/**
	 * In the queues a landing counts as having waited this long already: it goes before everybody on the ground who has
	 * waited less. Whoever has waited longer goes first and keeps the landing in the stack, so a stream of landings
	 * never keeps a departure waiting for good.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "s"))
	float LandingHeadStartSeconds = 120.f;

	/**
	 * A landing holds in the stack until its runway exit can be cleared (nobody on it, nobody waiting for it longer),
	 * at most this long. Then it comes anyway, and goes around when the exit is still taken 1.5 km before the runway.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "s"))
	float MaxHoldingSeconds = 300.f;

	/** Landings on the same runway come out of the stack at least this far apart. */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "s"))
	float LandingSpacingSeconds = 75.f;

	/** Asset paths that must not depend on content outside the project or this plugin (see the reference validator). */
	UPROPERTY(Config, EditAnywhere, Category = "Validation")
	TArray<FString> ValidatedPaths;

	/** Content roots that are always allowed as dependencies. */
	UPROPERTY(Config, EditAnywhere, Category = "Validation")
	TArray<FString> AllowedDependencyRoots;
};
