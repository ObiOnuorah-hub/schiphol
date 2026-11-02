#include "GMICameraSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"

UGMICameraSettings::UGMICameraSettings()
{
	// ATC.CreateCameraInput makes these assets in the plugin
	auto Action = [](const TCHAR* Name)
	{
		return TSoftObjectPtr<UInputAction>(FSoftObjectPath(FString::Printf(TEXT("/ATCSim/Input/%s.%s"), Name, Name)));
	};
	InputContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/ATCSim/Input/IMC_GMI.IMC_GMI")));
	PanAction = Action(TEXT("IA_GMI_Pan"));
	TiltAction = Action(TEXT("IA_GMI_Tilt"));
	ZoomAction = Action(TEXT("IA_GMI_Zoom"));
	NextCameraAction = Action(TEXT("IA_GMI_NextCamera"));
	NextPresetAction = Action(TEXT("IA_GMI_NextPreset"));
	PresetAction = Action(TEXT("IA_GMI_Preset"));
	LensAction = Action(TEXT("IA_GMI_Lens"));
}
