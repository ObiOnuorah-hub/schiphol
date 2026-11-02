#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "AirportSubsystem.generated.h"

class UScenarioData;
class UAirportData;
struct FActorsInitializedParams;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnScenarioStarted, UScenarioData*, Scenario);

/**
 * Starts scenarios. Lives in the game instance because opening an airport map replaces the world:
 * the scenario waits here until the new map is loaded, then configures clock, sun, weather, tower view and traffic.
 */
UCLASS()
class ATCAIRPORTS_API UAirportSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Opens the airport map if needed, then applies the scenario. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Scenario")
	void StartScenario(UScenarioData* Scenario);

	/** Applies a scenario to an already loaded world (used by AScenarioStarter). */
	UFUNCTION(BlueprintCallable, Category = "ATC|Scenario")
	void ApplyScenario(UWorld* World, UScenarioData* Scenario);

	UFUNCTION(BlueprintPure, Category = "ATC|Scenario")
	UScenarioData* GetActiveScenario() const { return ActiveScenario; }

	UFUNCTION(BlueprintPure, Category = "ATC|Scenario")
	UAirportData* GetActiveAirport() const { return ActiveAirport; }

	/** Moves the player to a tower position of the active airport, with a short fade. */
	UFUNCTION(BlueprintCallable, Category = "ATC|Scenario")
	bool GoToTower(FGameplayTag TowerTag);

	UPROPERTY(BlueprintAssignable, Category = "ATC|Scenario")
	FOnScenarioStarted OnScenarioStarted;

private:
	void HandleMapLoaded(UWorld* World);
	void HandleActorsInitialized(const FActorsInitializedParams& Params);
	void ApplyDeferred(TWeakObjectPtr<UWorld> World, int32 Attempt);

	/** Plain Play without a scenario: the player goes to the airport's PlayTower. */
	void ApplyPlayTower(TWeakObjectPtr<UWorld> World, int32 Attempt);
	void ScheduleSpawns(UWorld* World);
	void ApplyRenderProfile();

	UPROPERTY(Transient)
	TObjectPtr<UScenarioData> PendingScenario;

	UPROPERTY(Transient)
	TObjectPtr<UScenarioData> ActiveScenario;

	UPROPERTY(Transient)
	TObjectPtr<UAirportData> ActiveAirport;

	TWeakObjectPtr<UWorld> AppliedWorld;
	FDelegateHandle MapLoadedHandle;
	FDelegateHandle ActorsInitializedHandle;

	friend class AScenarioStarter;
};

/** Place in a map to start a scenario on Play, also when the map is opened directly in the editor. */
UCLASS()
class ATCAIRPORTS_API AScenarioStarter : public AActor
{
	GENERATED_BODY()

public:
	AScenarioStarter();

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Scenario")
	TObjectPtr<UScenarioData> Scenario;
};
