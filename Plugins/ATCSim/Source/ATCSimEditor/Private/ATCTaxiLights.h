#pragma once

#include "CoreMinimal.h"

/** Taxiway light mesh and material for ATaxiRoute (/ATCSim/Lights). */
namespace ATCTaxiLights
{
	/** Builds or rebuilds SM_TaxiLight and M_TaxiLight; returns the packages to save. */
	TArray<UPackage*> Create();
}
