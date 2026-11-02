#include "TrafficMixData.h"

const FTrafficMixEntry* UTrafficMixData::Pick(FRandomStream& Random) const
{
	float Total = 0.f;
	for (const FTrafficMixEntry& Entry : Entries)
	{
		Total += FMath::Max(Entry.Weight, 0.f);
	}
	if (Total <= 0.f)
	{
		return Entries.Num() > 0 ? &Entries[0] : nullptr;
	}
	float Roll = Random.FRandRange(0.f, Total);
	for (const FTrafficMixEntry& Entry : Entries)
	{
		Roll -= FMath::Max(Entry.Weight, 0.f);
		if (Roll <= 0.f)
		{
			return &Entry;
		}
	}
	return &Entries.Last();
}
