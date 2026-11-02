#include "EnvironmentTypes.h"

FWeatherState FWeatherState::Lerp(const FWeatherState& A, const FWeatherState& B, float Alpha)
{
	FWeatherState Out;
	Out.CloudCoverage = FMath::Lerp(A.CloudCoverage, B.CloudCoverage, Alpha);
	Out.CloudBaseFt = FMath::Lerp(A.CloudBaseFt, B.CloudBaseFt, Alpha);
	// Visibility changes exponentially in the eye: blend in log space so fog comes in evenly
	Out.VisibilityM = FMath::Exp(FMath::Lerp(FMath::Loge(FMath::Max(A.VisibilityM, 1.f)), FMath::Loge(FMath::Max(B.VisibilityM, 1.f)), Alpha));
	Out.Wetness = FMath::Lerp(A.Wetness, B.Wetness, Alpha);
	Out.SnowCover = FMath::Lerp(A.SnowCover, B.SnowCover, Alpha);
	Out.SunIntensityScale = FMath::Lerp(A.SunIntensityScale, B.SunIntensityScale, Alpha);
	Out.SunSourceAngle = FMath::Lerp(A.SunSourceAngle, B.SunSourceAngle, Alpha);
	Out.WindDirectionDeg = A.WindDirectionDeg + FMath::FindDeltaAngleDegrees(A.WindDirectionDeg, B.WindDirectionDeg) * Alpha;
	Out.WindSpeedKt = FMath::Lerp(A.WindSpeedKt, B.WindSpeedKt, Alpha);

	// Precipitation fades out first, then the new type fades in
	if (A.Precipitation == B.Precipitation)
	{
		Out.Precipitation = A.Precipitation;
		Out.PrecipitationRate = FMath::Lerp(A.PrecipitationRate, B.PrecipitationRate, Alpha);
	}
	else if (Alpha < 0.5f)
	{
		Out.Precipitation = A.Precipitation;
		Out.PrecipitationRate = A.PrecipitationRate * (1.f - Alpha * 2.f);
	}
	else
	{
		Out.Precipitation = B.Precipitation;
		Out.PrecipitationRate = B.PrecipitationRate * (Alpha * 2.f - 1.f);
	}
	return Out;
}
