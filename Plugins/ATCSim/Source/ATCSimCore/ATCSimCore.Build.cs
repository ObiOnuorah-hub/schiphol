using UnrealBuildTool;

public class ATCSimCore : ModuleRules
{
	public ATCSimCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GameplayTags", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Projects" });
	}
}
