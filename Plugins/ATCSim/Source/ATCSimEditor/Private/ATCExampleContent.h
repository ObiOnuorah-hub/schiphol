#pragma once

#include "CoreMinimal.h"

class AEnvironmentRig;
class AStaticMeshActor;
class UMaterial;
class UMaterialInterface;
class UMaterialParameterCollection;
class ULensSetData;
class UTrafficMixData;
class UWorld;

/** Building blocks of ATC.CreateExampleContent that the airport map tools use too. */
namespace ATCExample
{
	UClass* MakeBlueprint(UClass* Parent, const FString& Folder, const FString& Name);
	UMaterialParameterCollection* MakeEnvironmentParameters();

	/** Ground that reacts to MPC_Environment: darker and glossy when wet, white with snow. */
	UMaterial* MakeGroundMaterial(const FString& Folder, const FString& Name, const FLinearColor& Base, UMaterialParameterCollection* Collection,
		float DryRoughness = 0.85f);

	/** Sun, moon, sky light, atmosphere, clouds, fog, an unbound post process volume and the lighting rig that drives them. */
	AEnvironmentRig* SpawnEnvironment(UWorld* World);

	/** Yellow taxiway paint (/ATCSim/Materials/M_TaxiLine). */
	UMaterial* MakeTaxiLineMaterial();

	/** Camera screen (/ATCSim/Materials/M_Monitor). */
	UMaterial* MakeMonitorMaterial();

	/** DA_LensSet_GMI: Wide 24, Normal 50, Tele 100 and Custom 400 mm on a 36 mm sensor (saved). */
	ULensSetData* MakeLensSet();

	/** Operator input for the GMI cameras: /ATCSim/Input/IA_GMI_* and IMC_GMI with the default mapping (saved). */
	void MakeCameraInput();

	AStaticMeshActor* Block(UWorld* World, const TCHAR* MeshPath, const FVector& Location, const FVector& Scale, UMaterialInterface* Material, const TCHAR* Label);

	/** View from a tower cab: a little in front of the middle (where the consoles are), looking at Yaw/Pitch. */
	FTransform CabView(const FVector& CabCenter, float FloorZ, float Yaw, float Pitch, float ForwardCm);

	/** Every aircraft type with the project's models and liveries (ATC.SetupFleet). */
	void MakeFleet();

	/** The Schiphol day: which types and airlines, how often. */
	void FillEHAMTrafficMix(UTrafficMixData* Mix);
}
