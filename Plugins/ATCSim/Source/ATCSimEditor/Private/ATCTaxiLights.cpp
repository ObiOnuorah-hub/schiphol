// Taxiway centerline and stop bar lights. One small mesh, one unlit material; ATaxiRoute places the lights as
// instances and switches them with per-instance custom data (0 = on, 1-3 = colour).

#include "ATCTaxiLights.h"
#include "ATCExampleContent.h"
#include "ATCSchematic.h"
#include "ATCUnits.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionEyeAdaptationInverse.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionViewProperty.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"

namespace ATCTaxiLights
{
	UMaterial* MakeMaterial()
	{
		const FString PackageName = TEXT("/ATCSim/Lights/M_TaxiLight");
		UMaterial* Material = FPackageName::DoesPackageExist(PackageName) ? LoadObject<UMaterial>(nullptr, *(PackageName + TEXT(".M_TaxiLight"))) : nullptr;
		if (!Material)
		{
			Material = NewObject<UMaterial>(CreatePackage(*PackageName), TEXT("M_TaxiLight"), RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Material);
		}
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);
		Material->bUsedWithInstancedStaticMeshes = true;
		auto Make = [Material](UClass* Class, int32 X, int32 Y) { return UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y); };

		// Colour: lit lights keep the same brightness on screen at any exposure (day and night), off lights are dark glass
		UMaterialExpressionPerInstanceCustomData* On = Cast<UMaterialExpressionPerInstanceCustomData>(Make(UMaterialExpressionPerInstanceCustomData::StaticClass(), -900, 200));
		On->DataIndex = 0;
		On->ConstDefaultValue = 1.f;
		UMaterialExpressionPerInstanceCustomData3Vector* Color = Cast<UMaterialExpressionPerInstanceCustomData3Vector>(Make(UMaterialExpressionPerInstanceCustomData3Vector::StaticClass(), -900, 0));
		Color->DataIndex = 1;
		Color->ConstDefaultValue = FLinearColor(0.05f, 1.f, 0.3f);
		UMaterialExpressionScalarParameter* Brightness = Cast<UMaterialExpressionScalarParameter>(Make(UMaterialExpressionScalarParameter::StaticClass(), -900, 100));
		Brightness->ParameterName = TEXT("Brightness");
		Brightness->DefaultValue = 4.f;
		UMaterialExpressionMultiply* Lit = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -650, 0));
		Lit->A.Connect(0, Color);
		Lit->B.Connect(0, Brightness);
		UMaterialExpressionEyeAdaptationInverse* Adapted = Cast<UMaterialExpressionEyeAdaptationInverse>(Make(UMaterialExpressionEyeAdaptationInverse::StaticClass(), -450, 0));
		Adapted->LightValueInput.Connect(0, Lit);
		UMaterialExpressionConstant3Vector* Off = Cast<UMaterialExpressionConstant3Vector>(Make(UMaterialExpressionConstant3Vector::StaticClass(), -450, -150));
		Off->Constant = FLinearColor(0.01f, 0.01f, 0.012f);
		UMaterialExpressionLinearInterpolate* Emissive = Cast<UMaterialExpressionLinearInterpolate>(Make(UMaterialExpressionLinearInterpolate::StaticClass(), -200, 0));
		Emissive->A.Connect(0, Off);
		Emissive->B.Connect(0, Adapted);
		Emissive->Alpha.Connect(0, On);
		UMaterialEditingLibrary::ConnectMaterialProperty(Emissive, TEXT(""), MP_EmissiveColor);

		// Size: a 30 cm light is far below a pixel from the tower. A lit light grows with its distance to the camera so it
		// stays about two pixels wide (and three times as tall, the view from the cab is almost flat). Off lights keep their size.
		UMaterialExpression* Camera = Make(UMaterialExpressionCameraPositionWS::StaticClass(), -1300, 400);
		UMaterialExpressionWorldPosition* World = Cast<UMaterialExpressionWorldPosition>(Make(UMaterialExpressionWorldPosition::StaticClass(), -1300, 500));
		World->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;
		UMaterialExpressionDistance* Distance = Cast<UMaterialExpressionDistance>(Make(UMaterialExpressionDistance::StaticClass(), -1100, 450));
		Distance->A.Connect(0, Camera);
		Distance->B.Connect(0, World);
		// A zoomed camera (binoculars, GMI camera) shows more pixels per metre: tan(FOV / 2) is 1 at 90 degrees
		UMaterialExpressionViewProperty* Fov = Cast<UMaterialExpressionViewProperty>(Make(UMaterialExpressionViewProperty::StaticClass(), -1300, 650));
		Fov->Property = MEVP_TanHalfFieldOfView;
		UMaterialExpressionComponentMask* FovX = Cast<UMaterialExpressionComponentMask>(Make(UMaterialExpressionComponentMask::StaticClass(), -1150, 650));
		FovX->R = true;
		FovX->G = false;
		FovX->B = false;
		FovX->A = false;
		FovX->Input.Connect(0, Fov);
		UMaterialExpressionMultiply* ViewDistance = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -1000, 500));
		ViewDistance->A.Connect(0, Distance);
		ViewDistance->B.Connect(0, FovX);
		UMaterialExpressionScalarParameter* GrowDistance = Cast<UMaterialExpressionScalarParameter>(Make(UMaterialExpressionScalarParameter::StaticClass(), -1100, 550));
		GrowDistance->ParameterName = TEXT("GrowDistance");
		GrowDistance->DefaultValue = 15000.f;
		UMaterialExpressionDivide* Ratio = Cast<UMaterialExpressionDivide>(Make(UMaterialExpressionDivide::StaticClass(), -900, 450));
		Ratio->A.Connect(0, ViewDistance);
		Ratio->B.Connect(0, GrowDistance);
		UMaterialExpressionClamp* Scale = Cast<UMaterialExpressionClamp>(Make(UMaterialExpressionClamp::StaticClass(), -750, 450));
		Scale->Input.Connect(0, Ratio);
		Scale->MinDefault = 1.f;
		Scale->MaxDefault = 60.f;
		UMaterialExpressionSubtract* Extra = Cast<UMaterialExpressionSubtract>(Make(UMaterialExpressionSubtract::StaticClass(), -600, 450));
		Extra->A.Connect(0, Scale);
		Extra->ConstB = 1.f;
		UMaterialExpressionMultiply* ExtraOn = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -450, 450));
		ExtraOn->A.Connect(0, Extra);
		ExtraOn->B.Connect(0, On);

		UMaterialExpressionLocalPosition* Local = Cast<UMaterialExpressionLocalPosition>(Make(UMaterialExpressionLocalPosition::StaticClass(), -750, 300));
		Local->IncludedOffsets = EPositionIncludedOffsets::ExcludeOffsets;
		Local->LocalOrigin = ELocalPositionOrigin::Instance;
		UMaterialExpressionConstant3Vector* Taller = Cast<UMaterialExpressionConstant3Vector>(Make(UMaterialExpressionConstant3Vector::StaticClass(), -750, 350));
		Taller->Constant = FLinearColor(1.f, 1.f, 3.f);
		UMaterialExpressionMultiply* Stretched = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -450, 300));
		Stretched->A.Connect(0, Local);
		Stretched->B.Connect(0, Taller);
		// The instances are not rotated or scaled, so the instance-space offset is also the world offset
		UMaterialExpressionMultiply* Offset = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -250, 350));
		Offset->A.Connect(0, Stretched);
		Offset->B.Connect(0, ExtraOn);
		UMaterialEditingLibrary::ConnectMaterialProperty(Offset, TEXT(""), MP_WorldPositionOffset);

		UMaterialEditingLibrary::RecompileMaterial(Material);
		Material->MarkPackageDirty();
		return Material;
	}

	UStaticMesh* MakeMesh(UMaterialInterface* Material)
	{
		// Inset light: 30 cm across, 10 cm above the ground
		TArray<FVector2D> Octagon;
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const double Angle = Step * UE_DOUBLE_TWO_PI / 8.0;
			Octagon.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 15.0);
		}
		ATCSchematic::FMeshBuilder Builder;
		Builder.Prism(Material, Material, Octagon, -2.0, 10.0);
		UStaticMesh* Mesh = Builder.Save(TEXT("/ATCSim/Lights/SM_TaxiLight"));

		// Lit lights grow in the material (up to 60x); bigger bounds keep them from being culled at the screen edge
		Mesh->SetPositiveBoundsExtension(FVector(900.0, 900.0, 1800.0));
		Mesh->SetNegativeBoundsExtension(FVector(900.0, 900.0, 0.0));
		Mesh->CalculateExtendedBounds();
		Mesh->MarkPackageDirty();
		return Mesh;
	}

	TArray<UPackage*> Create()
	{
		UMaterial* Material = MakeMaterial();
		UStaticMesh* Mesh = MakeMesh(Material);
		// The taxiway paint used to glow green at night as a stand-in for these lights
		UMaterial* Paint = ATCExample::MakeTaxiLineMaterial();
		UMaterial* Screen = ATCExample::MakeMonitorMaterial();
		UE_LOG(LogATC, Display, TEXT("ATC.CreateTaxiLights: %s, %s and the screen material"), *Mesh->GetPathName(), *Material->GetPathName());
		return { Material->GetPackage(), Mesh->GetPackage(), Paint->GetPackage(), Screen->GetPackage() };
	}

	void CreateCommand(const TArray<FString>& Args)
	{
		UEditorLoadingAndSavingUtils::SavePackages(Create(), false);
	}

	FAutoConsoleCommand CreateTaxiLightsCommand(TEXT("ATC.CreateTaxiLights"), TEXT("Build the taxiway light mesh and material (/ATCSim/Lights)"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CreateCommand));
}
