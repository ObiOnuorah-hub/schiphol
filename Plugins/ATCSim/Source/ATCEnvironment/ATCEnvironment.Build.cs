using UnrealBuildTool;

public class ATCEnvironment : ModuleRules
{
	public ATCEnvironment(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "ATCSimCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "SunPosition", "Niagara", "HTTP" });
	}
}
