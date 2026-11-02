using UnrealBuildTool;

public class ATCCamera : ModuleRules
{
	public ATCCamera(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GameplayTags", "ATCSimCore", "DeveloperSettings", "EnhancedInput", "InputCore", "UMG" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "RHI", "RenderCore" });
	}
}
