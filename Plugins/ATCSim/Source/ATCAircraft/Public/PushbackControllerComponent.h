#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "PushbackControllerComponent.generated.h"

class AStand;
class ATaxiRoute;
class UAircraftMovementComponent;
class UAircraftTypeData;

UENUM(BlueprintType)
enum class EPushbackState : uint8
{
	Parked,
	/** Waiting for approval (only when the request asks for it). */
	Requested,
	TugConnecting,
	Pushing,
	/** Braking for the end of the pushback route. */
	Stopping,
	TugDisconnecting,
	Completed
};

/** What the controller asks for: which way to push and what to do after. */
USTRUCT(BlueprintType)
struct ATCAIRCRAFT_API FPushbackRequest
{
	GENERATED_BODY()

	/** Pushback option of the stand, for example Pushback.FaceNorth. Empty = first option. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pushback")
	FGameplayTag OptionTag;

	/** Route to start taxiing on when the tug is disconnected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pushback")
	TObjectPtr<ATaxiRoute> TaxiRouteAfter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pushback")
	bool bAutoTaxi = true;

	/** Wait in Requested until ApprovePushback is called. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pushback")
	bool bRequiresApproval = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPushbackStateChanged, EPushbackState, OldState, EPushbackState, NewState);

/**
 * Pushes the aircraft backwards from its stand over the stand's pushback route, then hands over to the taxi
 * controller. Uses the same movement component as taxiing; the nose points against the travel direction.
 * The push only starts when the taxi network has reserved the whole pushback path; until then the tug waits.
 */
UCLASS(ClassGroup = (ATC), meta = (BlueprintSpawnableComponent))
class ATCAIRCRAFT_API UPushbackControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPushbackControllerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "ATC|Pushback")
	bool RequestPushback(const FPushbackRequest& Request, AStand* FromStand);

	UFUNCTION(BlueprintCallable, Category = "ATC|Pushback")
	void ApprovePushback(bool bApproved);

	UFUNCTION(BlueprintPure, Category = "ATC|Pushback")
	EPushbackState GetState() const { return State; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Pushback")
	TObjectPtr<const UAircraftTypeData> AircraftType;

	/** Realistic is 20-60 s; shorter for demos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Pushback")
	float TugConnectTime = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Pushback")
	float TugDisconnectTime = 8.f;

	/**
	 * When the push starts, nobody may come the other way on the two-way stretches of the taxi route this far after it
	 * (whole stretches): a pier taxilane is the departure's until it has passed, so it never meets an arrival head-on there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Pushback", meta = (Units = "m"))
	float ReserveAheadM = 700.f;

	/**
	 * Tug attached to the nose gear while pushing. Default: APushbackTug, which drives off to the side once it has
	 * disconnected and goes back to the stand. Another actor class (a tug model of the project) is removed at the end
	 * like before; empty = no tug.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATC|Pushback")
	TSubclassOf<AActor> TugClass;

	UPROPERTY(BlueprintAssignable, Category = "ATC|Pushback")
	FOnPushbackStateChanged OnPushbackStateChanged;

private:
	void SetState(EPushbackState NewState);
	void StartTugConnect();
	void StartPushing();
	void Finish();
	/** The tug has disconnected: a vehicle tug drives away to the side before the aircraft taxis. */
	void ReleaseTug();
	void DestroyTug();

	UFUNCTION()
	void HandlePathEnd();

	float GetPushSpeedCms() const;

	UPROPERTY(Transient)
	TObjectPtr<UAircraftMovementComponent> Movement;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Tug;

	EPushbackState State = EPushbackState::Parked;
	FPushbackRequest ActiveRequest;
	TWeakObjectPtr<AStand> Stand;
	TWeakObjectPtr<ATaxiRoute> PushRoute;
	/** Holds the pushback path, logged once per aircraft it waits for. */
	TWeakObjectPtr<AActor> WaitingFor;
	FTimerHandle Timer;
	FTimerHandle TugTimer;
};
