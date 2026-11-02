#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "RadioSubsystem.generated.h"

class AATCAircraft;
class UAudioComponent;
class URadioVoiceData;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadioMessage, const FString&, Line);

/** One transmission on the frequency: who speaks, the fragments and the written line. */
USTRUCT()
struct FRadioTransmission
{
	GENERATED_BODY()

	bool bPilot = true;
	TArray<FName> Tokens;
	FString Text;
	/** Pause before it (the other party thinking, or keying the radio). */
	float PauseBefore = 0.8f;
};

/**
 * The tower frequency: every aircraft event becomes the radio exchange a controller would hear, in ICAO phraseology
 * (pushback, taxi clearance, holding point, takeoff clearance with the wind, runway crossings, arrivals vacating),
 * with readbacks. One transmission at a time, like a real frequency; each one is also a written line
 * (OnRadioMessage, the radio log screen in the tower and the output log).
 */
UCLASS()
class ATCAIRPORTS_API URadioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Queue one transmission; tokens that are not recorded are left out of the sound (the text is complete). */
	void Say(bool bPilot, const TArray<FName>& Tokens, const FString& Text, float PauseBefore = 0.8f);

	UFUNCTION(BlueprintPure, Category = "ATC|Radio")
	const TArray<FString>& GetRecentLines() const { return Recent; }

	UFUNCTION(BlueprintCallable, Category = "ATC|Radio")
	void SetEnabled(bool bEnable);

	UFUNCTION(BlueprintPure, Category = "ATC|Radio")
	bool IsEnabled() const { return bEnabled; }

	UPROPERTY(BlueprintAssignable, Category = "ATC|Radio")
	FOnRadioMessage OnRadioMessage;

	/** Fragments for a callsign: KLM1427 -> klm d1 d4 d2 d7; unknown airlines are spelled. Also used for the text. */
	TArray<FName> SpellCallsign(FName Callsign, FString& OutText) const;

	/** 36L -> d3 d6 left, "three six left". */
	static TArray<FName> SpellRunway(const FString& Runway, FString& OutText);

	/** G7 -> lg d7. */
	static TArray<FName> SpellCode(const FString& Code, FString& OutText);

private:
	UFUNCTION()
	void HandleAircraftEvent(AActor* Aircraft, FName Callsign, FGameplayTag EventTag, const FString& Detail);

	void PlayNext();
	void PlayFragment();
	void SayArrival(FName Callsign);
	FString DepartureRunway(const AATCAircraft* Aircraft) const;

	UPROPERTY(Transient)
	TObjectPtr<URadioVoiceData> Voice;

	UPROPERTY(Transient)
	TArray<FRadioTransmission> Queue;

	/** The fragments of the transmission that is on the air. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> OnAir;

	int32 OnAirIndex = 0;
	bool bBusy = false;
	bool bEnabled = true;
	TArray<FString> Recent;

	/** Callsign -> the stop it is waiting at for a clearance ("Holding point 36L"). */
	TMap<FName, FString> WaitingAt;

	FTimerHandle PauseTimer;
	FRandomStream Random = FRandomStream(1180);
};
