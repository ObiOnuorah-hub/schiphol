#include "Modules/ModuleManager.h"
#include "ATCUnits.h"
#include "ATCSimSettings.h"
#include "GameplayTagsManager.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY(LogATC);

UATCSimSettings::UATCSimSettings()
{
	// Lumen, SSGI and volumetric fog are too heavy for 90 Hz: see section J of the design document
	VRProfile.Add(TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("0"));
	VRProfile.Add(TEXT("r.ReflectionMethod"), TEXT("0"));
	VRProfile.Add(TEXT("r.SSGI.Enable"), TEXT("0"));
	VRProfile.Add(TEXT("r.VolumetricFog"), TEXT("0"));
	VRProfile.Add(TEXT("sg.ShadowQuality"), TEXT("1"));
	// TSR costs 8.8 ms in stereo at 3440x1440 on a GTX 1080 Ti, TAA 0.5 ms; SSAO another 1.7 ms (measured 02-11-2026)
	VRProfile.Add(TEXT("r.AntiAliasingMethod"), TEXT("2"));
	VRProfile.Add(TEXT("r.AmbientOcclusionLevels"), TEXT("0"));
	// Volumetric clouds cost ~20 ms in stereo at 3440x1440 on a GTX 1080 Ti; these settings take 15 ms off and keep the clouds
	// (measured 13-10-2026, see section N of the design document); 64 instead of 128 samples: 4.7 -> 3.5 ms (02-11-2026).
	// Mode 3 (meant for clouds in the sky only) costs 13 ms in stereo: keep 2.
	VRProfile.Add(TEXT("r.VolumetricRenderTarget.Mode"), TEXT("2"));
	// The clouds are traced once for each eye: in plain Play with traffic they were 6.3 of the 15.4 ms of a stereo frame
	// (GPU profile, 11-11-2026). Half the cloud resolution: 2.3 ms; 32 instead of 64 samples: 1.9 ms; 16 shadow samples
	// towards the sun: 1.0 ms. The sky looks the same from the tower, a little softer at the cloud edges
	VRProfile.Add(TEXT("r.VolumetricRenderTarget.Scale"), TEXT("0.5"));
	VRProfile.Add(TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"), TEXT("32"));
	VRProfile.Add(TEXT("r.VolumetricCloud.Shadow.ViewRaySampleMaxCount"), TEXT("16"));
	VRProfile.Add(TEXT("r.VolumetricCloud.SkyAO"), TEXT("0"));
	VRProfile.Add(TEXT("r.VolumetricCloud.ShadowMap"), TEXT("0"));
	// Every camera picture renders the scene again: in the headset the two live cameras run at 15 Hz instead of 20
	VRProfile.Add(TEXT("atc.CameraRateScale"), TEXT("0.75"));
	// 80 % resolution: 16.0 -> 13.5 ms per frame in stereo at 3440x1440 (p99 17.9 -> 15.1 ms), still sharp with TAA. Simpler
	// shadows (CSM) saved 0.9 ms but lose the sun's shadows on the cab's desks; cheaper VSM only 0.2 ms (measured 10-11-2026)
	VRProfile.Add(TEXT("r.ScreenPercentage"), TEXT("80"));

	ValidatedPaths = { TEXT("/Game/Aircraft"), TEXT("/Game/Airports"), TEXT("/Game/Environment"), TEXT("/Game/Camera"), TEXT("/Game/Scenarios"), TEXT("/ATCSim") };
	AllowedDependencyRoots = { TEXT("/Game/"), TEXT("/ATCSim/"), TEXT("/Script/"), TEXT("/Engine/"), TEXT("/Niagara/") };
}

class FATCSimCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Stands, routes, towers and camera presets are tags in the plugin's Config/Tags: the plugin brings them along
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ATCSim")))
		{
			UGameplayTagsManager::Get().AddTagIniSearchPath(Plugin->GetBaseDir() / TEXT("Config") / TEXT("Tags"));
		}
	}
};

IMPLEMENT_MODULE(FATCSimCoreModule, ATCSimCore);
