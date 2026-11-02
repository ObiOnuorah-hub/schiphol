#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Materials/MaterialInstance.h"
#include "Engine/Texture.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Engine/Texture.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunctionInterface.h"
#include "ObjectTools.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Factories/MaterialFactoryNew.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Engine/Texture2D.h"
#include "Aircraft.h"
#include "FlightPath.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#endif

namespace
{
	/** 0 = no marking, 1 = asphalt patch (lifted once), 2 = paint on top of that (lifted twice). */
	int32 GetMarkingLayer(const UStaticMeshComponent* Component)
	{
		for (const UMaterialInterface* Material : Component->GetMaterials())
		{
			if (!Material || !Material->GetName().StartsWith(TEXT("Decal")))
			{
				continue;
			}

			const FString Name = Material->GetName();
			const bool bAsphalt = Name.Contains(TEXT("Asphalt")) || Name.Contains(TEXT("RWY_")) || Name.Contains(TEXT("ParkingPos"))
				|| Name.Contains(TEXT("RainGutter")) || Name.Contains(TEXT("ApronLight"));
			return bAsphalt ? 1 : 2;
		}
		return 0;
	}

	/** Lifts all actors with a Decal_ material a bit, so the markings lie on top of the asphalt instead of under it. */
	void RaiseMarkings(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->WorldType != EWorldType::Editor)
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.RaiseMarkings: run this in the editor, not during Play."));
			return;
		}

		const float Amount = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 5.f;
		static const FName DoneTag(TEXT("MarkingsRaised"));
		int32 Count = 0;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor->Tags.Contains(DoneTag))
			{
				continue;
			}

			int32 Layer = 0;
			TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
			for (const UStaticMeshComponent* Component : Components)
			{
				Layer = FMath::Max(Layer, GetMarkingLayer(Component));
			}
			if (Layer == 0)
			{
				continue;
			}

			Actor->Modify();
			Actor->AddActorWorldOffset(FVector(0.0, 0.0, Amount * Layer));
			Actor->Tags.Add(DoneTag);
			++Count;
		}

		UE_LOG(LogTemp, Display, TEXT("Airport.RaiseMarkings: lifted %d actors. Save the level to keep it."), Count);
	}

