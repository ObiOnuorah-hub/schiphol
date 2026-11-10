#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ATCAmbienceSubsystem.generated.h"

class UATCSoundComponent;
struct FATCSoundParams;

/**
 * The sound of the tower cab itself, made in code (UATCSoundComponent): wind on the glass that follows the weather's
 * wind speed and gusts, and the low hum of the air conditioning. Plays for the listener without a position.
 */
UCLASS()
class ATCAIRPORTS_API UATCAmbienceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Wind on the glass for a wind speed (tests). */
	static FATCSoundParams WindParams(float WindSpeedKt);

	/** Volume of the whole cab sound, 0..1 (atc.AmbienceVolume). */
	void SetVolume(float Volume);

	UATCSoundComponent* GetWind() const { return Wind; }

private:
	void Refresh();

	UPROPERTY(Transient)
	TObjectPtr<UATCSoundComponent> Wind;

	UPROPERTY(Transient)
	TObjectPtr<UATCSoundComponent> Room;

	FTimerHandle RefreshTimer;
};
