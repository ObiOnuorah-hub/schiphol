using UnrealBuildTool;

public class ATCAirports : ModuleRules
{
	public ATCAirports(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Top layer: configures the other building blocks when a scenario starts
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GameplayTags", "ATCSimCore", "ATCAircraft", "ATCEnvironment", "ATCCamera" });
		PrivateDependencyModuleNames.AddRange(new string[] { "HeadMountedDisplay", "InputCore" });
	}
}
