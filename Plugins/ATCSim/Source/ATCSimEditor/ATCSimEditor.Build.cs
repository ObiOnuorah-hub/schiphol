using UnrealBuildTool;

public class ATCSimEditor : ModuleRules
{
	public ATCSimEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "AssetTools", "AssetRegistry", "DataValidation", "MaterialEditor", "GameplayTags", "Niagara", "NiagaraEditor", "NiagaraCore",
			"ImageCore", "MeshDescription", "StaticMeshDescription", "RenderCore", "Json", "Projects",
			"ATCSimCore", "ATCAircraft", "ATCCamera", "ATCEnvironment", "ATCAirports", "EnhancedInput", "InputCore"
		});
	}
}
