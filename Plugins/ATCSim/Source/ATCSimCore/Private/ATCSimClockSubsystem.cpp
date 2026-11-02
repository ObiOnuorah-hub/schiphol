#include "ATCSimClockSubsystem.h"
#include "ATCSimSettings.h"

void UATCSimClockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SimTimeUTC = FDateTime::UtcNow();
	bFollowRealTime = GetDefault<UATCSimSettings>()->bRealTimeClock;
}

void UATCSimClockSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bPaused)
	{
		return;
	}
	if (bFollowRealTime)
	{
		// The wall clock itself: adding up frame times drifts away after a hitch or while the editor waits
		SimTimeUTC = FDateTime::UtcNow();
		return;
	}
	SimTimeUTC += FTimespan::FromSeconds(DeltaTime * TimeScale);
}

TStatId UATCSimClockSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UATCSimClockSubsystem, STATGROUP_Tickables);
}

bool UATCSimClockSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UATCSimClockSubsystem::SetSimTimeUTC(FDateTime NewTimeUTC)
{
	bFollowRealTime = false;
	SimTimeUTC = NewTimeUTC;
	OnSimTimeSet.Broadcast(SimTimeUTC);
}

void UATCSimClockSubsystem::SetTimeScale(float NewTimeScale)
{
	TimeScale = FMath::Max(NewTimeScale, 0.f);
	if (!FMath::IsNearlyEqual(TimeScale, 1.f))
	{
		bFollowRealTime = false;
	}
}

void UATCSimClockSubsystem::SetPaused(bool bNewPaused)
{
	bPaused = bNewPaused;
	if (bPaused)
	{
		bFollowRealTime = false;
	}
}

void UATCSimClockSubsystem::FollowRealTime()
{
	TimeScale = 1.f;
	bPaused = false;
	bFollowRealTime = true;
	SimTimeUTC = FDateTime::UtcNow();
	OnSimTimeSet.Broadcast(SimTimeUTC);
}

FString UATCSimClockSubsystem::GetLocalTimeString() const
{
	// Offset of this computer's time zone right now (summer or winter time)
	const FTimespan Offset = FDateTime::Now() - FDateTime::UtcNow();
	const int64 Minutes = FMath::RoundToInt64(Offset.GetTotalMinutes());
	return (SimTimeUTC + FTimespan::FromMinutes(static_cast<double>(Minutes))).ToString(TEXT("%H:%M:%S LT"));
}