#if WITH_EDITOR
	/** Recreates the missing decal parent materials as copies of LambertSurfaceMaterialDecal. */
	void FixDecalMaterials()
	{
		const FString Folder = TEXT("/InterchangeAssets/Materials");
		UObject* Source = LoadObject<UObject>(nullptr, *(Folder + TEXT("/LambertSurfaceMaterialDecal.LambertSurfaceMaterialDecal")));
		if (!Source)
		{
			UE_LOG(LogTemp, Error, TEXT("Airport.FixDecalMaterials: LambertSurfaceMaterialDecal not found."));
			return;
		}

		const TCHAR* Names[] = {
			TEXT("DecalblackLambertSurfaceMaterial2"),
			TEXT("LambertSurfaceMaterialDecalDirtParking"),
			TEXT("LambertSurfaceMaterialDecalDirtParking1"),
			TEXT("LambertSurfaceMaterialDecalLettersBlack"),
			TEXT("LambertSurfaceMaterialDecalLettersWhite"),
			TEXT("LambertSurfaceMaterialDecalLettersYellow"),
			TEXT("LambertSurfaceMaterialDecalRWYAbrasion"),
			TEXT("LambertSurfaceMaterialDecalRWYLetters"),
		};

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		TArray<UPackage*> ToSave;
		for (const TCHAR* Name : Names)
		{
			if (FPackageName::DoesPackageExist(Folder / Name))
			{
				UE_LOG(LogTemp, Display, TEXT("Airport.FixDecalMaterials: %s already exists"), Name);
				continue;
			}
			if (UObject* Copy = AssetTools.DuplicateAsset(Name, Folder, Source))
			{
				ToSave.Add(Copy->GetPackage());
				UE_LOG(LogTemp, Display, TEXT("Airport.FixDecalMaterials: created %s"), Name);
			}
		}

		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.FixDecalMaterials: saved %d materials"), ToSave.Num());
	}

	/** Missing tree parent, Nanite usage flag, glass on Nanite meshes. Saves everything. */
	void FixAll(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->WorldType != EWorldType::Editor)
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.FixAll: run this in the editor, not during Play."));
			return;
		}

		const FString Folder = TEXT("/InterchangeAssets/Materials");
		TArray<UPackage*> ToSave;

		// 1. Tree1 parent is gone: copy the restored Tree2
		if (!FPackageName::DoesPackageExist(Folder / TEXT("LambertSurfaceMaterialTree1")))
		{
			UObject* Tree2 = LoadObject<UObject>(nullptr, *(Folder + TEXT("/LambertSurfaceMaterialTree2.LambertSurfaceMaterialTree2")));
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			if (UObject* Copy = Tree2 ? AssetTools.DuplicateAsset(TEXT("LambertSurfaceMaterialTree1"), Folder, Tree2) : nullptr)
			{
				ToSave.Add(Copy->GetPackage());
				UE_LOG(LogTemp, Display, TEXT("Airport.FixAll: created LambertSurfaceMaterialTree1"));
			}
		}

		// 2. Material used on Nanite meshes without the Nanite usage flag
		if (UMaterial* Phong = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/PhongSurfaceMaterial.PhongSurfaceMaterial"))))
		{
			if (!Phong->bUsedWithNanite)
			{
				Phong->Modify();
				Phong->bUsedWithNanite = true;
				Phong->PostEditChange();
				ToSave.Add(Phong->GetPackage());
				UE_LOG(LogTemp, Display, TEXT("Airport.FixAll: PhongSurfaceMaterial now used with Nanite"));
			}
		}

		// 3. Nanite cannot draw glass (translucent): render those meshes without Nanite
		int32 GlassFixed = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (UStaticMeshComponent* Component : Components)
			{
				const UStaticMesh* Mesh = Component->GetStaticMesh();
				if (!Mesh || !Mesh->IsNaniteEnabled() || Component->bDisallowNanite)
				{
					continue;
				}

				bool bTranslucent = false;
				for (const UMaterialInterface* Material : Component->GetMaterials())
				{
					if (Material && Material->GetBlendMode() != BLEND_Opaque && Material->GetBlendMode() != BLEND_Masked)
					{
						bTranslucent = true;
						break;
					}
				}
				if (!bTranslucent)
				{
					continue;
				}

				It->Modify();
				Component->Modify();
				Component->bDisallowNanite = true;
				Component->MarkRenderStateDirty();
				++GlassFixed;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Airport.FixAll: %d meshes with glass now render without Nanite"), GlassFixed);

		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true);
		UE_LOG(LogTemp, Display, TEXT("Airport.FixAll: saved"));
	}

	FAutoConsoleCommandWithWorldAndArgs FixAllCommand(
		TEXT("Airport.FixAll"),
		TEXT("Fix tree materials, Nanite usage flag and glass on Nanite meshes, then save"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FixAll));

	/**
	 * Terrain photos are Virtual Textures, but LambertSurfaceMaterial (engine plugin) samples them as normal textures,
	 * so the ground renders black. Make a project copy with a virtual sampler and move only those instances onto it.
	 */
	void FixTerrain()
	{
		const FName DiffuseName(TEXT("DiffuseColorMap"));
		UMaterial* Lambert = LoadObject<UMaterial>(nullptr, TEXT("/InterchangeAssets/Materials/LambertSurfaceMaterial.LambertSurfaceMaterial"));
		if (!Lambert)
		{
			UE_LOG(LogTemp, Error, TEXT("Airport.FixTerrain: LambertSurfaceMaterial not found"));
			return;
		}

		// All material instances in the EHAM folder that use Lambert with a virtual texture
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game/EHAM"));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		const FString Folder = TEXT("/Game/EHAM/Materials");
		UMaterial* TerrainMaterial = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/M_TerrainVT.M_TerrainVT")));

		TArray<UMaterialInstanceConstant*> Targets;
		UTexture* SampleTexture = nullptr;
		for (const FAssetData& Asset : Assets)
		{
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
			UTexture* Texture = nullptr;
			if (Instance && (Instance->Parent == Lambert || (TerrainMaterial && Instance->Parent == TerrainMaterial))
				&& Instance->GetTextureParameterValue(FMaterialParameterInfo(DiffuseName), Texture) && Texture && Texture->VirtualTextureStreaming)
			{
				Targets.Add(Instance);
				SampleTexture = Texture;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Airport.FixTerrain: %d of %d instances need a virtual texture parent"), Targets.Num(), Assets.Num());
		if (Targets.Num() == 0)
		{
			return;
		}

		// Simple terrain material: aerial photo (virtual texture) -> base color, rough ground
		TArray<UPackage*> ToSave;
		if (!TerrainMaterial)
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			TerrainMaterial = Cast<UMaterial>(AssetTools.DuplicateAsset(TEXT("M_TerrainVT"), Folder, Lambert));
			if (!TerrainMaterial)
			{
				UE_LOG(LogTemp, Error, TEXT("Airport.FixTerrain: could not create M_TerrainVT"));
				return;
			}
		}

		TerrainMaterial->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(TerrainMaterial);

		UMaterialExpressionTextureSampleParameter2D* Photo = Cast<UMaterialExpressionTextureSampleParameter2D>(
			UMaterialEditingLibrary::CreateMaterialExpression(TerrainMaterial, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -500, 0));
		Photo->ParameterName = DiffuseName;
		Photo->Texture = SampleTexture;
		Photo->SamplerType = SampleTexture->SRGB ? SAMPLERTYPE_VirtualColor : SAMPLERTYPE_VirtualLinearColor;

		// Close-up detail: the photo is ~1 pixel per metre, so add fine grain within 20 m that fades out by 80 m
		auto Make = [TerrainMaterial](UClass* Class, int32 X, int32 Y)
		{
			return UMaterialEditingLibrary::CreateMaterialExpression(TerrainMaterial, Class, X, Y);
		};

		UMaterialExpression* WorldPosition = Make(UMaterialExpressionWorldPosition::StaticClass(), -1300, 500);
		UMaterialExpressionComponentMask* XY = Cast<UMaterialExpressionComponentMask>(Make(UMaterialExpressionComponentMask::StaticClass(), -1150, 500));
		XY->R = true;
		XY->G = true;
		UMaterialEditingLibrary::ConnectMaterialExpressions(WorldPosition, TEXT(""), XY, TEXT(""));

		UMaterialExpressionMultiply* DetailUV = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -1000, 500));
		DetailUV->ConstB = 1.f / 150.f;  // one tile per 1.5 m
		UMaterialEditingLibrary::ConnectMaterialExpressions(XY, TEXT(""), DetailUV, TEXT("A"));

		UTexture* NoiseTexture = LoadObject<UTexture>(nullptr, TEXT("/Engine/MapTemplates/TilingNoise05.TilingNoise05"));
		UMaterialExpressionTextureSample* Noise = Cast<UMaterialExpressionTextureSample>(Make(UMaterialExpressionTextureSample::StaticClass(), -850, 500));
		Noise->Texture = NoiseTexture;
		Noise->SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture(NoiseTexture);
		UMaterialEditingLibrary::ConnectMaterialExpressions(DetailUV, TEXT(""), Noise, TEXT("UVs"));

		UMaterialExpressionSubtract* Centered = Cast<UMaterialExpressionSubtract>(Make(UMaterialExpressionSubtract::StaticClass(), -650, 500));
		Centered->ConstB = 0.5f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Noise, TEXT("R"), Centered, TEXT("A"));

		UMaterialExpression* Depth = Make(UMaterialExpressionPixelDepth::StaticClass(), -1000, 700);
		UMaterialExpressionSubtract* Near = Cast<UMaterialExpressionSubtract>(Make(UMaterialExpressionSubtract::StaticClass(), -850, 700));
		Near->ConstB = 2000.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Depth, TEXT(""), Near, TEXT("A"));
		UMaterialExpressionDivide* Range = Cast<UMaterialExpressionDivide>(Make(UMaterialExpressionDivide::StaticClass(), -700, 700));
		Range->ConstB = 6000.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Near, TEXT(""), Range, TEXT("A"));
		UMaterialExpression* Fade = Make(UMaterialExpressionOneMinus::StaticClass(), -550, 700);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Range, TEXT(""), Fade, TEXT(""));
		UMaterialExpression* FadeClamped = Make(UMaterialExpressionSaturate::StaticClass(), -450, 700);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Fade, TEXT(""), FadeClamped, TEXT(""));

		UMaterialExpressionMultiply* Amount = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -450, 500));
		Amount->ConstB = 0.7f;  // grain strength: brightness varies +-35%
		UMaterialEditingLibrary::ConnectMaterialExpressions(Centered, TEXT(""), Amount, TEXT("A"));
		UMaterialExpressionMultiply* AmountNear = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -300, 550));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Amount, TEXT(""), AmountNear, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(FadeClamped, TEXT(""), AmountNear, TEXT("B"));
		UMaterialExpressionAdd* Factor = Cast<UMaterialExpressionAdd>(Make(UMaterialExpressionAdd::StaticClass(), -200, 550));
		Factor->ConstB = 1.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(AmountNear, TEXT(""), Factor, TEXT("A"));

		UMaterialExpressionMultiply* Detailed = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -100, 0));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Photo, TEXT("RGB"), Detailed, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Factor, TEXT(""), Detailed, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Detailed, TEXT(""), MP_BaseColor);

		UMaterialExpressionConstant* Roughness = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(TerrainMaterial, UMaterialExpressionConstant::StaticClass(), -300, 250));
		Roughness->R = 1.f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);

		// Grass and soil barely reflect: no wet-looking sun glare
		UMaterialExpressionConstant* Specular = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(TerrainMaterial, UMaterialExpressionConstant::StaticClass(), -300, 350));
		Specular->R = 0.2f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Specular, TEXT(""), MP_Specular);

		TerrainMaterial->bUsedWithNanite = true;
		UMaterialEditingLibrary::RecompileMaterial(TerrainMaterial);
		ToSave.Add(TerrainMaterial->GetPackage());
		UE_LOG(LogTemp, Display, TEXT("Airport.FixTerrain: M_TerrainVT rebuilt (virtual sampler, sRGB=%d)"), SampleTexture->SRGB ? 1 : 0);

		for (UMaterialInstanceConstant* Instance : Targets)
		{
			Instance->Modify();
			Instance->SetParentEditorOnly(TerrainMaterial);
			Instance->PostEditChange();
			ToSave.Add(Instance->GetPackage());
		}

		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.FixTerrain: %d terrain materials now use M_TerrainVT, saved"), Targets.Num());
	}

	/** Asphalt, runway and dirt patches lying on top of the ground (the dark boxes at gates and touchdown zones). */
	bool IsOverlayTexture(const UTexture* Texture)
	{
		const FString Name = Texture->GetName();
		return Name.Contains(TEXT("Asphalt_01")) || Name.Contains(TEXT("RunwayAsphalt")) || Name.Contains(TEXT("RWY_"))
			|| Name.Contains(TEXT("ParkingSpotDirt"));
	}

	/**
	 * Those patches rendered as black boxes: their parent multiplied (Modulate) at full strength, grey x grey = almost black.
	 * Give them a project material that modulates at a low Strength: a subtle tint of the asphalt below.
	 */
	void FixOverlays()
	{
		const FName DiffuseName(TEXT("DiffuseColorMap"));
		const FName OpacityName(TEXT("Opacity"));
		const FString Folder = TEXT("/Game/EHAM/Materials");

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game/EHAM"));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		TArray<UMaterialInstanceConstant*> Targets;
		UTexture* SampleTexture = nullptr;
		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.AssetName.ToString().StartsWith(TEXT("Decal")))
			{
				continue;
			}

			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
			UTexture* Texture = nullptr;
			const bool bHasTexture = Instance && Instance->GetTextureParameterValue(FMaterialParameterInfo(DiffuseName), Texture) && Texture;
			UE_LOG(LogTemp, Display, TEXT("Airport.FixOverlays: check %s texture=%s vt=%d parent=%s"), *Asset.AssetName.ToString(),
				bHasTexture ? *Texture->GetName() : TEXT("none"), bHasTexture && Texture->VirtualTextureStreaming ? 1 : 0,
				Instance && Instance->Parent ? *Instance->Parent->GetName() : TEXT("none"));
			if (bHasTexture && IsOverlayTexture(Texture))
			{
				float Opacity = 1.f;
				Instance->GetScalarParameterValue(FMaterialParameterInfo(OpacityName), Opacity);
				UE_LOG(LogTemp, Display, TEXT("Airport.FixOverlays: %s texture=%s sRGB=%d opacity=%.2f parent=%s"),
					*Instance->GetName(), *Texture->GetName(), Texture->SRGB ? 1 : 0, Opacity, Instance->Parent ? *Instance->Parent->GetName() : TEXT("none"));
				Targets.Add(Instance);
				SampleTexture = Texture;
			}
		}
		if (Targets.Num() == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("Airport.FixOverlays: nothing to fix"));
			return;
		}

		UMaterial* Overlay = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/M_GroundOverlay.M_GroundOverlay")));
		if (!Overlay)
		{
			UMaterial* Terrain = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/M_TerrainVT.M_TerrainVT")));
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			Overlay = Terrain ? Cast<UMaterial>(AssetTools.DuplicateAsset(TEXT("M_GroundOverlay"), Folder, Terrain)) : nullptr;
			if (!Overlay)
			{
				UE_LOG(LogTemp, Error, TEXT("Airport.FixOverlays: could not create M_GroundOverlay (run Airport.FixTerrain first)"));
				return;
			}
		}

		// Modulate: the patch only darkens/tints the asphalt below by Strength, so light and shadows stay correct
		// and there are no hard opaque boxes. Strength 1 = full texture (that was the black look).
		Overlay->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Overlay);

		UMaterialExpressionTextureSampleParameter2D* Photo = Cast<UMaterialExpressionTextureSampleParameter2D>(
			UMaterialEditingLibrary::CreateMaterialExpression(Overlay, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -700, 0));
		Photo->ParameterName = DiffuseName;
		Photo->Texture = SampleTexture;
		if (SampleTexture->VirtualTextureStreaming)
		{
			Photo->SamplerType = SampleTexture->SRGB ? SAMPLERTYPE_VirtualColor : SAMPLERTYPE_VirtualLinearColor;
		}
		else
		{
			Photo->SamplerType = SampleTexture->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
		}

		UMaterialExpressionScalarParameter* Strength = Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(Overlay, UMaterialExpressionScalarParameter::StaticClass(), -700, 300));
		Strength->ParameterName = TEXT("Strength");
		Strength->DefaultValue = 0.35f;

		UMaterialExpressionMultiply* Fade = Cast<UMaterialExpressionMultiply>(
			UMaterialEditingLibrary::CreateMaterialExpression(Overlay, UMaterialExpressionMultiply::StaticClass(), -450, 300));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Strength, TEXT(""), Fade, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Photo, TEXT("A"), Fade, TEXT("B"));

		UMaterialExpressionLinearInterpolate* Tint = Cast<UMaterialExpressionLinearInterpolate>(
			UMaterialEditingLibrary::CreateMaterialExpression(Overlay, UMaterialExpressionLinearInterpolate::StaticClass(), -250, 100));
		Tint->ConstA = 1.f;  // white = leave the asphalt as it is
		UMaterialEditingLibrary::ConnectMaterialExpressions(Photo, TEXT("RGB"), Tint, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Fade, TEXT(""), Tint, TEXT("Alpha"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Tint, TEXT(""), MP_EmissiveColor);

		Overlay->BlendMode = BLEND_Modulate;
		Overlay->SetShadingModel(MSM_Unlit);
		Overlay->bUsedWithNanite = true;
		UMaterialEditingLibrary::RecompileMaterial(Overlay);

		TArray<UPackage*> ToSave = { Overlay->GetPackage() };
		for (UMaterialInstanceConstant* Instance : Targets)
		{
			Instance->Modify();
			Instance->SetParentEditorOnly(Overlay);

			// Oil/dirt stains a bit stronger than plain asphalt colour patches
			UTexture* Texture = nullptr;
			Instance->GetTextureParameterValue(FMaterialParameterInfo(DiffuseName), Texture);
			const bool bDirt = Texture && (Texture->GetName().Contains(TEXT("Dirt")) || Texture->GetName().Contains(TEXT("RWY_Abrasion")));
			Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Strength")), bDirt ? 0.55f : 0.35f);

			Instance->PostEditChange();
			ToSave.Add(Instance->GetPackage());
		}

		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.FixOverlays: %d overlay materials now use M_GroundOverlay, saved"), Targets.Num());

		// Earlier attempt with a virtual sampler: no longer used
		if (UMaterial* OldOverlay = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/M_OverlayVT.M_OverlayVT"))))
		{
			ObjectTools::DeleteAssets({ OldOverlay }, false);
			UE_LOG(LogTemp, Display, TEXT("Airport.FixOverlays: deleted M_OverlayVT"));
		}
	}

	/**
	 * Runway numbers and painted letters were invisible: their parents (rebuilt on 12 Oct as copies of the decal parent)
	 * use Modulate, and white x asphalt = asphalt. The textures are white or yellow letters with alpha: a masked material
	 * shows them on top of the asphalt.
	 */
	void FixLetters(const TArray<FString>& Args)
	{
		// "Airport.FixLetters debug": the letters in plain red, to find them in the map. "alpha": the texture alpha as colour, unmasked
		const bool bDebug = Args.Num() > 0 && Args[0] == TEXT("debug");
		const bool bShowAlpha = Args.Num() > 0 && Args[0] == TEXT("alpha");
		const FName DiffuseName(TEXT("DiffuseColorMap"));
		const FString Folder = TEXT("/Game/EHAM/Materials");
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game/EHAM"));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		TArray<UMaterialInstanceConstant*> Targets;
		UTexture* SampleTexture = nullptr;
		for (const FAssetData& Asset : Assets)
		{
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
			const FString Parent = Instance && Instance->Parent ? Instance->Parent->GetName() : FString();
			UTexture* Texture = nullptr;
			if (!Parent.StartsWith(TEXT("LambertSurfaceMaterialDecalRWYLetters")) && !Parent.StartsWith(TEXT("LambertSurfaceMaterialDecalLetters"))
				&& Parent != TEXT("M_PaintedLetters") && Parent != TEXT("M_RunwayLetters"))
			{
				continue;
			}
			if (!Instance->GetTextureParameterValue(FMaterialParameterInfo(DiffuseName), Texture) || !Texture || Texture->VirtualTextureStreaming)
			{
				UE_LOG(LogTemp, Warning, TEXT("Airport.FixLetters: %s skipped (no normal texture)"), *Instance->GetName());
				continue;
			}
			UE_LOG(LogTemp, Display, TEXT("Airport.FixLetters: %s texture=%s parent=%s"), *Instance->GetName(), *Texture->GetName(), *Parent);
			Targets.Add(Instance);
			SampleTexture = Texture;
		}
		if (Targets.Num() == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("Airport.FixLetters: nothing to fix"));
			return;
		}

		// A new material, not a copy of M_TerrainVT: the masked copies of that one (overlays, tree cards) never cut anything out
		UMaterial* Letters = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/M_RunwayLetters.M_RunwayLetters")));
		if (!Letters)
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			Letters = Cast<UMaterial>(AssetTools.CreateAsset(TEXT("M_RunwayLetters"), Folder, UMaterial::StaticClass(), NewObject<UMaterialFactoryNew>()));
			if (!Letters)
			{
				UE_LOG(LogTemp, Error, TEXT("Airport.FixLetters: could not create M_RunwayLetters"));
				return;
			}
		}

		// Texture -> base color, its alpha cuts out the letter
		Letters->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Letters);
		UMaterialExpressionTextureSampleParameter2D* Paint = Cast<UMaterialExpressionTextureSampleParameter2D>(
			UMaterialEditingLibrary::CreateMaterialExpression(Letters, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -500, 0));
		Paint->ParameterName = DiffuseName;
		Paint->Texture = SampleTexture;
		Paint->SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture(SampleTexture);
		if (bShowAlpha)
		{
			UMaterialEditingLibrary::ConnectMaterialProperty(Paint, TEXT("A"), MP_BaseColor);
			UMaterialEditingLibrary::ConnectMaterialProperty(Paint, TEXT("A"), MP_EmissiveColor);
		}
		else if (bDebug)
		{
			UMaterialExpressionConstant3Vector* Red = Cast<UMaterialExpressionConstant3Vector>(
				UMaterialEditingLibrary::CreateMaterialExpression(Letters, UMaterialExpressionConstant3Vector::StaticClass(), -250, -200));
			Red->Constant = FLinearColor(1.f, 0.f, 0.f);
			UMaterialEditingLibrary::ConnectMaterialProperty(Red, TEXT(""), MP_BaseColor);
			UMaterialEditingLibrary::ConnectMaterialProperty(Red, TEXT(""), MP_EmissiveColor);
		}
		else
		{
			UMaterialEditingLibrary::ConnectMaterialProperty(Paint, TEXT("RGB"), MP_BaseColor);
		}
		const bool bMaskConnected = !bShowAlpha && UMaterialEditingLibrary::ConnectMaterialProperty(Paint, TEXT("A"), MP_OpacityMask);
		UE_LOG(LogTemp, Display, TEXT("Airport.FixLetters: opacity mask connected=%d input=%s output=%d"), bMaskConnected ? 1 : 0,
			Letters->GetEditorOnlyData()->OpacityMask.Expression ? *Letters->GetEditorOnlyData()->OpacityMask.Expression->GetName() : TEXT("none"),
			Letters->GetEditorOnlyData()->OpacityMask.OutputIndex);

		UMaterialExpressionConstant* Roughness = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(Letters, UMaterialExpressionConstant::StaticClass(), -250, 300));
		Roughness->R = 0.9f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);
		UMaterialExpressionConstant* Specular = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(Letters, UMaterialExpressionConstant::StaticClass(), -250, 400));
		Specular->R = 0.3f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Specular, TEXT(""), MP_Specular);

		Letters->BlendMode = bShowAlpha ? BLEND_Opaque : BLEND_Masked;
		Letters->TwoSided = true;   // in case a letter quad was imported upside down
		Letters->OpacityMaskClipValue = 0.5f;
		Letters->SetShadingModel(MSM_DefaultLit);
		Letters->bUsedWithNanite = true;
		UMaterialEditingLibrary::RecompileMaterial(Letters);

		TArray<UPackage*> ToSave = { Letters->GetPackage() };
		for (UMaterialInstanceConstant* Instance : Targets)
		{
			const FMaterialInstanceBasePropertyOverrides& Old = Instance->BasePropertyOverrides;
			UE_LOG(LogTemp, Display, TEXT("Airport.FixLetters: %s overrides blend=%d twoSided=%d clip=%d (%.2f) shading=%d"), *Instance->GetName(),
				Old.bOverride_BlendMode ? 1 : 0, Old.bOverride_TwoSided ? 1 : 0, Old.bOverride_OpacityMaskClipValue ? 1 : 0, Old.OpacityMaskClipValue,
				Old.bOverride_ShadingModel ? 1 : 0);
			Instance->Modify();
			Instance->SetParentEditorOnly(Letters);
			// Made for the old Modulate parent: its own blend settings would beat the masked letters
			Instance->BasePropertyOverrides = FMaterialInstanceBasePropertyOverrides();
			Instance->PostEditChange();
			ToSave.Add(Instance->GetPackage());
		}
		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.FixLetters: %d letter materials now use M_RunwayLetters, saved"), Targets.Num());
	}

	/** Airport.MeshUV <mesh path>: UV channels of a mesh with their range, triangles and the first UVs (to check texture atlases). */
	void MeshUV(const TArray<FString>& Args)
	{
		const UStaticMesh* Mesh = Args.Num() > 0 ? LoadObject<UStaticMesh>(nullptr, *Args[0]) : nullptr;
		const FMeshDescription* Description = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!Description)
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.MeshUV: no mesh description for %s"), Args.Num() > 0 ? *Args[0] : TEXT(""));
			return;
		}
		const TVertexInstanceAttributesConstRef<FVector2f> UVs = FStaticMeshConstAttributes(*Description).GetVertexInstanceUVs();
		UE_LOG(LogTemp, Display, TEXT("Airport.MeshUV: %s triangles=%d vertex instances=%d uv channels=%d"), *Mesh->GetName(),
			Description->Triangles().Num(), Description->VertexInstances().Num(), UVs.GetNumChannels());
		for (int32 Channel = 0; Channel < UVs.GetNumChannels(); ++Channel)
		{
			FBox2f Range(ForceInit);
			FString First;
			int32 Count = 0;
			for (const FVertexInstanceID Instance : Description->VertexInstances().GetElementIDs())
			{
				const FVector2f UV = UVs.Get(Instance, Channel);
				Range += UV;
				if (Count++ < 8)
				{
					First += FString::Printf(TEXT(" (%.3f,%.3f)"), UV.X, UV.Y);
				}
			}
			UE_LOG(LogTemp, Display, TEXT("Airport.MeshUV:   channel %d range (%.3f,%.3f)-(%.3f,%.3f) first:%s"), Channel, Range.Min.X, Range.Min.Y,
				Range.Max.X, Range.Max.Y, *First);
		}
	}

	FAutoConsoleCommand MeshUVCommand(
		TEXT("Airport.MeshUV"),
		TEXT("Airport.MeshUV /Game/Path/Mesh.Mesh: UV channels and their range"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&MeshUV));

	FAutoConsoleCommand FixLettersCommand(
		TEXT("Airport.FixLetters"),
		TEXT("Make the runway numbers and painted letters visible (masked M_RunwayLetters instead of Modulate); debug = plain red"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&FixLetters));

	FAutoConsoleCommand FixOverlaysCommand(
		TEXT("Airport.FixOverlays"),
		TEXT("Fix the black asphalt/runway/dirt patches (gates, touchdown zones)"),
		FConsoleCommandDelegate::CreateStatic(&FixOverlays));

	/** Logs the bounds of every static mesh in a folder, to see if the parts already sit in model space. */
	void InspectModel(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			return;
		}

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(*Args[0]);
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		FBox Union(ForceInit);
		int32 NearOrigin = 0;
		for (const FAssetData& Asset : Assets)
		{
			const UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
			if (!Mesh)
			{
				continue;
			}
			const FBox Box = Mesh->GetBoundingBox();
			Union += Box;
			NearOrigin += Box.GetCenter().Size() < 100.0 ? 1 : 0;
		}
		UE_LOG(LogTemp, Display, TEXT("Airport.InspectModel: %s parts=%d centeredAtOrigin=%d union size=%s center=%s"),
			*Args[0], Assets.Num(), NearOrigin, *Union.GetSize().ToString(), *Union.GetCenter().ToString());
	}

	/**
	 * Finds where the tail is: vertices in the top 10% of the model are the fin, which sits at the back on the centre line.
	 * OutAxis is the fuselage axis (0 = X, 1 = Y), OutSign the direction of the tail along it.
	 */
	/** The real vertex positions of a mesh (mesh bounds can be padded). */
	void GetMeshPoints(const UStaticMesh* Mesh, TArray<FVector>& Points)
	{
		const FMeshDescription* Description = Mesh->GetMeshDescription(0);
		if (!Description)
		{
			Description = Mesh->GetHiResMeshDescription();  // Nanite source
		}
		if (!Description && Mesh->GetNumSourceModels() > 0)
		{
			Description = const_cast<UStaticMesh*>(Mesh)->GetSourceModel(0).GetOrCacheMeshDescription();
		}

		if (Description)
		{
			const TVertexAttributesConstRef<FVector3f> Positions = FStaticMeshConstAttributes(*Description).GetVertexPositions();
			for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
			{
				Points.Add(FVector(Positions[Vertex]));
			}
		}
		else if (const FStaticMeshRenderData* RenderData = Mesh->GetRenderData(); RenderData && RenderData->LODResources.Num() > 0)
		{
			// No source data: use the render vertices
			const FPositionVertexBuffer& Buffer = RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
			if (Buffer.GetVertexData())
			{
				for (uint32 Index = 0; Index < Buffer.GetNumVertices(); ++Index)
				{
					Points.Add(FVector(Buffer.VertexPosition(Index)));
				}
			}
		}
	}

	bool FindTail(const TArray<const UStaticMesh*>& Meshes, int32& OutAxis, double& OutSign, FBox& OutBox)
	{
		TArray<FVector> Points;
		for (const UStaticMesh* Mesh : Meshes)
		{
			GetMeshPoints(Mesh, Points);
		}

		if (Points.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.FindTail: no vertex data for %s"), *Meshes[0]->GetPathName());
			return false;
		}

		FBox Box(ForceInit);
		for (const FVector& Point : Points)
		{
			Box += Point;
		}
		const double TopZ = Box.Max.Z - 0.1 * Box.GetSize().Z;

		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const FVector& Point : Points)
		{
			if (Point.Z >= TopZ)
			{
				Sum += Point;
				++Count;
			}
		}

		OutBox = Box;
		const FVector Offset = Sum / Count - Box.GetCenter();
		const double RelativeX = FMath::Abs(Offset.X) / FMath::Max(Box.GetExtent().X, 1.0);
		const double RelativeY = FMath::Abs(Offset.Y) / FMath::Max(Box.GetExtent().Y, 1.0);
		OutAxis = RelativeX > RelativeY ? 0 : 1;
		OutSign = (OutAxis == 0 ? Offset.X : Offset.Y) >= 0.0 ? 1.0 : -1.0;
		return true;
	}

	/** Replaces the simple one-piece KLM/Emirates aircraft with the detailed multi-part models (old ones only hidden). */
	void SwapPlanes(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->WorldType != EWorldType::Editor)
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.SwapPlanes: run this in the editor, not during Play."));
			return;
		}

		const FString Root = TEXT("/Game/Datasmith/Vliegtuigen/");
		const TMap<FString, FString> Replacements = {
			{ TEXT("/A380_Emirates/"), TEXT("Emirates_Airbus_A380_861_A6_EOI") },
			{ TEXT("/787-9_KLM/"), TEXT("KLM_Boeing_787_9_Dreamliner") },
			{ TEXT("/777-306_KLM_orange_pride/"), TEXT("KLM_Boeing_777_306_ER") },
		};

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TMap<FString, TArray<const UStaticMesh*>> Models;
		auto GetModel = [&](const FString& Name) -> const TArray<const UStaticMesh*>&
		{
			if (!Models.Contains(Name))
			{
				FARFilter Filter;
				Filter.PackagePaths.Add(*(Root + Name));
				Filter.bRecursivePaths = true;
				Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
				TArray<FAssetData> Assets;
				Registry.GetAssets(Filter, Assets);
				TArray<const UStaticMesh*>& Parts = Models.Add(Name);
				for (const FAssetData& Asset : Assets)
				{
					if (const UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset()))
					{
						Parts.Add(Mesh);
					}
				}
			}
			return Models[Name];
		};

		// Collect first, spawning while iterating would change the actor list
		TArray<TPair<UStaticMeshComponent*, FString>> Targets;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (UStaticMeshComponent* Component : Components)
			{
				const UStaticMesh* Mesh = Component->GetStaticMesh();
				if (!Mesh || !Component->IsVisible())
				{
					continue;
				}
				for (const TPair<FString, FString>& Pair : Replacements)
				{
					if (Mesh->GetPathName().Contains(Pair.Key))
					{
						Targets.Add({ Component, Pair.Value });
					}
				}
			}
		}

		int32 Swapped = 0;
		for (const TPair<UStaticMeshComponent*, FString>& Target : Targets)
		{
			UStaticMeshComponent* OldComponent = Target.Key;
			const TArray<const UStaticMesh*>& Parts = GetModel(Target.Value);
			if (Parts.Num() == 0)
			{
				continue;
			}

			// Old plane: tail direction in the world, centre and ground height
			int32 OldAxis = 1, NewAxis = 1;
			double OldSign = 1.0, NewSign = 1.0;
			FBox OldLocalBox(ForceInit), NewLocalBox(ForceInit);
			const UStaticMesh* OldMesh = OldComponent->GetStaticMesh();
			if (!FindTail({ OldMesh }, OldAxis, OldSign, OldLocalBox) || !FindTail(Parts, NewAxis, NewSign, NewLocalBox))
			{
				UE_LOG(LogTemp, Warning, TEXT("Airport.SwapPlanes: could not find the tail of %s"), *OldComponent->GetOwner()->GetActorLabel());
				continue;
			}

			const FTransform OldTransform = OldComponent->GetComponentTransform();
			const FVector OldTail = OldTransform.TransformVectorNoScale(OldAxis == 0 ? FVector(OldSign, 0, 0) : FVector(0, OldSign, 0));
			const FVector NewTail = NewAxis == 0 ? FVector(NewSign, 0, 0) : FVector(0, NewSign, 0);
			const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(OldTail.Y, OldTail.X) - FMath::Atan2(NewTail.Y, NewTail.X));
			const FRotator Rotation(0.0, Yaw, 0.0);

			const FVector OldCenter = OldTransform.TransformPosition(OldLocalBox.GetCenter());
			const double OldGroundZ = OldTransform.TransformPosition(FVector(OldLocalBox.GetCenter().X, OldLocalBox.GetCenter().Y, OldLocalBox.Min.Z)).Z;
			const FVector RotatedNewCenter = Rotation.RotateVector(NewLocalBox.GetCenter());
			const FVector Location(OldCenter.X - RotatedNewCenter.X, OldCenter.Y - RotatedNewCenter.Y, OldGroundZ - NewLocalBox.Min.Z);

			// New plane: one actor, all parts at their model-space position, real size
			AActor* NewPlane = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Rotation, Location));
			USceneComponent* NewRoot = NewObject<USceneComponent>(NewPlane, TEXT("Root"));
			NewRoot->SetMobility(EComponentMobility::Static);
			NewPlane->SetRootComponent(NewRoot);
			NewPlane->AddInstanceComponent(NewRoot);
			NewRoot->RegisterComponent();
			NewPlane->SetActorTransform(FTransform(Rotation, Location));

			for (const UStaticMesh* Part : Parts)
			{
				UStaticMeshComponent* PartComponent = NewObject<UStaticMeshComponent>(NewPlane, MakeUniqueObjectName(NewPlane, UStaticMeshComponent::StaticClass(), Part->GetFName()));
				PartComponent->SetMobility(EComponentMobility::Static);
				PartComponent->SetStaticMesh(const_cast<UStaticMesh*>(Part));
				PartComponent->SetupAttachment(NewRoot);
				for (const UMaterialInterface* Material : PartComponent->GetMaterials())
				{
					if (Part->IsNaniteEnabled() && Material && Material->GetBlendMode() != BLEND_Opaque && Material->GetBlendMode() != BLEND_Masked)
					{
						PartComponent->bDisallowNanite = true;  // glass
					}
				}
				NewPlane->AddInstanceComponent(PartComponent);
				PartComponent->RegisterComponent();
			}

			NewPlane->SetActorLabel(Target.Value);
			NewPlane->SetFolderPath(TEXT("Aircraft_HQ"));

			// Old plane stays in the level, hidden
			OldComponent->Modify();
			OldComponent->SetVisibility(false);
			OldComponent->SetHiddenInGame(true);
			OldComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			UE_LOG(LogTemp, Display, TEXT("Airport.SwapPlanes: %s -> %s (%d parts, yaw %.0f)"),
				*OldComponent->GetOwner()->GetActorLabel(), *Target.Value, Parts.Num(), Yaw);
			++Swapped;
		}

		UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.SwapPlanes: %d planes replaced, level saved"), Swapped);
	}

	/**
	 * Puts a FlightPath on the longest real runway (found from the runway markings) and an aircraft on it.
	 * Takeoff and landing then happen on the runway; pushback/taxi splines still need to be laid over the taxiways.
	 */
	void PlaceFlight(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->WorldType != EWorldType::Editor)
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.PlaceFlight: run this in the editor, not during Play."));
			return;
		}

		// Runway markings come in pieces: find each piece's axis, then join pieces that lie on one line
		struct FRunway { FVector Origin; FVector Direction; double Min; double Max; double Z; };
		TArray<FRunway> Runways;
		auto AddPiece = [&Runways](const FVector& Mean, FVector Direction, double Min, double Max)
		{
			if (Direction.X < 0.0)
			{
				Direction = -Direction;
				const double OldMin = Min;
				Min = -Max;
				Max = -OldMin;
			}
			for (FRunway& Runway : Runways)
			{
				const bool bParallel = FMath::Abs(FVector::DotProduct(Runway.Direction, Direction)) > FMath::Cos(FMath::DegreesToRadians(3.0));
				const FVector Offset = Mean - Runway.Origin;
				const double Sideways = (Offset - Runway.Direction * FVector::DotProduct(Offset, Runway.Direction)).Size2D();
				if (bParallel && Sideways < 6000.0)
				{
					const double Shift = FVector::DotProduct(Offset, Runway.Direction);
					Runway.Min = FMath::Min(Runway.Min, Shift + Min);
					Runway.Max = FMath::Max(Runway.Max, Shift + Max);
					return;
				}
			}
			Runways.Add({ Mean, Direction, Min, Max, Mean.Z });
		};

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				const UMaterialInterface* Material = Component->GetMaterial(0);
				if (!Material || Material->GetName() != TEXT("Decal_RunwayMarkings_m") || !Component->GetStaticMesh())
				{
					continue;
				}

				TArray<FVector> Points;
				GetMeshPoints(Component->GetStaticMesh(), Points);
				if (Points.Num() < 10)
				{
					continue;
				}
				const FTransform Transform = Component->GetComponentTransform();
				FVector Mean = FVector::ZeroVector;
				for (FVector& Point : Points)
				{
					Point = Transform.TransformPosition(Point);
					Mean += Point;
				}
				Mean /= Points.Num();

				double Cxx = 0, Cyy = 0, Cxy = 0;
				for (const FVector& Point : Points)
				{
					const FVector D = Point - Mean;
					Cxx += D.X * D.X;
					Cyy += D.Y * D.Y;
					Cxy += D.X * D.Y;
				}
				const double Angle = 0.5 * FMath::Atan2(2.0 * Cxy, Cxx - Cyy);
				const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);

				double Min = TNumericLimits<double>::Max(), Max = -TNumericLimits<double>::Max();
				for (const FVector& Point : Points)
				{
					const double Along = FVector::DotProduct(Point - Mean, Direction);
					Min = FMath::Min(Min, Along);
					Max = FMath::Max(Max, Along);
				}
				AddPiece(Mean, Direction, Min, Max);
			}
		}

		double BestLength = 0.0;
		FVector BestStart = FVector::ZeroVector, BestDirection = FVector::ForwardVector;
		double BestZ = 0.0;
		for (const FRunway& Runway : Runways)
		{
			UE_LOG(LogTemp, Display, TEXT("Airport.PlaceFlight: runway candidate %.0f m, heading %.0f deg"), (Runway.Max - Runway.Min) / 100.0,
				FMath::RadiansToDegrees(FMath::Atan2(Runway.Direction.Y, Runway.Direction.X)));
			if (Runway.Max - Runway.Min > BestLength)
			{
				BestLength = Runway.Max - Runway.Min;
				BestDirection = Runway.Direction;
				BestStart = Runway.Origin + Runway.Direction * Runway.Min;
				BestZ = Runway.Z - 10.0;  // markings were lifted 10 cm above the asphalt
			}
		}
		if (BestLength < 150000.0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.PlaceFlight: no runway found (longest marking %.0f m)"), BestLength / 100.0);
			return;
		}

		// Route layout (FlightPath defaults): takeoff roll starts at local (45000, -9000), runway along +X
		const FRotator Rotation(0.0, FMath::RadiansToDegrees(FMath::Atan2(BestDirection.Y, BestDirection.X)), 0.0);
		const FVector TakeoffStart = BestStart + BestDirection * 30000.0;  // 300 m in from the runway end
		FVector Location = TakeoffStart - Rotation.RotateVector(FVector(45000.0, -9000.0, 0.0));
		Location.Z = BestZ;

		AFlightPath* Path = World->SpawnActor<AFlightPath>(AFlightPath::StaticClass(), FTransform(Rotation, Location));
		Path->SetActorLabel(TEXT("FlightPath_EHAM"));

		// Aircraft: single-mesh KLM 787-9, turned so its nose points along +X
		AAircraft* Plane = World->SpawnActor<AAircraft>(AAircraft::StaticClass(), FTransform(Rotation, Location));
		Plane->SetActorLabel(TEXT("Aircraft_KLM_787"));
		Plane->FlightPath = Path;
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Datasmith/Vliegtuigen/787-9_KLM/787-9_KLM/Geometries/Group_2.Group_2")))
		{
			int32 Axis = 1;
			double TailSign = 1.0;
			FBox Box;
			Plane->Mesh->SetStaticMesh(Mesh);
			Plane->Mesh->SetRelativeScale3D(FVector::OneVector);
			if (FindTail({ Mesh }, Axis, TailSign, Box))
			{
				const FVector Nose = Axis == 0 ? FVector(-TailSign, 0, 0) : FVector(0, -TailSign, 0);
				Plane->MeshRotation = FRotator(0.0, -FMath::RadiansToDegrees(FMath::Atan2(Nose.Y, Nose.X)), 0.0);
			}
		}

		UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.PlaceFlight: runway %.0f m long, heading %.0f deg, FlightPath at %s, mesh rotation %s, saved"),
			BestLength / 100.0, Rotation.Yaw, *Location.ToString(), *Plane->MeshRotation.ToString());
	}

	FAutoConsoleCommandWithWorldAndArgs PlaceFlightCommand(
		TEXT("Airport.PlaceFlight"),
		TEXT("Put a FlightPath and aircraft on the longest runway"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaceFlight));

	/** Removes every spline aircraft (and its FlightPath) from the open level, so Play no longer follows it with the camera. */
	void RemoveFlight(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->IsGameWorld())
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.RemoveFlight: run this in the editor, not during Play."));
			return;
		}
		TArray<AActor*> Remove;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsA<AAircraft>() || It->IsA<AFlightPath>())
			{
				Remove.Add(*It);
			}
		}
		for (AActor* Actor : Remove)
		{
			UE_LOG(LogTemp, Display, TEXT("Airport.RemoveFlight: removed %s"), *Actor->GetActorLabel());
			World->EditorDestroyActor(Actor, true);
		}
		UEditorLoadingAndSavingUtils::SavePackages({ World->GetOutermost() }, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.RemoveFlight: %d actors removed, level saved"), Remove.Num());
	}

	FAutoConsoleCommandWithWorldAndArgs RemoveFlightCommand(
		TEXT("Airport.RemoveFlight"),
		TEXT("Remove the spline aircraft and FlightPath from the open level (Airport.PlaceFlight puts one back)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RemoveFlight));

	/**
	 * Removes the hand-placed splines (BP_Spline) and the aircraft that follow them (BP_SplineObject) from the open level.
	 * An object that follows no BP_Spline stays. The two Blueprints stay in Content.
	 */
	void RemoveSplines(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->IsGameWorld())
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.RemoveSplines: run this in the editor, not during Play."));
			return;
		}
		TArray<AActor*> Splines;
		TArray<AActor*> Objects;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetClass()->GetName() == TEXT("BP_Spline_C"))
			{
				Splines.Add(*It);
			}
			else if (It->GetClass()->GetName() == TEXT("BP_SplineObject_C"))
			{
				Objects.Add(*It);
			}
		}

		TArray<AActor*> Remove = Splines;
		for (AActor* Object : Objects)
		{
			// Its spline to follow is a Blueprint variable: a reference to one of the spline actors or their spline component
			const AActor* Followed = nullptr;
			for (TFieldIterator<FObjectProperty> Property(Object->GetClass()); Property; ++Property)
			{
				const UObject* Value = Property->GetObjectPropertyValue_InContainer(Object);
				const UActorComponent* Component = Cast<UActorComponent>(Value);
				const AActor* Target = Component ? Component->GetOwner() : Cast<AActor>(Value);
				if (Splines.Contains(Target))
				{
					Followed = Target;
				}
			}
			if (Followed)
			{
				Remove.Add(Object);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("Airport.RemoveSplines: kept %s, it follows no BP_Spline"), *Object->GetActorLabel());
			}
		}
		for (AActor* Actor : Remove)
		{
			UE_LOG(LogTemp, Display, TEXT("Airport.RemoveSplines: removed %s at X=%.0f Y=%.0f m"), *Actor->GetActorLabel(),
				Actor->GetActorLocation().X / 100.0, Actor->GetActorLocation().Y / 100.0);
			World->EditorDestroyActor(Actor, true);
		}
		UEditorLoadingAndSavingUtils::SavePackages({ World->GetOutermost() }, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.RemoveSplines: %d splines and %d aircraft removed, level saved"), Splines.Num(), Remove.Num() - Splines.Num());
	}

	FAutoConsoleCommandWithWorldAndArgs RemoveSplinesCommand(
		TEXT("Airport.RemoveSplines"),
		TEXT("Remove the hand-placed BP_Spline paths and the BP_SplineObject aircraft that follow them from the open level"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RemoveSplines));

	FAutoConsoleCommandWithWorldAndArgs SwapPlanesCommand(
		TEXT("Airport.SwapPlanes"),
		TEXT("Replace the simple KLM/Emirates aircraft with the detailed models"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SwapPlanes));

	/** Logs every material instance in a folder: parent, blend, colour/texture parameters. */
	void ListMaterials(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			return;
		}

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(*Args[0]);
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		// Optional second argument: only instances whose name contains it (then all of them, not the first 40)
		const FString NameFilter = Args.Num() > 1 ? Args[1] : FString();
		const UEnum* BlendEnum = StaticEnum<EBlendMode>();
		for (int32 Index = 0, Listed = 0; Index < Assets.Num() && (Listed < 40 || !NameFilter.IsEmpty()); ++Index)
		{
			if (!NameFilter.IsEmpty() && !Assets[Index].AssetName.ToString().Contains(NameFilter))
			{
				continue;
			}
			const UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Assets[Index].GetAsset());
			if (!Instance)
			{
				continue;
			}
			++Listed;

			FString Params;
			for (const FVectorParameterValue& Value : Instance->VectorParameterValues)
			{
				Params += FString::Printf(TEXT(" %s=(%.2f,%.2f,%.2f)"), *Value.ParameterInfo.Name.ToString(), Value.ParameterValue.R, Value.ParameterValue.G, Value.ParameterValue.B);
			}
			for (const FScalarParameterValue& Value : Instance->ScalarParameterValues)
			{
				Params += FString::Printf(TEXT(" %s=%.2f"), *Value.ParameterInfo.Name.ToString(), Value.ParameterValue);
			}
			for (const FTextureParameterValue& Value : Instance->TextureParameterValues)
			{
				Params += FString::Printf(TEXT(" %s=%s"), *Value.ParameterInfo.Name.ToString(), Value.ParameterValue ? *Value.ParameterValue->GetName() : TEXT("none"));
			}
			UE_LOG(LogTemp, Display, TEXT("Airport.ListMaterials: %-28s parent=%-32s blend=%s%s"), *Instance->GetName(),
				Instance->Parent ? *Instance->Parent->GetName() : TEXT("-"), *BlendEnum->GetNameStringByValue(Instance->GetBlendMode()), *Params);
		}
		UE_LOG(LogTemp, Display, TEXT("Airport.ListMaterials: done (%d instances)"), Assets.Num());
	}

	/**
	 * The detailed KLM/Emirates models were imported without their livery textures: materials ask for a texture that
	 * does not exist and show the engine default. Give those materials a flat colour based on where their parts sit.
	 */
	void FixLiveries(const TArray<FString>& Args)
	{
		struct FModel { FString Folder; FLinearColor Body; FLinearColor Tail; };
		const FLinearColor KLMBlue(0.13f, 0.60f, 0.85f);    // same blue the model already uses (Material18)
		const FLinearColor White(0.85f, 0.85f, 0.85f);
		const TArray<FModel> Models = {
			{ TEXT("/Game/Datasmith/Vliegtuigen/KLM_Boeing_787_9_Dreamliner"), KLMBlue, KLMBlue },
			{ TEXT("/Game/Datasmith/Vliegtuigen/KLM_Boeing_777_306_ER"), KLMBlue, KLMBlue },
			{ TEXT("/Game/Datasmith/Vliegtuigen/Emirates_Airbus_A380_861_A6_EOI"), White, White },
		};
		const FLinearColor WingGrey(0.55f, 0.56f, 0.58f);
		const FLinearColor LightGrey(0.62f, 0.62f, 0.62f);
		const FLinearColor Dark(0.04f, 0.04f, 0.04f);

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<UPackage*> ToSave;
		for (const FModel& Model : Models)
		{
			TArray<FAssetData> MeshAssets;
			FARFilter MeshFilter;
			MeshFilter.PackagePaths.Add(*Model.Folder);
			MeshFilter.bRecursivePaths = true;
			MeshFilter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
			Registry.GetAssets(MeshFilter, MeshAssets);

			// Where each material is used, in model space
			FBox ModelBox(ForceInit);
			TMap<UMaterialInterface*, FBox> MaterialBoxes;
			for (const FAssetData& Asset : MeshAssets)
			{
				const UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
				if (!Mesh)
				{
					continue;
				}
				const FBox Box = Mesh->GetBoundingBox();
				ModelBox += Box;
				for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
				{
					if (Slot.MaterialInterface)
					{
						MaterialBoxes.FindOrAdd(Slot.MaterialInterface, FBox(ForceInit)) += Box;
					}
				}
			}
			const FVector ModelSize = ModelBox.GetSize();

			for (const TPair<UMaterialInterface*, FBox>& Pair : MaterialBoxes)
			{
				UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Pair.Key);
				if (!Instance)
				{
					continue;
				}

				float Weight = 0.f;
				UTexture* Texture = nullptr;
				Instance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DiffuseColorMapWeight")), Weight);
				const bool bHasTexture = Instance->TextureParameterValues.ContainsByPredicate([](const FTextureParameterValue& Value)
				{
					return Value.ParameterInfo.Name == TEXT("DiffuseColorMap") && Value.ParameterValue;
				});

				const FBox& Box = Pair.Value;
				const double Length = Box.GetSize().Y / ModelSize.Y;
				const double Width = Box.GetSize().X / ModelSize.X;
				const double Height = Box.GetSize().Z / ModelSize.Z;
				const double Top = (Box.Max.Z - ModelBox.Min.Z) / ModelSize.Z;

				FLinearColor Current(0, 0, 0);
				Instance->GetVectorParameterValue(FMaterialParameterInfo(TEXT("DiffuseColor")), Current);
				UE_LOG(LogTemp, Display, TEXT("Airport.FixLiveries: %-40s len=%.2f wid=%.2f hgt=%.2f top=%.2f weight=%.0f texture=%d color=(%.2f,%.2f,%.2f)"),
					*Instance->GetName(), Length, Width, Height, Top, Weight, bHasTexture ? 1 : 0, Current.R, Current.G, Current.B);

				if (Weight < 0.5f || bHasTexture)
				{
					continue;  // has a real colour or texture already
				}

				FLinearColor Color = LightGrey;
				FString Part = TEXT("other");
				if (Length > 0.6 && Width > 0.5)      { Color = Model.Body; Part = TEXT("whole plane"); }
				else if (Length > 0.6 && Width < 0.25) { Color = Model.Body; Part = TEXT("fuselage"); }
				else if (Top > 0.85 && Width < 0.2)    { Color = Model.Tail; Part = TEXT("fin"); }
				else if (Width > 0.5 && Height < 0.4)  { Color = WingGrey; Part = TEXT("wing"); }
				else if (Top < 0.2)                    { Color = Dark; Part = TEXT("gear"); }

				Instance->Modify();
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("DiffuseColorMapWeight")), 0.f);
				Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("DiffuseColor")), Color);
				Instance->PostEditChange();
				ToSave.Add(Instance->GetPackage());
				UE_LOG(LogTemp, Display, TEXT("Airport.FixLiveries:   -> %s (%.2f,%.2f,%.2f)"), *Part, Color.R, Color.G, Color.B);
			}
		}

		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.FixLiveries: %d materials fixed, saved"), ToSave.Num());
	}

	FAutoConsoleCommand FixLiveriesCommand(
		TEXT("Airport.FixLiveries"),
		TEXT("Flat colours for the detailed KLM/Emirates aircraft whose livery textures are missing"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&FixLiveries));

	/**
	 * Aircraft materials came in with SpecularColor 0 (no reflection at all), so white paint looks beige and blue looks
	 * teal under the warm sun, and sides turned away from the sun go almost black. Real aircraft paint is glossy.
	 */
	void GlossyPaint(const TArray<FString>& Args)
	{
		const float Shininess = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 40.f;
		const FLinearColor Specular(0.5f, 0.5f, 0.5f);

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game/Datasmith/Vliegtuigen"));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		TArray<UPackage*> ToSave;
		for (const FAssetData& Asset : Assets)
		{
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
			if (!Instance || !Instance->Parent || Instance->Parent->GetName() != TEXT("PhongSurfaceMaterial"))
			{
				continue;
			}

			const FString Name = Instance->GetName();
			if (Name.Contains(TEXT("Glass")) || Name.Contains(TEXT("ÑÑ‚ÐµÐºÐ»Ð¾")) || Name.Contains(TEXT("Translucent")) || Name.Contains(TEXT("Window")))
			{
				continue;  // glass has its own look
			}

			FLinearColor CurrentSpecular(0, 0, 0);
			float CurrentShininess = 0.f;
			Instance->GetVectorParameterValue(FMaterialParameterInfo(TEXT("SpecularColor")), CurrentSpecular);
			Instance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Shininess")), CurrentShininess);
			if (CurrentSpecular.GetLuminance() > 0.05f && CurrentShininess >= Shininess)
			{
				continue;  // already glossy
			}

			Instance->Modify();
			Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("SpecularColor")), Specular);
			Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Shininess")), Shininess);
			Instance->PostEditChange();
			ToSave.Add(Instance->GetPackage());
		}

		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.GlossyPaint: %d of %d aircraft materials now glossy (shininess %.0f), saved"), ToSave.Num(), Assets.Num(), Shininess);
	}

	/**
	 * PhongSurfaceMaterial (engine plugin, edited earlier) tints aircraft colours: white turns beige, KLM blue turns teal.
	 * Move the untextured aircraft materials to a clean project paint: model colour (sRGB) -> base colour, glossy.
	 */
	void AircraftPaint(const TArray<FString>& Args)
	{
		const FString Folder = TEXT("/Game/EHAM/Materials");
		UMaterial* Paint = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/M_AircraftPaint.M_AircraftPaint")));
		if (!Paint)
		{
			UMaterial* Base = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/M_TerrainVT.M_TerrainVT")));
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			Paint = Base ? Cast<UMaterial>(AssetTools.DuplicateAsset(TEXT("M_AircraftPaint"), Folder, Base)) : nullptr;
			if (!Paint)
			{
				UE_LOG(LogTemp, Error, TEXT("Airport.AircraftPaint: could not create M_AircraftPaint"));
				return;
			}
		}

		Paint->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Paint);

		UMaterialExpressionVectorParameter* Color = Cast<UMaterialExpressionVectorParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(Paint, UMaterialExpressionVectorParameter::StaticClass(), -600, 0));
		Color->ParameterName = TEXT("DiffuseColor");
		Color->DefaultValue = FLinearColor::White;

		// Imported colours are sRGB values (KLM blue 0.13/0.60/0.85), the base colour input is linear
		UMaterialExpressionPower* ToLinear = Cast<UMaterialExpressionPower>(
			UMaterialEditingLibrary::CreateMaterialExpression(Paint, UMaterialExpressionPower::StaticClass(), -350, 0));
		ToLinear->ConstExponent = 2.2f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Color, TEXT(""), ToLinear, TEXT("Base"));
		UMaterialEditingLibrary::ConnectMaterialProperty(ToLinear, TEXT(""), MP_BaseColor);

		UMaterialExpressionConstant* Specular = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(Paint, UMaterialExpressionConstant::StaticClass(), -350, 200));
		Specular->R = 0.5f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Specular, TEXT(""), MP_Specular);

		UMaterialExpressionScalarParameter* Roughness = Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(Paint, UMaterialExpressionScalarParameter::StaticClass(), -350, 300));
		Roughness->ParameterName = TEXT("Roughness");
		Roughness->DefaultValue = 0.35f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);

		Paint->BlendMode = BLEND_Opaque;
		Paint->bUsedWithNanite = true;
		UMaterialEditingLibrary::RecompileMaterial(Paint);
		TArray<UPackage*> ToSave = { Paint->GetPackage() };

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game/Datasmith/Vliegtuigen"));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		int32 Moved = 0;
		for (const FAssetData& Asset : Assets)
		{
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
			if (!Instance || !Instance->Parent || Instance->Parent->GetName() != TEXT("PhongSurfaceMaterial"))
			{
				continue;
			}

			const FString Name = Instance->GetName();
			if (Name.Contains(TEXT("Glass")) || Name.Contains(TEXT("ÑÑ‚ÐµÐºÐ»Ð¾")) || Name.Contains(TEXT("Translucent")) || Name.Contains(TEXT("Window")))
			{
				continue;
			}

			float Weight = 0.f;
			Instance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DiffuseColorMapWeight")), Weight);
			const bool bHasTexture = Instance->TextureParameterValues.ContainsByPredicate([](const FTextureParameterValue& Value)
			{
				return Value.ParameterInfo.Name == TEXT("DiffuseColorMap") && Value.ParameterValue;
			});
			if (Weight > 0.5f && bHasTexture)
			{
				continue;  // real livery texture: keep
			}

			Instance->Modify();
			Instance->SetParentEditorOnly(Paint);
			Instance->PostEditChange();
			ToSave.Add(Instance->GetPackage());
			++Moved;
		}

		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.AircraftPaint: %d aircraft materials now use M_AircraftPaint, saved"), Moved);
	}

	FAutoConsoleCommand AircraftPaintCommand(
		TEXT("Airport.AircraftPaint"),
		TEXT("Move untextured aircraft materials to the clean M_AircraftPaint"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&AircraftPaint));

	FAutoConsoleCommand GlossyPaintCommand(
		TEXT("Airport.GlossyPaint"),
		TEXT("Glossy paint on all aircraft. Usage: Airport.GlossyPaint [shininess, default 40]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&GlossyPaint));

	/** Which engine-plugin materials (/InterchangeAssets) does the project use, and from what kind of asset. */
	void ListEngineDeps(const TArray<FString>& Args)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> EngineAssets;
		Registry.GetAssetsByPath(TEXT("/InterchangeAssets/Materials"), EngineAssets, true);

		for (const FAssetData& EngineAsset : EngineAssets)
		{
			TArray<FName> Referencers;
			Registry.GetReferencers(EngineAsset.PackageName, Referencers);
			TMap<FString, int32> ByClass;
			for (const FName& Referencer : Referencers)
			{
				if (!Referencer.ToString().StartsWith(TEXT("/Game/")))
				{
					continue;
				}
				TArray<FAssetData> RefAssets;
				Registry.GetAssetsByPackageName(Referencer, RefAssets);
				const FString ClassName = RefAssets.Num() > 0 ? RefAssets[0].AssetClassPath.GetAssetName().ToString() : TEXT("?");
				ByClass.FindOrAdd(ClassName)++;
			}
			if (ByClass.Num() > 0)
			{
				FString Summary;
				for (const TPair<FString, int32>& Pair : ByClass)
				{
					Summary += FString::Printf(TEXT(" %s=%d"), *Pair.Key, Pair.Value);
				}
				UE_LOG(LogTemp, Display, TEXT("Airport.EngineDeps: %-40s used by%s"), *EngineAsset.AssetName.ToString(), *Summary);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Airport.EngineDeps: done"));
	}

	/**
	 * Copies every engine-plugin parent material the project uses into /Game/EHAM/Materials/Parents and reparents the
	 * project's instances to those copies. The project then works on any PC, the engine files stay untouched.
	 */
	void LocalizeMaterials(const TArray<FString>& Args)
	{
		const FString Target = TEXT("/Game/EHAM/Materials/Parents");
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		TMap<UMaterialInterface*, UMaterialInterface*> Copies;
		TArray<UPackage*> ToSave;
		int32 Moved = 0;
		for (const FAssetData& Asset : Assets)
		{
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
			UMaterialInterface* Parent = Instance ? Instance->Parent.Get() : nullptr;
			if (!Parent || !Parent->GetPathName().StartsWith(TEXT("/InterchangeAssets/")))
			{
				continue;
			}

			UMaterialInterface*& Copy = Copies.FindOrAdd(Parent);
			if (!Copy)
			{
				const FString CopyPath = Target / Parent->GetName() + TEXT(".") + Parent->GetName();
				Copy = LoadObject<UMaterialInterface>(nullptr, *CopyPath);
				if (!Copy)
				{
					Copy = Cast<UMaterialInterface>(AssetTools.DuplicateAsset(Parent->GetName(), Target, Parent));
					if (Copy)
					{
						ToSave.Add(Copy->GetPackage());
						UE_LOG(LogTemp, Display, TEXT("Airport.LocalizeMaterials: copied %s"), *Parent->GetName());
					}
				}
			}
			if (!Copy)
			{
				continue;
			}

			Instance->Modify();
			Instance->SetParentEditorOnly(Copy);
			Instance->PostEditChange();
			ToSave.Add(Instance->GetPackage());
			++Moved;
		}

		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.LocalizeMaterials: %d parents copied, %d instances moved into the project, saved"), Copies.Num(), Moved);
	}

	/** Builds (or rebuilds) a tree card material: leaf texture, alpha cut-out, two-sided, matte. */
	UMaterial* BuildTreeMaterial(const FString& Name, UTexture* SampleTexture)
	{
		const FString Folder = TEXT("/Game/EHAM/Materials");
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *(Folder / Name + TEXT(".") + Name));
		if (!Material)
		{
			UMaterial* Base = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/M_TerrainVT.M_TerrainVT")));
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			Material = Base ? Cast<UMaterial>(AssetTools.DuplicateAsset(Name, Folder, Base)) : nullptr;
			if (!Material)
			{
				return nullptr;
			}
		}

		Material->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);

		UMaterialExpressionTextureSampleParameter2D* Leaves = Cast<UMaterialExpressionTextureSampleParameter2D>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -600, 0));
		Leaves->ParameterName = TEXT("DiffuseColorMap");
		Leaves->Texture = SampleTexture;
		Leaves->SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture(SampleTexture);
		UMaterialEditingLibrary::ConnectMaterialProperty(Leaves, TEXT("RGB"), MP_BaseColor);

		// The tree textures have no real alpha: four tree sprites on a white background. Cut away the white,
		// using the blue channel: white has full blue, leaves and trunks have little.
		UMaterialExpression* NotBlue = UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionOneMinus::StaticClass(), -500, 250);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Leaves, TEXT("B"), NotBlue, TEXT(""));
		UMaterialExpressionSubtract* BelowWhite = Cast<UMaterialExpressionSubtract>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionSubtract::StaticClass(), -400, 250));
		BelowWhite->ConstB = 0.15f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(NotBlue, TEXT(""), BelowWhite, TEXT("A"));
		UMaterialExpressionMultiply* Sharpen = Cast<UMaterialExpressionMultiply>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -300, 250));
		Sharpen->ConstB = 8.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(BelowWhite, TEXT(""), Sharpen, TEXT("A"));
		UMaterialExpression* Key = UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionSaturate::StaticClass(), -200, 250);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Sharpen, TEXT(""), Key, TEXT(""));

		// Keep a real alpha too, for trees that do have one
		UMaterialExpressionMultiply* Mask = Cast<UMaterialExpressionMultiply>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -100, 250));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Key, TEXT(""), Mask, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Leaves, TEXT("A"), Mask, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Mask, TEXT(""), MP_OpacityMask);

		UMaterialExpressionConstant* Roughness = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -300, 250));
		Roughness->R = 0.85f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);

		UMaterialExpressionConstant* Specular = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -300, 350));
		Specular->R = 0.2f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Specular, TEXT(""), MP_Specular);

		Material->BlendMode = BLEND_Masked;
		Material->OpacityMaskClipValue = 0.4f;
		Material->TwoSided = true;
		Material->bUsedWithNanite = true;
		UMaterialEditingLibrary::RecompileMaterial(Material);
		return Material;
	}

	/** Tree cards rendered as opaque dark boards: give the tree instances a masked, two-sided material. */
	void FixTrees(const TArray<FString>& Args)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game/EHAM"));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		// Virtual and normal textures need different samplers: one material for each
		TArray<UMaterialInstanceConstant*> Normal, Virtual;
		UTexture* NormalSample = nullptr;
		UTexture* VirtualSample = nullptr;
		TArray<UPackage*> ToSave;
		UMaterial* Terrain = LoadObject<UMaterial>(nullptr, TEXT("/Game/EHAM/Materials/M_TerrainVT.M_TerrainVT"));
		for (const FAssetData& Asset : Assets)
		{
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset());

			// Case-sensitive: "Streets" also contains "tree". Undo an earlier run that caught the road materials.
			const bool bTree = Asset.AssetName.ToString().Contains(TEXT("Tree"), ESearchCase::CaseSensitive);
			if (!bTree)
			{
				if (Instance && Terrain && Instance->Parent && Instance->Parent->GetName().StartsWith(TEXT("M_TreeCards")))
				{
					Instance->Modify();
					Instance->SetParentEditorOnly(Terrain);
					Instance->PostEditChange();
					ToSave.Add(Instance->GetPackage());
					UE_LOG(LogTemp, Display, TEXT("Airport.FixTrees: %s back to M_TerrainVT"), *Instance->GetName());
				}
				continue;
			}

			UTexture* Texture = nullptr;
			if (!Instance || !Instance->GetTextureParameterValue(FMaterialParameterInfo(TEXT("DiffuseColorMap")), Texture) || !Texture)
			{
				continue;
			}

			// The import threw the leaf alpha away ("compress without alpha"): bring it back if the source has it
			UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
			const ETextureSourceFormat SourceFormat = Texture->Source.GetFormat();
			const bool bSourceHasAlpha = SourceFormat == TSF_BGRA8 || SourceFormat == TSF_RGBA16F || SourceFormat == TSF_RGBA16 || SourceFormat == TSF_RGBA32F;
			if (Texture2D && !Texture2D->HasAlphaChannel() && bSourceHasAlpha && Texture->CompressionNoAlpha)
			{
				Texture->Modify();
				Texture->CompressionNoAlpha = false;
				Texture->PostEditChange();
				ToSave.AddUnique(Texture->GetPackage());
			}
			UE_LOG(LogTemp, Display, TEXT("Airport.FixTrees: %s texture=%s vt=%d alpha=%d sourceAlpha=%d noAlphaFlag=%d"), *Instance->GetName(), *Texture->GetName(),
				Texture->VirtualTextureStreaming ? 1 : 0, Texture2D && Texture2D->HasAlphaChannel() ? 1 : 0, bSourceHasAlpha ? 1 : 0, Texture->CompressionNoAlpha ? 1 : 0);
			if (Texture->VirtualTextureStreaming)
			{
				Virtual.Add(Instance);
				VirtualSample = Texture;
			}
			else
			{
				Normal.Add(Instance);
				NormalSample = Texture;
			}
		}

		auto Apply = [&ToSave](UMaterial* Material, const TArray<UMaterialInstanceConstant*>& Instances)
		{
			if (!Material)
			{
				return;
			}
			ToSave.Add(Material->GetPackage());
			for (UMaterialInstanceConstant* Instance : Instances)
			{
				const UEnum* BlendEnum = StaticEnum<EBlendMode>();
				const FString Before = BlendEnum->GetNameStringByValue(Instance->GetBlendMode());

				Instance->Modify();
				Instance->SetParentEditorOnly(Material);
				// Interchange instances override blend mode/two-sided, which beats the parent: force the tree settings
				Instance->BasePropertyOverrides.bOverride_BlendMode = true;
				Instance->BasePropertyOverrides.BlendMode = BLEND_Masked;
				Instance->BasePropertyOverrides.bOverride_TwoSided = true;
				Instance->BasePropertyOverrides.TwoSided = true;
				Instance->BasePropertyOverrides.bOverride_OpacityMaskClipValue = true;
				Instance->BasePropertyOverrides.OpacityMaskClipValue = 0.4f;
				Instance->PostEditChange();
				ToSave.Add(Instance->GetPackage());

				UE_LOG(LogTemp, Display, TEXT("Airport.FixTrees: %s blend %s -> %s"), *Instance->GetName(), *Before,
					*BlendEnum->GetNameStringByValue(Instance->GetBlendMode()));
			}
		};
		if (NormalSample)
		{
			Apply(BuildTreeMaterial(TEXT("M_TreeCards"), NormalSample), Normal);
		}
		if (VirtualSample)
		{
			Apply(BuildTreeMaterial(TEXT("M_TreeCardsVT"), VirtualSample), Virtual);
		}

		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);

		// No virtual-texture trees: the VT variant from the earlier run is unused
		if (!VirtualSample)
		{
			if (UMaterial* Unused = LoadObject<UMaterial>(nullptr, TEXT("/Game/EHAM/Materials/M_TreeCardsVT.M_TreeCardsVT")))
			{
				ObjectTools::DeleteAssets({ Unused }, false);
				UE_LOG(LogTemp, Display, TEXT("Airport.FixTrees: deleted unused M_TreeCardsVT"));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Airport.FixTrees: %d tree materials fixed (%d normal, %d virtual), saved"), Normal.Num() + Virtual.Num(), Normal.Num(), Virtual.Num());
	}

	/**
	 * Puts the materials back to how they were on 13 Oct 10:51, before the last round of fixes: dark patches again
	 * (masked M_GroundOverlay), no grass grain, engine-plugin parents instead of the Parents copies, old tree cards.
	 * Afterwards delete /Game/EHAM/Materials/Parents and M_TreeCards*; the level comes from Saved/Backup_before_PlaceFlight.
	 */
	void UndoLastFixes()
	{
		const FString Folder = TEXT("/Game/EHAM/Materials");
		const FName DiffuseName(TEXT("DiffuseColorMap"));
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

		TArray<FAssetData> EngineAssets;
		Registry.GetAssetsByPath(TEXT("/InterchangeAssets"), EngineAssets, true);
		auto FindEngineParent = [&EngineAssets](const FString& Name) -> UMaterialInterface*
		{
			const FAssetData* Found = EngineAssets.FindByPredicate([&Name](const FAssetData& Asset) { return Asset.AssetName.ToString() == Name; });
			return Found ? Cast<UMaterialInterface>(Found->GetAsset()) : nullptr;
		};

		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		TArray<UPackage*> ToSave;
		int32 Localized = 0, Trees = 0, Patches = 0;
		for (const FAssetData& Asset : Assets)
		{
			// The copies themselves are deleted afterwards
			if (Asset.PackagePath.ToString().StartsWith(Folder / TEXT("Parents")))
			{
				continue;
			}
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
			if (!Instance || !Instance->Parent)
			{
				continue;
			}

			const FString ParentPath = Instance->Parent->GetPathName();
			if (ParentPath.StartsWith(Folder / TEXT("Parents/")))
			{
				// LocalizeMaterials: back to the engine-plugin parent with the same name
				UMaterialInterface* Original = FindEngineParent(Instance->Parent->GetName());
				if (!Original)
				{
					UE_LOG(LogTemp, Error, TEXT("Airport.UndoLastFixes: no engine parent %s for %s"), *Instance->Parent->GetName(), *Instance->GetName());
					continue;
				}
				Instance->Modify();
				Instance->SetParentEditorOnly(Original);
				++Localized;
			}
			else if (ParentPath.StartsWith(Folder / TEXT("M_TreeCards")))
			{
				// FixTrees: the Lambert parents from the first FixTrees log, without the forced masked/two-sided settings
				const FString Name = Instance->GetName();
				const TCHAR* ParentName = Name == TEXT("TreeLod1_02_summer_mat") ? TEXT("LambertSurfaceMaterial1")
					: Name.Contains(TEXT("TreeLod1_01")) ? TEXT("LambertSurfaceMaterialTree1")
					: Name.Contains(TEXT("TreeLod1_03")) ? TEXT("LambertSurfaceMaterialTree3")
					: TEXT("LambertSurfaceMaterialTree2");
				UMaterialInterface* Original = FindEngineParent(ParentName);
				if (!Original)
				{
					UE_LOG(LogTemp, Error, TEXT("Airport.UndoLastFixes: no engine parent %s for %s"), ParentName, *Name);
					continue;
				}
				Instance->Modify();
				Instance->SetParentEditorOnly(Original);
				Instance->BasePropertyOverrides = FMaterialInstanceBasePropertyOverrides();
				++Trees;
			}
			else if (Instance->Parent->GetName() == TEXT("M_GroundOverlay"))
			{
				// FixOverlays: drop the Strength value of the subtle version
				Instance->Modify();
				Instance->ScalarParameterValues.RemoveAll([](const FScalarParameterValue& Value) { return Value.ParameterInfo.Name == TEXT("Strength"); });
				++Patches;
			}
			else
			{
				continue;
			}
			Instance->PostEditChange();
			ToSave.Add(Instance->GetPackage());
		}

		// M_GroundOverlay as it was: texture -> base color, Opacity x texture alpha -> dithered mask (the dark patches)
		if (UMaterial* Overlay = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/M_GroundOverlay.M_GroundOverlay"))))
		{
			UTexture* SampleTexture = nullptr;
			Overlay->GetTextureParameterValue(FMaterialParameterInfo(DiffuseName), SampleTexture);
			Overlay->Modify();
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(Overlay);

			UMaterialExpressionTextureSampleParameter2D* Photo = Cast<UMaterialExpressionTextureSampleParameter2D>(
				UMaterialEditingLibrary::CreateMaterialExpression(Overlay, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -700, 0));
			Photo->ParameterName = DiffuseName;
			Photo->Texture = SampleTexture;
			Photo->SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture(SampleTexture);
			UMaterialEditingLibrary::ConnectMaterialProperty(Photo, TEXT("RGB"), MP_BaseColor);

			UMaterialExpressionScalarParameter* OpacityParam = Cast<UMaterialExpressionScalarParameter>(
				UMaterialEditingLibrary::CreateMaterialExpression(Overlay, UMaterialExpressionScalarParameter::StaticClass(), -700, 300));
			OpacityParam->ParameterName = TEXT("Opacity");
			OpacityParam->DefaultValue = 1.f;

			UMaterialExpressionMultiply* Fade = Cast<UMaterialExpressionMultiply>(
				UMaterialEditingLibrary::CreateMaterialExpression(Overlay, UMaterialExpressionMultiply::StaticClass(), -450, 300));
			UMaterialEditingLibrary::ConnectMaterialExpressions(OpacityParam, TEXT(""), Fade, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(Photo, TEXT("A"), Fade, TEXT("B"));

			UMaterialExpressionMaterialFunctionCall* Dither = Cast<UMaterialExpressionMaterialFunctionCall>(
				UMaterialEditingLibrary::CreateMaterialExpression(Overlay, UMaterialExpressionMaterialFunctionCall::StaticClass(), -250, 300));
			Dither->SetMaterialFunction(LoadObject<UMaterialFunctionInterface>(nullptr,
				TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA.DitherTemporalAA")));
			UMaterialEditingLibrary::ConnectMaterialExpressions(Fade, TEXT(""), Dither, TEXT("Alpha Threshold"));
			UMaterialEditingLibrary::ConnectMaterialProperty(Dither, TEXT("Result"), MP_OpacityMask);

			UMaterialExpressionConstant* Roughness = Cast<UMaterialExpressionConstant>(
				UMaterialEditingLibrary::CreateMaterialExpression(Overlay, UMaterialExpressionConstant::StaticClass(), -250, 500));
			Roughness->R = 1.f;
			UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);

			UMaterialExpressionConstant* Specular = Cast<UMaterialExpressionConstant>(
				UMaterialEditingLibrary::CreateMaterialExpression(Overlay, UMaterialExpressionConstant::StaticClass(), -250, 600));
			Specular->R = 0.25f;
			UMaterialEditingLibrary::ConnectMaterialProperty(Specular, TEXT(""), MP_Specular);

			Overlay->BlendMode = BLEND_Masked;
			Overlay->SetShadingModel(MSM_DefaultLit);
			Overlay->bUsedWithNanite = true;
			UMaterialEditingLibrary::RecompileMaterial(Overlay);
			ToSave.Add(Overlay->GetPackage());
		}

		// M_TerrainVT as it was: aerial photo straight to base color, no grass grain
		if (UMaterial* TerrainMaterial = LoadObject<UMaterial>(nullptr, *(Folder + TEXT("/M_TerrainVT.M_TerrainVT"))))
		{
			UTexture* SampleTexture = nullptr;
			TerrainMaterial->GetTextureParameterValue(FMaterialParameterInfo(DiffuseName), SampleTexture);
			TerrainMaterial->Modify();
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(TerrainMaterial);

			UMaterialExpressionTextureSampleParameter2D* Photo = Cast<UMaterialExpressionTextureSampleParameter2D>(
				UMaterialEditingLibrary::CreateMaterialExpression(TerrainMaterial, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -500, 0));
			Photo->ParameterName = DiffuseName;
			Photo->Texture = SampleTexture;
			Photo->SamplerType = SampleTexture->SRGB ? SAMPLERTYPE_VirtualColor : SAMPLERTYPE_VirtualLinearColor;
			UMaterialEditingLibrary::ConnectMaterialProperty(Photo, TEXT("RGB"), MP_BaseColor);

			UMaterialExpressionConstant* Roughness = Cast<UMaterialExpressionConstant>(
				UMaterialEditingLibrary::CreateMaterialExpression(TerrainMaterial, UMaterialExpressionConstant::StaticClass(), -300, 250));
			Roughness->R = 1.f;
			UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);

			UMaterialExpressionConstant* Specular = Cast<UMaterialExpressionConstant>(
				UMaterialEditingLibrary::CreateMaterialExpression(TerrainMaterial, UMaterialExpressionConstant::StaticClass(), -300, 350));
			Specular->R = 0.2f;
			UMaterialEditingLibrary::ConnectMaterialProperty(Specular, TEXT(""), MP_Specular);

			TerrainMaterial->bUsedWithNanite = true;
			UMaterialEditingLibrary::RecompileMaterial(TerrainMaterial);
			ToSave.Add(TerrainMaterial->GetPackage());
		}

		UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.UndoLastFixes: %d back to engine parents, %d trees, %d patches, M_GroundOverlay and M_TerrainVT rebuilt, saved"),
			Localized, Trees, Patches);
	}

	/** Saves the source image of a texture as PNG in Saved/Exports, to look at it. */
	void ExportTexture(const TArray<FString>& Args)
	{
		UTexture* Texture = Args.Num() > 0 ? LoadObject<UTexture>(nullptr, *Args[0]) : nullptr;
		FImage Image;
		if (!Texture || !Texture->Source.GetMipImage(Image, 0))
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.ExportTexture: could not read %s"), Args.Num() > 0 ? *Args[0] : TEXT(""));
			return;
		}
		const FString File = FPaths::ProjectSavedDir() / TEXT("Exports") / Texture->GetName() + TEXT(".png");
		FImageUtils::SaveImageByExtension(*File, Image);
		UE_LOG(LogTemp, Display, TEXT("Airport.ExportTexture: %s (%dx%d) -> %s"), *Texture->GetName(), Image.SizeX, Image.SizeY, *File);
	}

	/** Logs a texture's colour adjustments and compression, which can change how it renders vs. the source. */
	void TextureInfo(const TArray<FString>& Args)
	{
		const UTexture* Texture = Args.Num() > 0 ? LoadObject<UTexture>(nullptr, *Args[0]) : nullptr;
		if (!Texture)
		{
			return;
		}
		const UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
		UE_LOG(LogTemp, Display, TEXT("Airport.TextureInfo: %s sRGB=%d compression=%d noAlpha=%d brightness=%.2f brightnessCurve=%.2f vibrance=%.2f saturation=%.2f rgbCurve=%.2f hue=%.2f minAlpha=%.2f maxAlpha=%.2f lodGroup=%d maxSize=%d pixelFormat=%d"),
			*Texture->GetName(), Texture->SRGB ? 1 : 0, static_cast<int32>(Texture->CompressionSettings), Texture->CompressionNoAlpha ? 1 : 0,
			Texture->AdjustBrightness, Texture->AdjustBrightnessCurve, Texture->AdjustVibrance, Texture->AdjustSaturation, Texture->AdjustRGBCurve,
			Texture->AdjustHue, Texture->AdjustMinAlpha, Texture->AdjustMaxAlpha, static_cast<int32>(Texture->LODGroup), Texture->MaxTextureSize,
			Texture2D ? static_cast<int32>(Texture2D->GetPixelFormat()) : -1);
	}

	FAutoConsoleCommand TextureInfoCommand(
		TEXT("Airport.TextureInfo"),
		TEXT("Airport.TextureInfo /Game/Path/Texture.Texture"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&TextureInfo));

	FAutoConsoleCommand ExportTextureCommand(
		TEXT("Airport.ExportTexture"),
		TEXT("Airport.ExportTexture /Game/Path/Texture.Texture"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExportTexture));

	FAutoConsoleCommand UndoLastFixesCommand(
		TEXT("Airport.UndoLastFixes"),
		TEXT("Materials back to 13 Oct 10:51 (before the last round of fixes)"),
		FConsoleCommandDelegate::CreateStatic(&UndoLastFixes));

	FAutoConsoleCommand FixTreesCommand(
		TEXT("Airport.FixTrees"),
		TEXT("Masked two-sided material for the tree cards"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&FixTrees));

	FAutoConsoleCommand LocalizeMaterialsCommand(
		TEXT("Airport.LocalizeMaterials"),
		TEXT("Copy engine-plugin parent materials into the project and reparent to them"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&LocalizeMaterials));

	FAutoConsoleCommand ListEngineDepsCommand(
		TEXT("Airport.EngineDeps"),
		TEXT("List engine plugin materials the project depends on"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ListEngineDeps));

	FAutoConsoleCommand ListMaterialsCommand(
		TEXT("Airport.ListMaterials"),
		TEXT("Airport.ListMaterials /Game/Folder"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ListMaterials));

	/** Logs a parent material's default texture parameters (a non-white default tints untextured instances). */
	void InspectParent(const TArray<FString>& Args)
	{
		const UMaterial* Material = Args.Num() > 0 ? LoadObject<UMaterial>(nullptr, *Args[0]) : nullptr;
		if (!Material)
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.InspectParent: not found"));
			return;
		}
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (const UMaterialExpressionTextureSampleParameter* Texture = Cast<UMaterialExpressionTextureSampleParameter>(Expression))
			{
				UE_LOG(LogTemp, Display, TEXT("Airport.InspectParent: texture param %s default=%s"), *Texture->ParameterName.ToString(),
					Texture->Texture ? *Texture->Texture->GetPathName() : TEXT("none"));
			}
			else if (const UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
			{
				UE_LOG(LogTemp, Display, TEXT("Airport.InspectParent: vector param %s default=%s"), *Vector->ParameterName.ToString(), *Vector->DefaultValue.ToString());
			}
			else if (const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
			{
				UE_LOG(LogTemp, Display, TEXT("Airport.InspectParent: scalar param %s default=%.2f"), *Scalar->ParameterName.ToString(), Scalar->DefaultValue);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Airport.InspectParent: done"));
	}

	FAutoConsoleCommand InspectParentCommand(
		TEXT("Airport.InspectParent"),
		TEXT("Airport.InspectParent /Path/To/Material.Material"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&InspectParent));

	FAutoConsoleCommand InspectModelCommand(
		TEXT("Airport.InspectModel"),
		TEXT("Airport.InspectModel /Game/Folder"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&InspectModel));

	FAutoConsoleCommand FixTerrainCommand(
		TEXT("Airport.FixTerrain"),
		TEXT("Give the virtual texture terrain materials a parent that can read virtual textures"),
		FConsoleCommandDelegate::CreateStatic(&FixTerrain));

	FAutoConsoleCommand FixDecalMaterialsCommand(
		TEXT("Airport.FixDecalMaterials"),
		TEXT("Recreate missing decal parent materials"),
		FConsoleCommandDelegate::CreateStatic(&FixDecalMaterials));
#endif

	/** Lighter shadows: stronger sky light that captures the scene live. Saves the level. */
	void SoftenShadows(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->WorldType != EWorldType::Editor)
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.SoftenShadows: run this in the editor, not during Play."));
			return;
		}

		const float Intensity = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 2.f;
		const UEnum* MobilityEnum = StaticEnum<EComponentMobility::Type>();

		// Without static lighting only Movable lights do anything, so the sun must be Movable too
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent());
			UE_LOG(LogTemp, Display, TEXT("Airport.SoftenShadows: sun %s was mobility=%s intensity=%.1f"),
				*It->GetActorNameOrLabel(), *MobilityEnum->GetNameStringByValue(Sun->Mobility), Sun->Intensity);
			It->Modify();
			Sun->Modify();
			Sun->SetMobility(EComponentMobility::Movable);
		}

		int32 Count = 0;
		for (TActorIterator<ASkyLight> It(World); It; ++It)
		{
			USkyLightComponent* Sky = It->GetLightComponent();
			UE_LOG(LogTemp, Display, TEXT("Airport.SoftenShadows: sky %s was mobility=%s intensity=%.1f realtime=%d visible=%d source=%d"),
				*It->GetActorNameOrLabel(), *MobilityEnum->GetNameStringByValue(Sky->Mobility), Sky->Intensity,
				Sky->bRealTimeCapture ? 1 : 0, Sky->IsVisible() ? 1 : 0, static_cast<int32>(Sky->SourceType));
			It->Modify();
			Sky->Modify();
			UE_LOG(LogTemp, Display, TEXT("Airport.SoftenShadows: sky indirect=%.2f lowerBlack=%d affectsWorld=%d"),
				Sky->IndirectLightingIntensity, Sky->bLowerHemisphereIsBlack ? 1 : 0, Sky->bAffectsWorld ? 1 : 0);
			Sky->SetMobility(EComponentMobility::Movable);
			Sky->SetVisibility(true);
			Sky->bAffectsWorld = true;
			Sky->IndirectLightingIntensity = 1.f;
			Sky->bLowerHemisphereIsBlack = false;
			Sky->SourceType = ESkyLightSourceType::SLS_CapturedScene;
			Sky->bRealTimeCapture = true;
			Sky->SetIntensity(Intensity);
			Sky->RecaptureSky();
			++Count;
		}

		UE_LOG(LogTemp, Display, TEXT("Airport.SoftenShadows: %d sky light(s) set to intensity %.1f"), Count, Intensity);

		// Bounce light (GI was off, that is why shadows are pitch black) and lighter ambient occlusion
		int32 VolumeCount = 0;
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			FPostProcessSettings& Settings = It->Settings;
			UE_LOG(LogTemp, Display, TEXT("Airport.SoftenShadows: volume %s unbound=%d exposureBias(%d)=%.2f minEV(%d)=%.2f maxEV(%d)=%.2f indirect(%d)=%.2f ao(%d)=%.2f gi(%d)=%d"),
				*It->GetActorNameOrLabel(), It->bUnbound ? 1 : 0,
				Settings.bOverride_AutoExposureBias, Settings.AutoExposureBias,
				Settings.bOverride_AutoExposureMinBrightness, Settings.AutoExposureMinBrightness,
				Settings.bOverride_AutoExposureMaxBrightness, Settings.AutoExposureMaxBrightness,
				Settings.bOverride_IndirectLightingIntensity, Settings.IndirectLightingIntensity,
				Settings.bOverride_AmbientOcclusionIntensity, Settings.AmbientOcclusionIntensity,
				Settings.bOverride_DynamicGlobalIlluminationMethod, static_cast<int32>(Settings.DynamicGlobalIlluminationMethod.GetValue()));

			It->Modify();
			It->bUnbound = true;  // was a box: only worked with the camera inside it
			Settings.bOverride_DynamicGlobalIlluminationMethod = true;
			Settings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::ScreenSpace;
			Settings.bOverride_IndirectLightingIntensity = true;
			Settings.IndirectLightingIntensity = 1.f;
			Settings.bOverride_AmbientOcclusionIntensity = true;
			Settings.AmbientOcclusionIntensity = 0.4f;
			++VolumeCount;
		}
		if (VolumeCount == 0)
		{
			// No volume in the level: add one that covers the whole map
			APostProcessVolume* Volume = World->SpawnActor<APostProcessVolume>();
			Volume->bUnbound = true;
			Volume->Settings.bOverride_DynamicGlobalIlluminationMethod = true;
			Volume->Settings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::ScreenSpace;
			Volume->Settings.bOverride_AmbientOcclusionIntensity = true;
			Volume->Settings.AmbientOcclusionIntensity = 0.4f;
			UE_LOG(LogTemp, Display, TEXT("Airport.SoftenShadows: no post process volume found, added one"));
		}
		UE_LOG(LogTemp, Display, TEXT("Airport.SoftenShadows: %d post process volume(s) updated"), VolumeCount);
