#include "ATCSoundComponent.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"
#include "TimerManager.h"
#include <atomic>

/** The values between the game thread (writes) and the audio thread (reads). */
struct FATCSoundShared
{
	std::atomic<float> NoiseLevel{0.f};
	std::atomic<float> NoiseCutoffHz{400.f};
	std::atomic<float> Gusts{0.f};
	std::atomic<float> ToneHz{800.f};
	std::atomic<float> ToneLevel{0.f};
	std::atomic<float> GlideSeconds{2.f};

	void Set(const FATCSoundParams& Params)
	{
		NoiseLevel.store(Params.NoiseLevel);
		NoiseCutoffHz.store(Params.NoiseCutoffHz);
		Gusts.store(Params.Gusts);
		ToneHz.store(Params.ToneHz);
		ToneLevel.store(Params.ToneLevel);
		GlideSeconds.store(Params.GlideSeconds);
	}
};

/** Renders on the audio thread: white noise through two low-pass stages plus a whine with its octave, all gliding. */
class FATCNoiseToneGenerator : public ISoundGenerator
{
public:
	FATCNoiseToneGenerator(TSharedPtr<FATCSoundShared, ESPMode::ThreadSafe> InShared, float InSampleRate, int32 InChannels)
		: Shared(MoveTemp(InShared)), SampleRate(FMath::Max(InSampleRate, 8000.f)), Channels(FMath::Max(InChannels, 1)),
		Random(static_cast<int32>(FPlatformTime::Cycles()))
	{
	}

	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
	{
		const int32 Frames = NumSamples / Channels;
		const float BlockSeconds = Frames / SampleRate;

		// The values glide once per block (a few ms): no clicks, and a jet that spools up
		const float Glide = 1.f - FMath::Exp(-BlockSeconds / FMath::Max(Shared->GlideSeconds.load(), 0.05f));
		Noise += (Shared->NoiseLevel.load() - Noise) * Glide;
		Cutoff += (Shared->NoiseCutoffHz.load() - Cutoff) * Glide;
		Gust += (Shared->Gusts.load() - Gust) * Glide;
		ToneFrequency += (Shared->ToneHz.load() - ToneFrequency) * Glide;
		Tone += (Shared->ToneLevel.load() - Tone) * Glide;

		// Gusts: a new random strength every few seconds, reached in about 1.5 s
		GustTimer -= BlockSeconds;
		if (GustTimer <= 0.f)
		{
			GustTimer = 1.5f + 3.f * Random.FRand();
			GustTarget = Random.FRand();
		}
		GustValue += (GustTarget - GustValue) * (1.f - FMath::Exp(-BlockSeconds / 1.5f));
		const float NoiseGain = Noise * (1.f - Gust + Gust * (0.3f + 1.4f * GustValue));

		const float Hz = FMath::Clamp(Cutoff, 20.f, SampleRate * 0.45f);
		const float A = 1.f - FMath::Exp(-2.f * PI * Hz / SampleRate);
		// The low-pass takes level away from the noise, the more the lower it is: make that up
		const float Makeup = FMath::Min(0.5f / FMath::Sqrt(PI * Hz / SampleRate), 20.f);
		const float PhaseStep = 2.f * PI * ToneFrequency / SampleRate;
		const float WobbleStep = 2.f * PI * 0.7f / SampleRate;

		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			const float White = 2.f * Random.FRand() - 1.f;
			Low1 += A * (White - Low1);
			Low2 += A * (Low1 - Low2);
			// The whine wavers a little, like a real fan
			Phase += PhaseStep * (1.f + 0.004f * FMath::Sin(Wobble));
			Wobble += WobbleStep;
			Phase = Phase > 2.f * PI ? Phase - 2.f * PI : Phase;
			Wobble = Wobble > 2.f * PI ? Wobble - 2.f * PI : Wobble;
			const float Whine = FMath::Sin(Phase) + 0.35f * FMath::Sin(2.f * Phase);
			const float Sample = FMath::Clamp(NoiseGain * Low2 * Makeup + 0.5f * Tone * Whine, -1.f, 1.f);
			for (int32 Channel = 0; Channel < Channels; ++Channel)
			{
				OutAudio[Frame * Channels + Channel] = Sample;
			}
		}
		return Frames * Channels;
	}

