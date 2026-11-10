#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "ATCSoundComponent.generated.h"

struct FATCSoundShared;

/** What the generator makes: filtered noise (rumble, roar, wind) plus a tone (turbine whine). Every value glides. */
struct FATCSoundParams
{
	/** Loudness of the noise, 0..1. */
	float NoiseLevel = 0.f;
	/** Low-pass of the noise in Hz: low is a rumble, high a roar. */
	float NoiseCutoffHz = 400.f;
	/** Slow random swells of the noise, 0..1 (gusts of wind). */
	float Gusts = 0.f;
	float ToneHz = 800.f;
	float ToneLevel = 0.f;
	/** Seconds to get most of the way to new values (a jet spools up in a few seconds). */
	float GlideSeconds = 2.f;

	bool IsSilent() const { return NoiseLevel <= 0.f && ToneLevel <= 0.f; }
};

/**
 * Sound made in code, no sound files: noise through a low-pass filter plus a tone, the way a jet engine or the wind
 * sounds from a distance. The audio thread renders it; the game thread only sets the values (shared atomics).
 * Spatialized by default with a natural falloff over 4 km and a low-pass that grows with distance, so far traffic is a
 * dull rumble, the way it sounds through the tower glass.
 */
UCLASS(ClassGroup = (ATC), meta = (BlueprintSpawnableComponent))
class ATCAIRCRAFT_API UATCSoundComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UATCSoundComponent(const FObjectInitializer& ObjectInitializer);

	/** New values; starts the sound when it makes some, stops it a while after it went silent. */
	void SetParams(const FATCSoundParams& Params);

	/** A jet engine: 0 = off, 0.3 = idle (taxi), 1 = takeoff power. */
	void SetThrust(float Thrust);

	/** The same level everywhere, no position (the listener's own room: wind on the cab). Call before it starts. */
	void MakeAmbient();

	float GetThrust() const { return Thrust; }
	const FATCSoundParams& GetParams() const { return Current; }

	/** What SetThrust sets for a thrust (tests). */
	static FATCSoundParams EngineParams(float Thrust);

protected:
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	FATCSoundParams Current;
	float Thrust = 0.f;
	/** Written on the game thread, read on the audio thread; made once and never replaced. */
	TSharedPtr<FATCSoundShared, ESPMode::ThreadSafe> Shared;
	FTimerHandle StopTimer;
};