#if WITH_EDITOR
		UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, false);
		UE_LOG(LogTemp, Display, TEXT("Airport.SoftenShadows: level saved"));
#endif
	}

	FAutoConsoleCommandWithWorldAndArgs SoftenShadowsCommand(
		TEXT("Airport.SoftenShadows"),
		TEXT("Lighter shadows. Usage: Airport.SoftenShadows [sky light intensity, default 2]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SoftenShadows));

	/** Play mode only: flies the camera to a few views over the airport, saves a screenshot of each, then quits. */
	void TakeShots(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		if (!World || !World->IsGameWorld() || !PlayerController)
		{
			UE_LOG(LogTemp, Warning, TEXT("Airport.Shots: run this during Play."));
			return;
		}

		// The markings show where the airport is
		FBox Area(ForceInit);
		TArray<FVector> Markings;
		TArray<FVector> Patches;  // touchdown zones and gate stands
		TArray<FBox> Planes;      // parked aircraft (Datasmith "Vliegtuigen")
		TArray<FVector> Trees;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (Trees.Num() < 200)
			{
				TInlineComponentArray<UStaticMeshComponent*> TreeComponents(*It);
				for (const UStaticMeshComponent* Component : TreeComponents)
				{
					const UMaterialInterface* Material = Component->GetMaterial(0);
					if (Material && Material->GetName().Contains(TEXT("Tree")))
					{
						Trees.Add(Component->Bounds.Origin);
						break;
					}
				}
			}

			TInlineComponentArray<UMeshComponent*> Meshes(*It);
			for (const UMeshComponent* Mesh : Meshes)
			{
				const UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Mesh);
				const USkinnedMeshComponent* SkinnedMesh = Cast<USkinnedMeshComponent>(Mesh);
				const UObject* Asset = StaticMesh ? static_cast<const UObject*>(StaticMesh->GetStaticMesh())
					: SkinnedMesh ? static_cast<const UObject*>(SkinnedMesh->GetSkinnedAsset()) : nullptr;
				const bool bOnlyNew = Args.Num() > 1 && Args[1] == TEXT("hq");
				const FString Path = Asset ? Asset->GetPathName() : FString();
				const bool bNewModel = Path.Contains(TEXT("Emirates_Airbus_A380_861")) || Path.Contains(TEXT("KLM_Boeing_"));
				if (Path.Contains(TEXT("/Vliegtuigen/")) && Mesh->IsVisible() && (!bOnlyNew || bNewModel))
				{
					Planes.Add(It->GetComponentsBoundingBox());
					break;
				}
			}

			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				for (const UMaterialInterface* Material : Component->GetMaterials())
				{
					const FString Name = Material ? Material->GetName() : FString();
					if (Name.Contains(TEXT("RWY_Dark")) || Name.Contains(TEXT("AsphaltDark")) || Name.Contains(TEXT("ParkingPos")))
					{
						Patches.Add(Component->Bounds.Origin);
						break;
					}
				}

				if (GetMarkingLayer(Component) == 2)
				{
					Area += Component->Bounds.GetBox();
					Markings.Add(Component->Bounds.Origin);
					break;
				}
			}
		}
		const FVector Center = Area.IsValid ? Area.GetCenter() : FVector::ZeroVector;
		const double Size = Area.IsValid ? Area.GetExtent().Size2D() : 100000.0;

		struct FView { FVector Location; FVector Target; };
		TArray<FView> Views;

		const FString Mode = Args.Num() > 1 ? Args[1] : FString();
		if (Mode == TEXT("trees"))
		{
			for (int32 Index = 0; Index < 4 && Index < Trees.Num(); ++Index)
			{
				const FVector Spot = Trees[(Trees.Num() * Index) / 4];
				Views.Add({ Spot + FVector(-2500.0, -2500.0, 900.0), Spot });
			}
			UE_LOG(LogTemp, Display, TEXT("Airport.Shots: %d trees found, %d views"), Trees.Num(), Views.Num());
		}
		else if (Mode == TEXT("perf"))
		{
			// Typical views to measure frame time in: overview, apron, gates
			Views.Add({ Center + FVector(0.25 * Size, -0.35 * Size, 0.12 * Size), Center });
			for (int32 Index = 0; Index < 3 && Planes.Num() > 0; ++Index)
			{
				const FBox& Box = Planes[(Planes.Num() * Index) / 3];
				Views.Add({ Box.GetCenter() + FVector(4500.0, 2700.0, 2000.0), Box.GetCenter() });
			}
		}
		// "Airport.Shots 60 runways": the runway numbers at the thresholds, from close by
		else if (Mode == TEXT("runways"))
		{
			for (TActorIterator<AActor> It(World); It && Views.Num() < 8; ++It)
			{
				TInlineComponentArray<UStaticMeshComponent*> Components(*It);
				for (const UStaticMeshComponent* Component : Components)
				{
					bool bNumbers = false;
					for (const UMaterialInterface* Material : Component->GetMaterials())
					{
						bNumbers |= Material && Material->GetName().Contains(TEXT("RunwayLetters"));
					}
					if (bNumbers && Component->Bounds.BoxExtent.Size2D() < 5000.0)
					{
						const FVector Spot = Component->Bounds.Origin;
						Views.Add({ Spot + FVector(-4000.0, -4000.0, 3000.0), Spot });
						Views.Add({ Spot + FVector(0.0, 0.0, 6000.0) + FVector(1.0, 1.0, 0.0), Spot });   // straight down
					}
				}
			}
			UE_LOG(LogTemp, Display, TEXT("Airport.Shots: %d runway number views"), Views.Num());
		}
		// "Airport.Shots 60 planes": only close-ups of parked aircraft, from both sides
		else if (Mode == TEXT("planes") || Mode == TEXT("hq"))
		{
			for (int32 Index = 0; Index < 4 && Index < Planes.Num(); ++Index)
			{
				const FBox& Box = Planes[(Planes.Num() * Index) / 4];
				const FVector Target = Box.GetCenter();
				const double Distance = 4500.0;  // about 45 m, close enough to see shading on a widebody
				Views.Add({ Target + FVector(Distance, Distance * 0.6, Distance * 0.45), Target });
				Views.Add({ Target + FVector(-Distance, -Distance * 0.6, Distance * 0.3), Target });
			}
			UE_LOG(LogTemp, Display, TEXT("Airport.Shots: %d planes found, %d views"), Planes.Num(), Views.Num());
		}
		else
		{
		Views.Add({ Center + FVector(-0.8 * Size, -0.8 * Size, 0.5 * Size), Center });
		Views.Add({ Center + FVector(0.25 * Size, -0.35 * Size, 0.12 * Size), Center });
		for (int32 Index = 1; Index <= 4 && Markings.Num() > 0; ++Index)
		{
			const FVector Spot = Markings[(Markings.Num() * Index) / 5];
			Views.Add({ Spot + FVector(-6000.0, -6000.0, 2500.0), Spot });
		}
		for (int32 Index = 1; Index <= 3 && Patches.Num() > 0; ++Index)
		{
			const FVector Spot = Patches[(Patches.Num() * Index) / 4];
			Views.Add({ Spot + FVector(-9000.0, -9000.0, 6000.0), Spot });
		}
		}

		const float FirstDelay = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 60.f;
		const FString Prefix = Args.Num() > 2 ? Args[2] : TEXT("Airport");
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("Screenshots/Airport") / Prefix;
		GEngine->Exec(World, TEXT("DisableAllScreenMessages"));

		// One timer step per view: move camera, wait, shoot
		TSharedRef<int32> Step = MakeShared<int32>(0);
		FTimerHandle Handle;
		const bool bPerf = Mode == TEXT("perf");
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([World, Views, Folder, Step, bPerf]()
		{
			APlayerController* Controller = World->GetFirstPlayerController();
			const int32 ViewIndex = *Step / 2;
			if (bPerf && *Step == 0)
			{
				GEngine->Exec(World, TEXT("CsvProfile Frames=900"));  // frame times while flying over the views
			}
			if (ViewIndex >= Views.Num())
			{
				UE_LOG(LogTemp, Display, TEXT("Airport.Shots: done"));
				FPlatformMisc::RequestExit(false);
				return;
			}

			const FView& View = Views[ViewIndex];
			if (*Step % 2 == 0)
			{
				if (APawn* Pawn = Controller->GetPawn())
				{
					Pawn->SetActorLocation(View.Location);
				}
				Controller->SetControlRotation((View.Target - View.Location).Rotation());
			}
			else
			{
				// What is in front of the camera: meshes around the target, with their materials
				const UEnum* BlendEnum = StaticEnum<EBlendMode>();
				int32 Logged = 0;
				for (TActorIterator<AActor> It(World); It && Logged < 12; ++It)
				{
					TInlineComponentArray<UStaticMeshComponent*> Components(*It);
					for (const UStaticMeshComponent* Component : Components)
					{
						if (!Component->IsVisible() || !Component->GetStaticMesh() || FVector::Dist(Component->Bounds.Origin, View.Target) > 4000.0)
						{
							continue;
						}
						FString Slots;
						for (const UMaterialInterface* Material : Component->GetMaterials())
						{
							const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
							Slots += FString::Printf(TEXT(" [%s<%s %s]"), Material ? *Material->GetName() : TEXT("none"),
								Instance && Instance->Parent ? *Instance->Parent->GetName() : TEXT("-"),
								Material ? *BlendEnum->GetNameStringByValue(Material->GetBlendMode()) : TEXT("-"));
						}
						UE_LOG(LogTemp, Display, TEXT("Airport.Shots: view %d near: %s nanite=%d%s"), ViewIndex, *Component->GetStaticMesh()->GetName(),
							Component->GetStaticMesh()->HasValidNaniteData() ? 1 : 0, *Slots);
						++Logged;
					}
				}

				const FString File = Folder / FString::Printf(TEXT("Airport_%d.png"), ViewIndex);
				FScreenshotRequest::RequestScreenshot(File, false, false);
				UE_LOG(LogTemp, Display, TEXT("Airport.Shots: %s"), *File);
			}
			++(*Step);
		}), 8.f, true, FirstDelay);
	}

	/** Logs every material used on flat ground meshes (blend mode, parent, texture), most used first. */
	void ListGroundMaterials(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		TMap<const UMaterialInterface*, int32> Counts;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				const FBox Box = Component->Bounds.GetBox();
				if (Box.GetSize().Z > 100.0 || Box.GetSize().Size2D() < 500.0)
				{
					continue;  // only flat things lying on the ground
				}
				for (const UMaterialInterface* Material : Component->GetMaterials())
				{
					if (Material)
					{
						Counts.FindOrAdd(Material)++;
					}
				}
			}
		}

		Counts.ValueSort(TGreater<int32>());
		const UEnum* BlendEnum = StaticEnum<EBlendMode>();
		for (const TPair<const UMaterialInterface*, int32>& Pair : Counts)
		{
			const UMaterialInterface* Material = Pair.Key;
			const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
			UTexture* Texture = nullptr;
			Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("DiffuseColorMap")), Texture);
			UE_LOG(LogTemp, Display, TEXT("Airport.ListGround: %4d x %-45s blend=%-16s parent=%-40s texture=%s vt=%d"),
				Pair.Value, *Material->GetName(), *BlendEnum->GetNameStringByValue(Material->GetBlendMode()),
				Instance && Instance->Parent ? *Instance->Parent->GetName() : TEXT("-"),
				Texture ? *Texture->GetName() : TEXT("-"), Texture && Texture->VirtualTextureStreaming ? 1 : 0);
		}
		UE_LOG(LogTemp, Display, TEXT("Airport.ListGround: done, %d materials"), Counts.Num());
	}

	/** Logs the meshes and all their material slots for components that have a tree material. */
	void ListTrees(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSet<const UStaticMesh*> Seen;
		const UEnum* BlendEnum = StaticEnum<EBlendMode>();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				const UStaticMesh* Mesh = Component->GetStaticMesh();
				if (!Mesh || Seen.Contains(Mesh))
				{
					continue;
				}
				bool bTree = false;
				for (const UMaterialInterface* Material : Component->GetMaterials())
				{
					bTree |= Material && Material->GetName().Contains(TEXT("Tree"), ESearchCase::CaseSensitive);
				}
				if (!bTree)
				{
					continue;
				}
				Seen.Add(Mesh);

				FString Slots;
				for (const UMaterialInterface* Material : Component->GetMaterials())
				{
					const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
					Slots += FString::Printf(TEXT(" [%s parent=%s blend=%s]"), Material ? *Material->GetName() : TEXT("none"),
						Instance && Instance->Parent ? *Instance->Parent->GetName() : TEXT("-"),
						Material ? *BlendEnum->GetNameStringByValue(Material->GetBlendMode()) : TEXT("-"));
				}
				UE_LOG(LogTemp, Display, TEXT("Airport.ListTrees: %s nanite=%d size=%s%s"), *Mesh->GetName(), Mesh->HasValidNaniteData() ? 1 : 0,
					*Mesh->GetBoundingBox().GetSize().ToString(), *Slots);
				if (Seen.Num() >= 15)
				{
					UE_LOG(LogTemp, Display, TEXT("Airport.ListTrees: done"));
					return;
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Airport.ListTrees: done"));
	}

	FAutoConsoleCommandWithWorldAndArgs ListTreesCommand(
		TEXT("Airport.ListTrees"),
		TEXT("Log tree meshes and their material slots"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ListTrees));

	FAutoConsoleCommandWithWorldAndArgs ListGroundCommand(
		TEXT("Airport.ListGround"),
		TEXT("Log materials used on flat ground meshes"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ListGroundMaterials));

	FAutoConsoleCommandWithWorldAndArgs ShotsCommand(
		TEXT("Airport.Shots"),
		TEXT("Play mode: screenshots of the airport. Usage: Airport.Shots [seconds to wait first]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TakeShots));

	FAutoConsoleCommandWithWorldAndArgs RaiseMarkingsCommand(
		TEXT("Airport.RaiseMarkings"),
		TEXT("Lift airport markings above the asphalt. Usage: Airport.RaiseMarkings [cm, default 5]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RaiseMarkings));
}
