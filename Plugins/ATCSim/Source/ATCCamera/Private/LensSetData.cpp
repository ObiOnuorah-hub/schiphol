#include "LensSetData.h"

float ULensLibrary::FocalLengthToFOV(float FocalLengthMm, float SensorWidthMm)
{
	return FMath::RadiansToDegrees(2.f * FMath::Atan(SensorWidthMm / (2.f * FMath::Max(FocalLengthMm, 0.1f))));
}

float ULensLibrary::FOVToFocalLength(float FOVDeg, float SensorWidthMm)
{
	return SensorWidthMm / (2.f * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FOVDeg, 0.1f, 179.f)) * 0.5f));
}

float ULensLibrary::InterpolateFocalLength(float FromMm, float ToMm, float Alpha)
{
	return FMath::Exp(FMath::Lerp(FMath::Loge(FMath::Max(FromMm, 0.1f)), FMath::Loge(FMath::Max(ToMm, 0.1f)), Alpha));
}
