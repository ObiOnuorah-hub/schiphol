// ATC.ImportRadioVoice: the radio fragments from Plugins/ATCSim/Data/Radio (made by Tools/MakeRadioVoice.ps1) become
// sound assets in /ATCSim/Radio, and DA_RadioVoice maps every token to its sound for the radio subsystem.

#include "ATCUnits.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RadioVoiceData.h"
#include "Sound/SoundWave.h"

namespace ATCRadioImport
{
	void ImportRadioVoice(const TArray<FString>& Args)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ATCSim"));
		if (!Plugin)
		{
			return;
		}
		const FString Source = Plugin->GetBaseDir() / TEXT("Data") / TEXT("Radio");

		const FString PackageName = TEXT("/ATCSim/Radio/DA_RadioVoice");
		URadioVoiceData* Voice = FPackageName::DoesPackageExist(PackageName) ? LoadObject<URadioVoiceData>(nullptr, *(PackageName + TEXT(".DA_RadioVoice"))) : nullptr;
		if (!Voice)
		{
			Voice = NewObject<URadioVoiceData>(CreatePackage(*PackageName), TEXT("DA_RadioVoice"), RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Voice);
		}
		Voice->Modify();

		TArray<UPackage*> Packages = { Voice->GetPackage() };
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		for (const TCHAR* Speaker : { TEXT("Controller"), TEXT("Pilot") })
		{
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(Source / Speaker / TEXT("*.wav")), true, false);
			TArray<UAssetImportTask*> Tasks;
			for (const FString& File : Files)
			{
				UAssetImportTask* Task = NewObject<UAssetImportTask>();
				Task->Filename = Source / Speaker / File;
				Task->DestinationPath = FString(TEXT("/ATCSim/Radio/")) + Speaker;
				Task->DestinationName = FPaths::GetBaseFilename(File);
				Task->bReplaceExisting = true;
				Task->bAutomated = true;
				Task->bSave = false;
				Tasks.Add(Task);
			}
			AssetTools.ImportAssetTasks(Tasks);

			TMap<FName, TObjectPtr<USoundBase>>& Set = FCString::Strcmp(Speaker, TEXT("Pilot")) == 0 ? Voice->Pilot : Voice->Controller;
			Set.Reset();
			for (UAssetImportTask* Task : Tasks)
			{
				for (UObject* Imported : Task->GetObjects())
				{
					if (USoundWave* Sound = Cast<USoundWave>(Imported))
					{
						Sound->Volume = 1.f;
						Set.Add(FName(*Task->DestinationName), Sound);
						Packages.AddUnique(Sound->GetPackage());
					}
				}
			}
			UE_LOG(LogATC, Display, TEXT("ATC.ImportRadioVoice: %s %d fragments"), Speaker, Set.Num());
		}
		Voice->MarkPackageDirty();
		UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
		UE_LOG(LogATC, Display, TEXT("ATC.ImportRadioVoice: done (%d packages)"), Packages.Num());
	}

	FAutoConsoleCommand ImportRadioVoiceCommand(TEXT("ATC.ImportRadioVoice"),
		TEXT("Import the radio fragments from Plugins/ATCSim/Data/Radio into /ATCSim/Radio and update DA_RadioVoice"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ImportRadioVoice));
}
