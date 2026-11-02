#include "ATCSimClockSubsystem.h"

void UATCSimClockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SimTimeUTC = FDateTime::UtcNow();
}

void UATCSimClockSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bPaused)
	{
		SimTimeUTC += FTimespan::FromSeconds(DeltaTime * TimeScale);
	}
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
	SimTimeUTC = NewTimeUTC;
	OnSimTimeSet.Broadcast(SimTimeUTC);
}
