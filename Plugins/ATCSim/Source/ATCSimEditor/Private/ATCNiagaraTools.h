#pragma once

#include "CoreMinimal.h"

namespace ATCNiagara
{
	/** Builds NS_Rain and NS_Snow in /ATCSim/Environment from Epic's RecycleParticlesInView template. Returns the packages to save. */
	TArray<UPackage*> CreatePrecipitation();
}