private:
	TSharedPtr<FATCSoundShared, ESPMode::ThreadSafe> Shared;
	float SampleRate;
	int32 Channels;
	FRandomStream Random;
	float Noise = 0.f, Cutoff = 400.f, Gust = 0.f, ToneFrequency = 800.f, Tone = 0.f;
	float GustTimer = 0.f, GustTarget = 0.5f, GustValue = 0.5f;
	float Low1 = 0.f, Low2 = 0.f, Phase = 0.f, Wobble = 0.f;
};

UATCSoundComponent::UATCSoundComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 1;
	bAutoActivate = false;
	bStopWhenOwnerDestroyed = true;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	FSoundAttenuationSettings& Attenuation = AttenuationOverrides;
	Attenuation.bAttenuate = true;
	Attenuation.bSpatialize = true;
	Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation.AttenuationShapeExtents = FVector(5000.f, 0.f, 0.f);   // full level within 50 m
	Attenuation.FalloffDistance = 400000.f;                            // quiet 4 km further
	Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Attenuation.dBAttenuationAtMax = -60.f;
	// Far away only the low end is left (and the tower glass takes the rest)
	Attenuation.bAttenuateWithLPF = true;
	Attenuation.LPFRadiusMin = 20000.f;
	Attenuation.LPFRadiusMax = 300000.f;
	Attenuation.LPFFrequencyAtMin = 20000.f;
	Attenuation.LPFFrequencyAtMax = 700.f;
	Shared = MakeShared<FATCSoundShared, ESPMode::ThreadSafe>();
}

FATCSoundParams UATCSoundComponent::EngineParams(float InThrust)
{
	const float T = FMath::Clamp(InThrust, 0.f, 1.f);
	const bool bOn = T > 0.01f;
	FATCSoundParams Params;
	Params.NoiseLevel = bOn ? 0.15f + 0.85f * T : 0.f;   // idle hum to takeoff roar
	Params.NoiseCutoffHz = 250.f + 2200.f * T;
	Params.ToneHz = 900.f + 2600.f * T;                  // the fan whine climbs with the power
	Params.ToneLevel = bOn ? 0.05f + 0.05f * T : 0.f;
	Params.GlideSeconds = 3.f;
	return Params;
}

void UATCSoundComponent::SetThrust(float InThrust)
{
	Thrust = FMath::Clamp(InThrust, 0.f, 1.f);
	SetParams(EngineParams(Thrust));
}

void UATCSoundComponent::MakeAmbient()
{
	bAllowSpatialization = false;
	bOverrideAttenuation = true;
	AttenuationOverrides.bAttenuate = false;
	AttenuationOverrides.bSpatialize = false;
	AttenuationOverrides.bAttenuateWithLPF = false;
}

void UATCSoundComponent::SetParams(const FATCSoundParams& Params)
{
	Current = Params;
	Shared->Set(Params);
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	FTimerManager& Timers = World->GetTimerManager();
	if (!Params.IsSilent())
	{
		Timers.ClearTimer(StopTimer);
		if (!IsPlaying())
		{
			Start();
		}
	}
	// Silent: it glides down first, then the voice is freed
	else if (IsPlaying() && !Timers.IsTimerActive(StopTimer))
	{
		Timers.SetTimer(StopTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (Current.IsSilent())
			{
				Stop();
			}
		}), Params.GlideSeconds * 3.f + 1.f, false);
	}
}

ISoundGeneratorPtr UATCSoundComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<FATCNoiseToneGenerator, ESPMode::ThreadSafe>(Shared, InParams.SampleRate, InParams.NumChannels);
}
