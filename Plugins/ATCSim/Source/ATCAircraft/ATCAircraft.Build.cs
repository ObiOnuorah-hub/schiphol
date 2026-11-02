using UnrealBuildTool;

public class ATCAircraft : ModuleRules
{
	public ATCAircraft(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GameplayTags", "ATCSimCore" });
	}
}
