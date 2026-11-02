#include "ATCReferenceValidator.h"
#include "ATCSimSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "ATCReferenceValidator"

bool UATCReferenceValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const
{
	const FString Package = InAssetData.PackageName.ToString();
	for (const FString& Root : GetDefault<UATCSimSettings>()->ValidatedPaths)
	{
		if (Package.StartsWith(Root))
		{
			return true;
		}
	}
	return false;
}

EDataValidationResult UATCReferenceValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
	const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FName> Dependencies;
	Registry.GetDependencies(InAssetData.PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);

	const TArray<FString>& Allowed = GetDefault<UATCSimSettings>()->AllowedDependencyRoots;
	bool bFailed = false;
	for (const FName& Dependency : Dependencies)
	{
		const FString Path = Dependency.ToString();
		if (!Allowed.ContainsByPredicate([&Path](const FString& Root) { return Path.StartsWith(Root); }))
		{
			AssetFails(InAsset, FText::Format(LOCTEXT("External", "{0} depends on {1}, which is outside the project and the ATCSim plugin. Copy it into the project first."),
				FText::FromName(InAssetData.PackageName), FText::FromString(Path)));
			bFailed = true;
		}
	}

	if (!bFailed)
	{
		AssetPasses(InAsset);
	}
	return bFailed ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
