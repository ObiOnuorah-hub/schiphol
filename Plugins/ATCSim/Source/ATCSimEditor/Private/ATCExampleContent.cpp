// ATC.CreateExampleContent: builds every example asset and the showcase map from code, so the content can always
// be regenerated. Run it in the editor (Output Log > Cmd) or headless with -ExecCmds.

#include "ATCExampleContent.h"
#include "ATCNiagaraTools.h"
#include "ATCTaxiLights.h"
#include "AircraftMovementComponent.h"
#include "AircraftTypeData.h"
#include "AirportData.h"
#include "AirportSubsystem.h"
#include "ATCAircraft.h"
#include "ATCUnits.h"
#include "EnvironmentTypes.h"
#include "GMICamera.h"
#include "GMICameraSubsystem.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "LensSetData.h"
#include "LightingRigComponent.h"
#include "MonitorDisplay.h"
#include "PTZControllerComponent.h"
#include "ScenarioData.h"
#include "Stand.h"
#include "TaxiRoute.h"
#include "TrafficMixData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionEyeAdaptationInverse.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialParameterCollection.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "StaticMeshAttributes.h"

namespace ATCExample
{
	TArray<UPackage*> Dirty;

	FGameplayTag Tag(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), true);
	}

	/** Loads the asset if it exists, otherwise creates it; either way it is marked for saving. */
	template<typename T>
	T* GetOrCreate(const FString& Folder, const FString& Name)
	{
		const FString PackageName = Folder / Name;
		T* Asset = nullptr;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			Asset = LoadObject<T>(nullptr, *(PackageName + TEXT(".") + Name));
		}
		if (!Asset)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Asset = NewObject<T>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Asset);
		}
		Asset->Modify();
		Asset->MarkPackageDirty();
		Dirty.AddUnique(Asset->GetPackage());
		return Asset;
	}

	UClass* MakeBlueprint(UClass* Parent, const FString& Folder, const FString& Name)
	{
		const FString PackageName = Folder / Name;
		UBlueprint* Blueprint = FPackageName::DoesPackageExist(PackageName) ? LoadObject<UBlueprint>(nullptr, *(PackageName + TEXT(".") + Name)) : nullptr;
		if (!Blueprint)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(*Name), BPTYPE_Normal, UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass());
			FAssetRegistryModule::AssetCreated(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			Blueprint->MarkPackageDirty();
			Dirty.AddUnique(Package);
		}
		return Blueprint->GeneratedClass;
	}

	// ---------------------------------------------------------------- Materials

	UMaterialParameterCollection* MakeEnvironmentParameters()
	{
		UMaterialParameterCollection* Collection = GetOrCreate<UMaterialParameterCollection>(TEXT("/ATCSim/Environment"), TEXT("MPC_Environment"));
		for (const TCHAR* Name : { TEXT("Wetness"), TEXT("SnowCover"), TEXT("CloudCoverage"), TEXT("NightAmount"), TEXT("PrecipitationRate") })
		{
			if (!Collection->GetParameterId(Name).IsValid())
			{
				FCollectionScalarParameter Parameter;
				Parameter.ParameterName = Name;
				Parameter.DefaultValue = 0.f;
				Collection->ScalarParameters.Add(Parameter);
			}
		}
		Collection->PostEditChange();
		return Collection;
	}

	UMaterialExpression* Make(UMaterial* Material, UClass* Class, int32 X, int32 Y)
	{
		return UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y);
	}

	UMaterialExpressionConstant3Vector* Color(UMaterial* Material, const FLinearColor& Value, int32 X, int32 Y)
	{
		UMaterialExpressionConstant3Vector* Expression = Cast<UMaterialExpressionConstant3Vector>(Make(Material, UMaterialExpressionConstant3Vector::StaticClass(), X, Y));
		Expression->Constant = Value;
		return Expression;
	}

	UMaterialExpressionCollectionParameter* Parameter(UMaterial* Material, UMaterialParameterCollection* Collection, const TCHAR* Name, int32 X, int32 Y)
	{
		UMaterialExpressionCollectionParameter* Expression = Cast<UMaterialExpressionCollectionParameter>(Make(Material, UMaterialExpressionCollectionParameter::StaticClass(), X, Y));
		Expression->Collection = Collection;
		Expression->ParameterName = Name;
		Expression->ParameterId = Collection->GetParameterId(Name);
		return Expression;
	}

	UMaterial* StartMaterial(const FString& Folder, const FString& Name)
	{
		UMaterial* Material = GetOrCreate<UMaterial>(Folder, Name);
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		return Material;
	}

	/**
	 * Unlit, emissive = the camera feed. The feed is already exposed by the camera itself: the inverse of the
	 * viewer's eye adaptation keeps the screen as bright as a real screen, in a dark cab as well as in daylight.
	 */
	UMaterial* MakeMonitorMaterial()
	{
		UMaterial* Material = StartMaterial(TEXT("/ATCSim/Materials"), TEXT("M_Monitor"));
		Material->SetShadingModel(MSM_Unlit);
		UMaterialExpressionTextureSampleParameter2D* Feed = Cast<UMaterialExpressionTextureSampleParameter2D>(
			Make(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -600, 0));
		Feed->ParameterName = TEXT("Feed");
		Feed->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		Feed->SamplerType = SAMPLERTYPE_Color;
		UMaterialExpressionScalarParameter* Brightness = Cast<UMaterialExpressionScalarParameter>(Make(Material, UMaterialExpressionScalarParameter::StaticClass(), -600, 250));
		Brightness->ParameterName = TEXT("Brightness");
		Brightness->DefaultValue = 0.9f;
		UMaterialExpressionMultiply* Lit = Cast<UMaterialExpressionMultiply>(Make(Material, UMaterialExpressionMultiply::StaticClass(), -350, 50));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Feed, TEXT("RGB"), Lit, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Brightness, TEXT(""), Lit, TEXT("B"));
		UMaterialExpressionEyeAdaptationInverse* Adapted = Cast<UMaterialExpressionEyeAdaptationInverse>(Make(Material, UMaterialExpressionEyeAdaptationInverse::StaticClass(), -150, 50));
		Adapted->LightValueInput.Connect(0, Lit);
		UMaterialEditingLibrary::ConnectMaterialProperty(Adapted, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::RecompileMaterial(Material);
		return Material;
	}

	ULensSetData* MakeLensSet()
	{
		ULensSetData* Lenses = GetOrCreate<ULensSetData>(TEXT("/Game/Camera"), TEXT("DA_LensSet_GMI"));
		auto Lens = [](const TCHAR* Name, float FocalMm)
		{
			FLensPreset Preset;
			Preset.Name = FText::FromString(Name);
			Preset.FocalLengthMm = FocalMm;
			Preset.SensorWidthMm = 36.f;
			return Preset;
		};
		// 73.7, 39.6, 20.4 and 5.2 degrees; a custom lens is one more row
		Lenses->Lenses = { Lens(TEXT("Wide"), 24.f), Lens(TEXT("Normal"), 50.f), Lens(TEXT("Tele"), 100.f), Lens(TEXT("Custom"), 400.f) };
		UEditorLoadingAndSavingUtils::SavePackages({ Lenses->GetPackage() }, false);
		return Lenses;
	}

	void MakeCameraInput()
	{
		const FString Folder = TEXT("/ATCSim/Input");
		auto Action = [&Folder](const TCHAR* Name, const TCHAR* Description)
		{
			UInputAction* Result = GetOrCreate<UInputAction>(Folder, Name);
			Result->ValueType = EInputActionValueType::Axis1D;
			Result->ActionDescription = FText::FromString(Description);
			// The project's own mappings keep their keys (the VR template moves with the right hand)
			Result->bConsumeInput = false;
			return Result;
		};
		FGMIInputActions Actions;
		Actions.Pan = Action(TEXT("IA_GMI_Pan"), TEXT("GMI camera: pan (right +)"));
		Actions.Tilt = Action(TEXT("IA_GMI_Tilt"), TEXT("GMI camera: tilt (up +)"));
		Actions.Zoom = Action(TEXT("IA_GMI_Zoom"), TEXT("GMI camera: zoom (in +)"));
		Actions.NextCamera = Action(TEXT("IA_GMI_NextCamera"), TEXT("GMI camera: next (+1) or previous (-1) camera"));
		Actions.NextPreset = Action(TEXT("IA_GMI_NextPreset"), TEXT("GMI camera: next (+1) or previous (-1) preset"));
		Actions.Preset = Action(TEXT("IA_GMI_Preset"), TEXT("GMI camera: preset number (1 = first)"));
		Actions.Lens = Action(TEXT("IA_GMI_Lens"), TEXT("GMI camera: next lens"));
		UInputMappingContext* Context = GetOrCreate<UInputMappingContext>(Folder, TEXT("IMC_GMI"));
		Context->UnmapAll();
		UGMICameraSubsystem::AddDefaultMappings(Context, Actions);
		TArray<UPackage*> Packages = { Context->GetPackage(), Actions.Pan->GetPackage(), Actions.Tilt->GetPackage(), Actions.Zoom->GetPackage(),
			Actions.NextCamera->GetPackage(), Actions.NextPreset->GetPackage(), Actions.Preset->GetPackage(), Actions.Lens->GetPackage() };
		UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
		UE_LOG(LogATC, Display, TEXT("ATC.CreateCameraInput: %d mappings in %s/IMC_GMI"), Context->GetMappings().Num(), *Folder);
	}

	FAutoConsoleCommand CreateCameraInputCommand(TEXT("ATC.CreateCameraInput"),
		TEXT("Input actions and mapping context for the GMI cameras (/ATCSim/Input): keys, gamepad and VR controller"),
		FConsoleCommandDelegate::CreateLambda([]() { MakeCameraInput(); }));

	/** Yellow taxiway paint. The lights at night are separate meshes along the taxi routes (ATC.CreateTaxiLights). */
	UMaterial* MakeTaxiLineMaterial()
	{
		UMaterial* Material = StartMaterial(TEXT("/ATCSim/Materials"), TEXT("M_TaxiLine"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Color(Material, FLinearColor(0.85f, 0.6f, 0.02f), -300, 0), TEXT(""), MP_BaseColor);
		UMaterialExpressionConstant* Roughness = Cast<UMaterialExpressionConstant>(Make(Material, UMaterialExpressionConstant::StaticClass(), -300, 150));
		Roughness->R = 0.6f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);
		Material->bUsedWithSplineMeshes = true;
		UMaterialEditingLibrary::RecompileMaterial(Material);
		return Material;
	}

	UMaterial* MakeGroundMaterial(const FString& Folder, const FString& Name, const FLinearColor& Base, UMaterialParameterCollection* Collection, float DryRoughness)
	{
		UMaterial* Material = StartMaterial(Folder, Name);

		UMaterialExpressionConstant3Vector* Dry = Color(Material, Base, -900, 0);
		UMaterialExpressionMultiply* WetColor = Cast<UMaterialExpressionMultiply>(Make(Material, UMaterialExpressionMultiply::StaticClass(), -700, 80));
		WetColor->ConstB = 0.55f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Dry, TEXT(""), WetColor, TEXT("A"));
		UMaterialExpressionCollectionParameter* Wetness = Parameter(Material, Collection, TEXT("Wetness"), -900, 200);

		UMaterialExpressionLinearInterpolate* WetLerp = Cast<UMaterialExpressionLinearInterpolate>(Make(Material, UMaterialExpressionLinearInterpolate::StaticClass(), -500, 0));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Dry, TEXT(""), WetLerp, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(WetColor, TEXT(""), WetLerp, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Wetness, TEXT(""), WetLerp, TEXT("Alpha"));

		UMaterialExpressionLinearInterpolate* SnowLerp = Cast<UMaterialExpressionLinearInterpolate>(Make(Material, UMaterialExpressionLinearInterpolate::StaticClass(), -300, 0));
		UMaterialEditingLibrary::ConnectMaterialExpressions(WetLerp, TEXT(""), SnowLerp, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Color(Material, FLinearColor(0.85f, 0.87f, 0.9f), -500, 150), TEXT(""), SnowLerp, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Parameter(Material, Collection, TEXT("SnowCover"), -500, 250), TEXT(""), SnowLerp, TEXT("Alpha"));
		UMaterialEditingLibrary::ConnectMaterialProperty(SnowLerp, TEXT(""), MP_BaseColor);

		UMaterialExpressionLinearInterpolate* Roughness = Cast<UMaterialExpressionLinearInterpolate>(Make(Material, UMaterialExpressionLinearInterpolate::StaticClass(), -300, 300));
		Roughness->ConstA = DryRoughness;
		Roughness->ConstB = FMath::Min(DryRoughness, 0.2f);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Wetness, TEXT(""), Roughness, TEXT("Alpha"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);

		UMaterialEditingLibrary::RecompileMaterial(Material);
		return Material;
	}

	// ---------------------------------------------------------------- Data

	UAircraftTypeData* MakeAircraftType(const TCHAR* ICAO, const TCHAR* Name, EAerodromeCode Code, EWakeCategory Wake, float Span, float Length,
		float Wheelbase, float MaxTaxiKt, float PushKt)
	{
		UAircraftTypeData* Type = GetOrCreate<UAircraftTypeData>(FString(TEXT("/Game/Aircraft/")) + ICAO, FString(TEXT("DA_AircraftType_")) + ICAO);
		Type->ICAOType = ICAO;
		Type->DisplayName = FText::FromString(Name);
		Type->AerodromeCode = Code;
		Type->WakeCategory = Wake;
		Type->WingspanM = Span;
		Type->LengthM = Length;
		Type->WheelbaseM = Wheelbase;
		Type->MaxTaxiSpeedKt = MaxTaxiKt;
		Type->PushbackSpeedKt = PushKt;
		return Type;
	}

	/** Rotation that turns an airliner model nose to +X: the long side is the fuselage, the highest point is the top of the fin. */
	FRotator NoseForward(const UStaticMesh* Mesh)
	{
		const FBox Box = Mesh->GetBoundingBox();
		const bool bAlongX = Box.GetSize().X >= Box.GetSize().Y;
		FVector Top(0.0, 0.0, -UE_BIG_NUMBER);
		if (const FMeshDescription* Description = Mesh->GetMeshDescription(0))
		{
			const TVertexAttributesConstRef<FVector3f> Positions = FStaticMeshConstAttributes(*Description).GetVertexPositions();
			for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
			{
				Top = Positions[Vertex].Z > Top.Z ? FVector(Positions[Vertex]) : Top;
			}
		}
		const double TailSide = bAlongX ? Top.X - Box.GetCenter().X : Top.Y - Box.GetCenter().Y;
		const FVector Nose = bAlongX ? FVector(TailSide > 0.0 ? -1.0 : 1.0, 0.0, 0.0) : FVector(0.0, TailSide > 0.0 ? -1.0 : 1.0, 0.0);
		return FRotator(0.0, -Nose.Rotation().Yaw, 0.0);
	}

	/** Offset that puts a model with its nose along +X after Rotation, the main gear on the origin and the wheels on the ground. */
	bool FitModel(const UAircraftTypeData* Type, const TCHAR* MeshPath, TOptional<FRotator> InRotation, UStaticMesh*& OutMesh, FTransform& OutOffset)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
		if (!Mesh)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC: mesh %s not found, %s keeps its model."), MeshPath, *Type->GetName());
			return false;
		}
		const FRotator Rotation = InRotation.IsSet() ? InRotation.GetValue() : NoseForward(Mesh);
		// Scale the model to the real length and wingspan: both matter to a controller judging separation, and a model
		// with other proportions (the KLM 787 is 56.3 m wide at the right length) would be off by metres
		const FBox Raw = Mesh->GetBoundingBox().TransformBy(FTransform(Rotation));
		const float LengthScale = Type->LengthM * ATCUnits::MetersToCm / FMath::Max(Raw.GetSize().X, 1.f);
		const float SpanScale = Type->WingspanM * ATCUnits::MetersToCm / FMath::Max(Raw.GetSize().Y, 1.f);

		// The scale works on the mesh's own axes, before the rotation: find which one becomes the span
		FVector Scale(LengthScale);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			FVector Local = FVector::ZeroVector;
			Local[Axis] = 1.0;
			if (FMath::Abs(Rotation.RotateVector(Local).Y) > 0.9)
			{
				Scale[Axis] = SpanScale;
			}
		}
		const FBox Box = Mesh->GetBoundingBox().TransformBy(FTransform(Rotation, FVector::ZeroVector, Scale));
		const float NoseTip = (Type->WheelbaseM + 0.12f * Type->LengthM) * ATCUnits::MetersToCm;
		OutMesh = Mesh;
		OutOffset = FTransform(Rotation, FVector(NoseTip - Box.Max.X, -Box.GetCenter().Y, -Box.Min.Z), Scale);
		UE_LOG(LogATC, Display, TEXT("ATC: %s %s yaw %.0f, mesh %.1f x %.1f m, scaled x%.3f (length) x%.3f (span) to %.1f x %.1f m"), *Type->GetName(),
			*Mesh->GetName(), Rotation.Yaw, Raw.GetSize().X / 100.f, Raw.GetSize().Y / 100.f, LengthScale, SpanScale, Box.GetSize().X / 100.f, Box.GetSize().Y / 100.f);
		return true;
	}

	/** The type's own model. */
	void FitMesh(UAircraftTypeData* Type, const TCHAR* MeshPath, TOptional<FRotator> InRotation = {})
	{
		UStaticMesh* Mesh = nullptr;
		FTransform Offset;
		if (FitModel(Type, MeshPath, InRotation, Mesh, Offset))
		{
			Type->Mesh = Mesh;
			Type->MeshOffset = Offset;
		}
	}

	/** Another airline's model of the type (replaces an earlier one of that airline). */
	void AddLivery(UAircraftTypeData* Type, const TCHAR* Airline, const TCHAR* MeshPath, TOptional<FRotator> InRotation = {})
	{
		FAircraftLivery Livery;
		UStaticMesh* Mesh = nullptr;
		if (!FitModel(Type, MeshPath, InRotation, Mesh, Livery.MeshOffset))
		{
			return;
		}
		Livery.Airline = Airline;
		Livery.Mesh = Mesh;
		Type->Liveries.RemoveAll([Airline](const FAircraftLivery& Existing) { return Existing.Airline == Airline; });
		Type->Liveries.Add(Livery);
	}

	/**
	 * Every aircraft type with the models in the project: Content/staticmeshplane and the Datasmith aircraft. The first model
	 * of a type is its own Mesh, the others are liveries of the airline that flies them.
	 */
	void MakeFleet()
	{
		// Airport Planning values; check against the manufacturer documents
		const FString Datasmith = TEXT("/Game/Datasmith/Vliegtuigen/");
		auto Model = [&Datasmith](const TCHAR* Path) { return Datasmith + Path; };
		auto Plane = [](const TCHAR* Name) { return FString::Printf(TEXT("/Game/staticmeshplane/%s.%s"), Name, Name); };

		// Narrow-bodies (code C gates)
		UAircraftTypeData* A320 = MakeAircraftType(TEXT("A320"), TEXT("Airbus A320"), EAerodromeCode::C, EWakeCategory::Medium, 35.8f, 37.6f, 12.6f, 30.f, 3.f);
		FitMesh(A320, *Plane(TEXT("A320IBERIA")));
		AddLivery(A320, TEXT("EZY"), *Model(TEXT("A320_Easyjet/A320_Easyjet/Geometries/Group_294.Group_294")));
		AddLivery(A320, TEXT("VLG"), *Model(TEXT("A320_Vueling/A320_Vueling/Geometries/Group_144.Group_144")));
		UAircraftTypeData* B738 = MakeAircraftType(TEXT("B738"), TEXT("Boeing 737-800"), EAerodromeCode::C, EWakeCategory::Medium, 35.8f, 39.5f, 15.6f, 30.f, 3.f);
		FitMesh(B738, *Model(TEXT("737-8_Transavia/737-8_Transavia/Geometries/Group_219.Group_219")));
		AddLivery(B738, TEXT("TFL"), *Model(TEXT("737_TUI/737_TUI/Geometries/Group_281.Group_281")));
		UAircraftTypeData* E190 = MakeAircraftType(TEXT("E190"), TEXT("Embraer 190"), EAerodromeCode::C, EWakeCategory::Medium, 28.7f, 36.2f, 14.0f, 30.f, 3.f);
		FitMesh(E190, *Model(TEXT("KLM_Cityhopper/KLM_Cityhopper/Geometries/Group_281.Group_281")));

		// Wide-bodies (code E gates) and the A380 (code F)
		UAircraftTypeData* B789 = MakeAircraftType(TEXT("B789"), TEXT("Boeing 787-9"), EAerodromeCode::E, EWakeCategory::Heavy, 60.1f, 62.8f, 25.9f, 25.f, 2.5f);
		FitMesh(B789, *Model(TEXT("787-9_KLM/787-9_KLM/Geometries/Group_2.Group_2")), FRotator(0.f, -90.f, 0.f));
		UAircraftTypeData* B78X = MakeAircraftType(TEXT("B78X"), TEXT("Boeing 787-10"), EAerodromeCode::E, EWakeCategory::Heavy, 60.1f, 68.3f, 30.0f, 25.f, 2.5f);
		FitMesh(B78X, *Model(TEXT("787-10_KLM/787-10_KLM_100_jaar/Geometries/Group_3.Group_3")));
		UAircraftTypeData* B788 = MakeAircraftType(TEXT("B788"), TEXT("Boeing 787-8"), EAerodromeCode::E, EWakeCategory::Heavy, 60.1f, 56.7f, 22.8f, 25.f, 2.5f);
		// The 787-8 and the A380 are wider than they are long: the long-side rule would take the wings for the fuselage
		FitMesh(B788, *Plane(TEXT("787norwegian")), FRotator(0.f, -90.f, 0.f));
		UAircraftTypeData* B77W = MakeAircraftType(TEXT("B77W"), TEXT("Boeing 777-300ER"), EAerodromeCode::E, EWakeCategory::Heavy, 64.8f, 73.9f, 31.2f, 25.f, 2.5f);
		FitMesh(B77W, *Plane(TEXT("777koreanair")));
		AddLivery(B77W, TEXT("KLM"), *Model(TEXT("777-306_KLM_orange_pride/777-306_KLM_orange_pride/Geometries/Group_101.Group_101")));
		AddLivery(B77W, TEXT("ANZ"), *Plane(TEXT("777airnewzealand")));
		AddLivery(B77W, TEXT("AAL"), *Plane(TEXT("777americanairline")));
		UAircraftTypeData* B77L = MakeAircraftType(TEXT("B77L"), TEXT("Boeing 777F"), EAerodromeCode::E, EWakeCategory::Heavy, 64.8f, 63.7f, 25.9f, 25.f, 2.5f);
		FitMesh(B77L, *Plane(TEXT("777fedex")));
		UAircraftTypeData* A359 = MakeAircraftType(TEXT("A359"), TEXT("Airbus A350-900"), EAerodromeCode::E, EWakeCategory::Heavy, 64.8f, 66.8f, 28.7f, 25.f, 2.5f);
		FitMesh(A359, *Plane(TEXT("a350finnair")));
		AddLivery(A359, TEXT("DLH"), *Plane(TEXT("a350lufthansa")));
		AddLivery(A359, TEXT("QTR"), *Plane(TEXT("a350qatar")));
		AddLivery(A359, TEXT("SIA"), *Plane(TEXT("a350singaporeairlines")));
		UAircraftTypeData* A333 = MakeAircraftType(TEXT("A333"), TEXT("Airbus A330-300"), EAerodromeCode::E, EWakeCategory::Heavy, 60.3f, 63.7f, 25.6f, 25.f, 2.5f);
		FitMesh(A333, *Plane(TEXT("a330airdelta")));
		UAircraftTypeData* B744 = MakeAircraftType(TEXT("B744"), TEXT("Boeing 747-400F"), EAerodromeCode::E, EWakeCategory::Heavy, 64.4f, 70.7f, 25.6f, 25.f, 2.5f);
		FitMesh(B744, *Plane(TEXT("747chinaairlinescargo")));
		AddLivery(B744, TEXT("NCA"), *Plane(TEXT("747nca")));
		UAircraftTypeData* A388 = MakeAircraftType(TEXT("A388"), TEXT("Airbus A380-800"), EAerodromeCode::F, EWakeCategory::Super, 79.8f, 72.7f, 32.0f, 25.f, 2.5f);
		FitMesh(A388, *Model(TEXT("A380_Emirates/A380_Emirates/Geometries/Group_17.Group_17")), FRotator(0.f, -90.f, 0.f));
	}

	void FillEHAMTrafficMix(UTrafficMixData* Mix)
	{
		auto Entry = [](const TCHAR* ICAO, const TCHAR* Airline, float Weight)
		{
			FTrafficMixEntry MixEntry;
			MixEntry.Type = TSoftObjectPtr<UAircraftTypeData>(FSoftObjectPath(FString::Printf(TEXT("/Game/Aircraft/%s/DA_AircraftType_%s.DA_AircraftType_%s"), ICAO, ICAO, ICAO)));
			MixEntry.Airlines = { Airline };
			MixEntry.Weight = Weight;
			return MixEntry;
		};
		// Roughly a Schiphol day: KLM and its Cityhopper, Transavia and easyJet on the narrow gates; intercontinental
		// KLM with a few foreign long-haul and cargo carriers on the wide gates
		Mix->Entries = {
			Entry(TEXT("E190"), TEXT("KLC"), 3.f), Entry(TEXT("B738"), TEXT("TRA"), 3.f), Entry(TEXT("B738"), TEXT("TFL"), 1.f),
			Entry(TEXT("A320"), TEXT("EZY"), 3.f), Entry(TEXT("A320"), TEXT("VLG"), 1.f), Entry(TEXT("A320"), TEXT("IBS"), 0.5f),
			Entry(TEXT("B789"), TEXT("KLM"), 2.f), Entry(TEXT("B78X"), TEXT("KLM"), 1.5f), Entry(TEXT("B77W"), TEXT("KLM"), 1.5f),
			Entry(TEXT("B77W"), TEXT("KAL"), 0.5f), Entry(TEXT("B77W"), TEXT("ANZ"), 0.3f), Entry(TEXT("B77W"), TEXT("AAL"), 0.6f),
			Entry(TEXT("A359"), TEXT("FIN"), 0.6f), Entry(TEXT("A359"), TEXT("DLH"), 0.5f), Entry(TEXT("A359"), TEXT("QTR"), 0.7f),
			Entry(TEXT("A359"), TEXT("SIA"), 0.6f), Entry(TEXT("A333"), TEXT("DAL"), 0.8f), Entry(TEXT("B788"), TEXT("NAX"), 0.4f),
			Entry(TEXT("B77L"), TEXT("FDX"), 0.4f), Entry(TEXT("B744"), TEXT("CAL"), 0.4f), Entry(TEXT("B744"), TEXT("NCA"), 0.5f),
			Entry(TEXT("A388"), TEXT("UAE"), 0.5f) };
		Mix->Modify();
		Mix->MarkPackageDirty();
	}

	/** ATC.SetupFleet: all aircraft types and liveries, and the Schiphol traffic mix that uses them. */
	void SetupFleet(const TArray<FString>& Args)
	{
		Dirty.Reset();
		MakeFleet();
		if (UTrafficMixData* Mix = LoadObject<UTrafficMixData>(nullptr, TEXT("/Game/Airports/EHAM/Data/DA_TrafficMix_EHAM.DA_TrafficMix_EHAM")))
		{
			FillEHAMTrafficMix(Mix);
			Dirty.AddUnique(Mix->GetPackage());
		}
		UEditorLoadingAndSavingUtils::SavePackages(Dirty, false);
		UE_LOG(LogATC, Display, TEXT("ATC.SetupFleet: %d assets saved"), Dirty.Num());
	}

	FAutoConsoleCommand SetupFleetCommand(TEXT("ATC.SetupFleet"),
		TEXT("Make every aircraft type with the models and liveries in the project, and the Schiphol traffic mix"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetupFleet));

	UWeatherPresetData* MakeWeather(const TCHAR* Name, const TCHAR* Display, const TCHAR* Metar, float Coverage, float BaseFt, float VisibilityM,
		EPrecipitationType Precipitation, float Rate, float Wetness, float Snow, float Sun, float SourceAngle, float WindDir, float WindKt)
	{
		UWeatherPresetData* Preset = GetOrCreate<UWeatherPresetData>(TEXT("/Game/Environment/Weather"), FString(TEXT("DA_Weather_")) + Name);
		Preset->DisplayName = FText::FromString(Display);
		Preset->MetarExample = Metar;
		FWeatherState& State = Preset->State;
		State.CloudCoverage = Coverage;
		State.CloudBaseFt = BaseFt;
		State.VisibilityM = VisibilityM;
		State.Precipitation = Precipitation;
		State.PrecipitationRate = Rate;
		State.Wetness = Wetness;
		State.SnowCover = Snow;
		State.SunIntensityScale = Sun;
		State.SunSourceAngle = SourceAngle;
		State.WindDirectionDeg = WindDir;
		State.WindSpeedKt = WindKt;
		return Preset;
	}

	FRunwayInfo Runway(const TCHAR* Designator, const TCHAR* Name, float LengthM)
	{
		FRunwayInfo Info;
		Info.Designator = Designator;
		Info.Name = FText::FromString(Name);
		Info.LengthM = LengthM;
		return Info;
	}

	FTowerPosition TowerAt(const TCHAR* TagName, const TCHAR* Name, const FTransform& Transform = FTransform::Identity)
	{
		FTowerPosition Tower;
		Tower.Tag = Tag(TagName);
		Tower.Name = FText::FromString(Name);
		Tower.Transform = Transform;
		return Tower;
	}

	/** View from a tower cab: a little in front of the middle (where the consoles are), looking at Yaw/Pitch. */
	FTransform CabView(const FVector& CabCenter, float FloorZ, float Yaw, float Pitch, float ForwardCm)
	{
		const FRotator Flat(0.f, Yaw, 0.f);
		return FTransform(FRotator(Pitch, Yaw, 0.f), FVector(CabCenter.X, CabCenter.Y, FloorZ) + Flat.Vector() * ForwardCm);
	}

	UAirportData* MakeAirport(const TCHAR* ICAO, const TCHAR* Name, double Latitude, double Longitude, float ElevationFt, const TCHAR* MapPath,
		const TArray<FRunwayInfo>& Runways, const TArray<FTowerPosition>& Towers, UWeatherPresetData* Weather)
	{
		UAirportData* Airport = GetOrCreate<UAirportData>(FString::Printf(TEXT("/Game/Airports/%s/Data"), ICAO), FString(TEXT("DA_Airport_")) + ICAO);
		Airport->ICAO = ICAO;
		Airport->DisplayName = FText::FromString(Name);
		Airport->Latitude = Latitude;
		Airport->Longitude = Longitude;
		Airport->ElevationFt = ElevationFt;
		Airport->Map = MapPath ? TSoftObjectPtr<UWorld>(FSoftObjectPath(MapPath)) : TSoftObjectPtr<UWorld>();
		Airport->Runways = Runways;
		Airport->TowerPositions = Towers;
		Airport->DefaultWeather = Weather;
		return Airport;
	}

	// ---------------------------------------------------------------- Showcase map

	const FVector TowerFloor(-20000.f, 25000.f, 3800.f);
	const FRotator TowerView(-8.f, -29.5f, 0.f);

	AStaticMeshActor* Block(UWorld* World, const TCHAR* MeshPath, const FVector& Location, const FVector& Scale, UMaterialInterface* Material, const TCHAR* Label)
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
		Actor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
		if (Material)
		{
			Actor->GetStaticMeshComponent()->SetMaterial(0, Material);
		}
		Actor->SetActorScale3D(Scale);
		Actor->SetActorLabel(Label);
		return Actor;
	}

	ATaxiRoute* MakeRoute(UWorld* World, const TCHAR* TagName, const TArray<FVector>& Points, UMaterialInterface* LineMaterial, bool bCenterline)
	{
		ATaxiRoute* Route = World->SpawnActor<ATaxiRoute>(Points[0], FRotator::ZeroRotator);
		Route->RouteTag = Tag(TagName);
		Route->CenterlineMaterial = LineMaterial;
		Route->bShowCenterline = bCenterline;
		Route->bShowLights = bCenterline;   // the pushback route has no centerline and no lights
		Route->Spline->ClearSplinePoints(false);
		for (const FVector& Point : Points)
		{
			Route->Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
		}
		Route->Spline->UpdateSpline();
		Route->SetActorLabel(FString(TagName).Replace(TEXT("."), TEXT("_")));
		return Route;
	}

	float DistanceAt(const ATaxiRoute* Route, const FVector& Point)
	{
		return UAircraftMovementComponent::GetDistanceClosestTo(Route->Spline, Point);
	}

	FCameraPreset Aim(const FVector& From, const FVector& Target, const TCHAR* TagName, float FocalMm)
	{
		const FVector Delta = Target - From;
		FCameraPreset Preset;
		Preset.Tag = Tag(TagName);
		Preset.PanDeg = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		Preset.TiltDeg = FMath::RadiansToDegrees(FMath::Atan2(Delta.Z, FVector2D(Delta.X, Delta.Y).Size()));
		Preset.FocalLengthMm = FocalMm;
		return Preset;
	}

	AEnvironmentRig* SpawnEnvironment(UWorld* World)
	{
		// Sky, sun and fog: the lighting rig drives these
		ADirectionalLight* SunActor = World->SpawnActor<ADirectionalLight>(FVector(0.f, 0.f, 2000.f), FRotator(-40.f, 160.f, 0.f));
		UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(SunActor->GetLightComponent());
		Sun->SetMobility(EComponentMobility::Movable);
		Sun->bAtmosphereSunLight = true;
		Sun->bCastCloudShadows = true;
		SunActor->SetActorLabel(TEXT("Sun"));

		ADirectionalLight* MoonActor = World->SpawnActor<ADirectionalLight>(FVector(0.f, 0.f, 2200.f), FRotator(-35.f, 0.f, 0.f));
		UDirectionalLightComponent* Moon = Cast<UDirectionalLightComponent>(MoonActor->GetLightComponent());
		Moon->SetMobility(EComponentMobility::Movable);
		Moon->bAtmosphereSunLight = false;
		Moon->SetIntensity(0.f);
		Moon->SetLightColor(FLinearColor(0.65f, 0.75f, 1.f));
		Moon->SetCastShadows(false);
		MoonActor->SetActorLabel(TEXT("Moon"));

		ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.f, 0.f, 2000.f), FRotator::ZeroRotator);
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sky->GetLightComponent()->bRealTimeCapture = true;

		ASkyAtmosphere* Atmosphere = World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
		AVolumetricCloud* Clouds = World->SpawnActor<AVolumetricCloud>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (UVolumetricCloudComponent* CloudComponent = Clouds->FindComponentByClass<UVolumetricCloudComponent>(); CloudComponent && !CloudComponent->GetMaterial())
		{
			CloudComponent->SetMaterial(LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")));
		}
		AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector, FRotator::ZeroRotator);

		AEnvironmentRig* Rig = World->SpawnActor<AEnvironmentRig>(MakeBlueprint(AEnvironmentRig::StaticClass(), TEXT("/ATCSim/Blueprints"), TEXT("BP_EnvironmentRig")),
			FVector(0.f, 0.f, 500.f), FRotator::ZeroRotator);
		Rig->LightingRig->Sun = SunActor;
		Rig->LightingRig->Moon = MoonActor;
		Rig->LightingRig->SkyLight = Sky;
		Rig->LightingRig->Fog = Fog;
		Rig->LightingRig->Clouds = Clouds;
		Rig->LightingRig->Atmosphere = Atmosphere;
		APostProcessVolume* PostProcess = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator);
		PostProcess->bUnbound = true;
		PostProcess->SetActorLabel(TEXT("PostProcess_Global"));
		Rig->LightingRig->PostProcess = PostProcess;
		Rig->SetActorLabel(TEXT("EnvironmentRig"));
		return Rig;
	}

	void BuildShowcaseMap(UScenarioData* Scenario, ULensSetData* Lenses, UTextureRenderTarget2D* RenderTarget, UMaterialInterface* Ground,
		UMaterialInterface* Apron, UMaterialInterface* RunwayMaterial, UMaterialInterface* TaxiLine)
	{
		UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
		if (!World)
		{
			UE_LOG(LogATC, Error, TEXT("ATC.CreateExampleContent: could not create a map."));
			return;
		}
		SpawnEnvironment(World);

		// Ground: grass-free airport, apron near the stands, runway along X (north = +X)
		const TCHAR* Plane = TEXT("/Engine/BasicShapes/Plane.Plane");
		const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
		Block(World, Plane, FVector(30000.f, 20000.f, 0.f), FVector(1200.f, 800.f, 1.f), Ground, TEXT("Ground"));
		Block(World, Plane, FVector(-3000.f, 5000.f, 1.f), FVector(110.f, 260.f, 1.f), Apron, TEXT("Apron"));
		Block(World, Plane, FVector(50000.f, 33000.f, 1.f), FVector(1200.f, 45.f, 1.f), RunwayMaterial, TEXT("Runway 18/36"));
		Block(World, Cube, FVector(TowerFloor.X, TowerFloor.Y, TowerFloor.Z * 0.5f), FVector(8.f, 8.f, TowerFloor.Z / 100.f), nullptr, TEXT("Tower"));
		Block(World, Cube, FVector(4000.f, 4500.f, 600.f), FVector(20.f, 160.f, 12.f), nullptr, TEXT("Terminal"));

		// Routes. Pushback from S1 ends facing east; the taxi route continues from there to the runway
		ATaxiRoute* Pushback = MakeRoute(World, TEXT("Route.Showcase.S1.Pushback"), {
			FVector(0.f, 0.f, 0.f), FVector(-2000.f, 0.f, 0.f), FVector(-4475.f, -1025.f, 0.f), FVector(-5500.f, -3500.f, 0.f), FVector(-5500.f, -4500.f, 0.f) },
			TaxiLine, false);

		ATaxiRoute* Taxi = MakeRoute(World, TEXT("Route.Showcase.S1.Runway"), {
			FVector(-5500.f, -4500.f, 0.f), FVector(-5500.f, 12000.f, 0.f), FVector(-4036.f, 15536.f, 0.f), FVector(-500.f, 17000.f, 0.f),
			FVector(30000.f, 17000.f, 0.f), FVector(40000.f, 17000.f, 0.f), FVector(42828.f, 18172.f, 0.f), FVector(44000.f, 21000.f, 0.f),
			FVector(44000.f, 30000.f, 0.f) },
			TaxiLine, true);

		FSpeedZone ApronZone;
		ApronZone.StartDistance = 0.f;
		ApronZone.EndDistance = DistanceAt(Taxi, FVector(-5500.f, 12000.f, 0.f));
		ApronZone.MaxSpeedKt = 10.f;
		Taxi->SpeedZones = { ApronZone };

		auto Waypoint = [Taxi](const TCHAR* Name, const FVector& Point)
		{
			FTaxiWaypoint Result;
			Result.Name = Name;
			Result.Distance = DistanceAt(Taxi, Point);
			return Result;
		};
		Taxi->Waypoints = { Waypoint(TEXT("A1"), FVector(-5500.f, 12000.f, 0.f)), Waypoint(TEXT("B"), FVector(-500.f, 17000.f, 0.f)),
			Waypoint(TEXT("B4"), FVector(40000.f, 17000.f, 0.f)) };

		FTaxiStop Intersection;
		Intersection.Name = TEXT("Intersection B2");
		Intersection.Type = ETaxiStopType::Intersection;
		Intersection.Distance = DistanceAt(Taxi, FVector(10000.f, 17000.f, 0.f));
		Intersection.HoldTime = 4.f;
		Taxi->Stops = { Intersection };

		// The holding point is a marker, the way designers place stops
		ATaxiStopMarker* HoldingPoint = World->SpawnActor<ATaxiStopMarker>(FVector(30000.f, 17000.f, 0.f), FRotator::ZeroRotator);
		HoldingPoint->Stop.Name = TEXT("Holding point B4 runway 36");
		HoldingPoint->Stop.Type = ETaxiStopType::HoldingPoint;
		HoldingPoint->Stop.bRequiresClearance = true;
		HoldingPoint->SetActorLabel(TEXT("HoldingPoint_B4"));
		Taxi->StopMarkers = { HoldingPoint };

		Pushback->RerunConstructionScripts();
		Taxi->RerunConstructionScripts();

		// Stands
		UClass* StandClass = MakeBlueprint(AStand::StaticClass(), TEXT("/ATCSim/Blueprints"), TEXT("BP_Stand"));
		AStand* S1 = World->SpawnActor<AStand>(StandClass, FVector::ZeroVector, FRotator::ZeroRotator);
		S1->StandTag = Tag(TEXT("Stand.Showcase.S1"));
		FPushbackOption FaceEast;
		FaceEast.OptionTag = Tag(TEXT("Pushback.FaceEast"));
		FaceEast.Route = Pushback;
		FaceEast.Phraseology = FText::FromString(TEXT("push back approved, face east"));
		S1->PushbackOptions = { FaceEast };
		S1->SetActorLabel(TEXT("Stand_S1"));

		AStand* S2 = World->SpawnActor<AStand>(StandClass, FVector(0.f, 9000.f, 0.f), FRotator::ZeroRotator);
		S2->StandTag = Tag(TEXT("Stand.Showcase.S2"));
		S2->SetActorLabel(TEXT("Stand_S2"));

		// GMI camera on a 25 m mast, cycling through three presets
		const FVector CameraLocation(-12000.f, 28000.f, 2550.f);
		Block(World, Cube, FVector(CameraLocation.X, CameraLocation.Y, 1250.f), FVector(0.4f, 0.4f, 25.f), nullptr, TEXT("CameraMast"));
		AGMICamera* Camera = World->SpawnActor<AGMICamera>(MakeBlueprint(AGMICamera::StaticClass(), TEXT("/ATCSim/Blueprints"), TEXT("BP_GMICamera")),
			CameraLocation, FRotator::ZeroRotator);
		Camera->RenderTarget = RenderTarget;
		Camera->PTZ->LensSet = Lenses;
		Camera->PTZ->Presets = {
			Aim(CameraLocation, FVector(0.f, 0.f, 500.f), TEXT("Cam.Showcase.Stand"), 50.f),
			Aim(CameraLocation, FVector(30000.f, 17000.f, 400.f), TEXT("Cam.Showcase.HoldingPoint"), 100.f),
			Aim(CameraLocation, FVector(44000.f, 30000.f, 400.f), TEXT("Cam.Showcase.Runway"), 135.f) };
		Camera->PTZ->bCyclePresets = true;
		Camera->SetActorLabel(TEXT("GMI-01"));

		// Monitor on the console: below the window line and to the right, so it never blocks the apron
		const FVector Eye = TowerFloor + FVector(0.f, 0.f, 170.f);
		const FRotator Flat(0.f, TowerView.Yaw, 0.f);
		const FVector MonitorLocation = Eye + Flat.Vector() * 150.f + FRotationMatrix(Flat).GetUnitAxis(EAxis::Y) * 75.f - FVector(0.f, 0.f, 80.f);
		AMonitorDisplay* Monitor = World->SpawnActor<AMonitorDisplay>(MakeBlueprint(AMonitorDisplay::StaticClass(), TEXT("/ATCSim/Blueprints"), TEXT("BP_MonitorDisplay")),
			MonitorLocation, (Eye - MonitorLocation).Rotation());
		Monitor->SourceCamera = Camera;
		Monitor->SetActorLabel(TEXT("Monitor_GMI-01"));
		Monitor->RerunConstructionScripts();

		World->SpawnActor<APlayerStart>(TowerFloor + FVector(0.f, 0.f, 100.f), Flat);

		AScenarioStarter* Starter = World->SpawnActor<AScenarioStarter>(FVector(0.f, 0.f, 300.f), FRotator::ZeroRotator);
		Starter->Scenario = Scenario;
		Starter->SetActorLabel(TEXT("ScenarioStarter"));

		UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/Airports/Showcase/Maps/Map_Showcase_ATC"));
	}

	void CreateExampleContent(const TArray<FString>& Args)
	{
		Dirty.Reset();

		UMaterialParameterCollection* Collection = MakeEnvironmentParameters();
		Dirty.Append(ATCNiagara::CreatePrecipitation());
		MakeMonitorMaterial();
		UMaterial* TaxiLine = MakeTaxiLineMaterial();
		const FString ShowcaseMaterials = TEXT("/Game/Airports/Showcase/Materials");
		UMaterial* Ground = MakeGroundMaterial(ShowcaseMaterials, TEXT("M_ShowcaseGround"), FLinearColor(0.16f, 0.17f, 0.15f), Collection);
		UMaterial* Apron = MakeGroundMaterial(ShowcaseMaterials, TEXT("M_ShowcaseApron"), FLinearColor(0.38f, 0.38f, 0.36f), Collection);
		UMaterial* RunwayMaterial = MakeGroundMaterial(ShowcaseMaterials, TEXT("M_ShowcaseRunway"), FLinearColor(0.06f, 0.06f, 0.06f), Collection);

		// Aircraft types with their models and liveries
		MakeFleet();
		UAircraftTypeData* A320 = LoadObject<UAircraftTypeData>(nullptr, TEXT("/Game/Aircraft/A320/DA_AircraftType_A320.DA_AircraftType_A320"));
		UAircraftTypeData* B789 = LoadObject<UAircraftTypeData>(nullptr, TEXT("/Game/Aircraft/B789/DA_AircraftType_B789.DA_AircraftType_B789"));

		// Weather presets with the METAR they stand for
		UWeatherPresetData* Clear = MakeWeather(TEXT("Clear"), TEXT("Helder"), TEXT("EHAM 131125Z 24008KT CAVOK 16/07 Q1021 NOSIG"),
			0.05f, 4000.f, 25000.f, EPrecipitationType::None, 0.f, 0.f, 0.f, 1.f, 0.5f, 240.f, 8.f);
		MakeWeather(TEXT("Overcast"), TEXT("Bewolkt"), TEXT("EHAM 131125Z 22012KT 9999 BKN012 OVC025 13/10 Q1012"),
			0.85f, 1200.f, 10000.f, EPrecipitationType::None, 0.f, 0.1f, 0.f, 0.35f, 5.f, 220.f, 12.f);
		MakeWeather(TEXT("Rain"), TEXT("Regen"), TEXT("EHAM 131125Z 21018G28KT 4000 RA BKN008 OVC015 11/10 Q1004"),
			0.95f, 800.f, 4000.f, EPrecipitationType::Rain, 0.7f, 1.f, 0.f, 0.2f, 8.f, 210.f, 18.f);
		MakeWeather(TEXT("Mist"), TEXT("Mist"), TEXT("EHAM 130625Z 00000KT 0300 FG VV001 06/06 Q1025"),
			1.f, 100.f, 300.f, EPrecipitationType::None, 0.f, 0.4f, 0.f, 0.1f, 10.f, 0.f, 0.f);
		MakeWeather(TEXT("Snow"), TEXT("Sneeuw"), TEXT("EHAM 130925Z 03010KT 1500 SN BKN006 OVC012 M01/M02 Q1009"),
			0.95f, 600.f, 1500.f, EPrecipitationType::Snow, 0.6f, 0.f, 0.6f, 0.25f, 8.f, 30.f, 10.f);

		// Camera
		ULensSetData* Lenses = MakeLensSet();
		MakeCameraInput();

		UTextureRenderTarget2D* RenderTarget = GetOrCreate<UTextureRenderTarget2D>(TEXT("/Game/Camera"), TEXT("RT_GMI_01"));
		RenderTarget->RenderTargetFormat = RTF_RGBA8_SRGB;
		RenderTarget->ClearColor = FLinearColor::Black;
		RenderTarget->SizeX = 1280;
		RenderTarget->SizeY = 720;
		RenderTarget->PostEditChange();

		// Airports. The EHAM map is built around Tower Centrum (OSM node 247334421) with +X east and +Y south; the
		// cab heights come from the tower models in the map (ATC.MapNear profile)
		UAirportData* Eham = MakeAirport(TEXT("EHAM"), TEXT("Amsterdam Schiphol"), 52.3075170, 4.7624393, -11.f, TEXT("/Game/EHAM/EHAM.EHAM"),
			{ Runway(TEXT("18R/36L"), TEXT("Polderbaan"), 3800.f), Runway(TEXT("06/24"), TEXT("Kaagbaan"), 3500.f),
			  Runway(TEXT("09/27"), TEXT("Buitenveldertbaan"), 3453.f), Runway(TEXT("18C/36C"), TEXT("Zwanenburgbaan"), 3300.f),
			  Runway(TEXT("18L/36R"), TEXT("Aalsmeerbaan"), 3400.f), Runway(TEXT("04/22"), TEXT("Oostbaan"), 2014.f) },
			{ TowerAt(TEXT("Tower.EHAM.Centrum"), TEXT("Schiphol Tower Centrum"), CabView(FVector::ZeroVector, 9250.f, -128.f, -8.f, 300.f)),
			  TowerAt(TEXT("Tower.EHAM.West"), TEXT("Schiphol Tower West"), CabView(FVector(-302800.f, -269320.f, 0.f), 4250.f, 152.f, -4.f, 200.f)),
			  // Assumption: the old tower at Schiphol-Oost (OSM "Eerste verkeerstoren", building BLDG112); the sources
			  // do not say clearly which building is the official emergency tower today
			  TowerAt(TEXT("Tower.EHAM.Nood"), TEXT("Schiphol Noodtoren (aanname: oude toren Schiphol-Oost)"),
				  CabView(FVector(322990.f, -23080.f, 0.f), 1800.f, 180.f, -3.f, 100.f)) }, Clear);
		Eham->NorthOffsetDeg = -90.f;

		// The other four get their map, geo reference and towers from ATC.SetupAirport / ATC.BuildSchematicMap; here only a
		// first version when they do not exist yet, so running this again does not undo that
		auto Placeholder = [&Clear](const TCHAR* ICAO, const TCHAR* Name, double Latitude, double Longitude, float ElevationFt,
			const TArray<FRunwayInfo>& Runways, const TCHAR* TowerTag, const TCHAR* TowerName)
		{
			if (!FPackageName::DoesPackageExist(FString::Printf(TEXT("/Game/Airports/%s/Data/DA_Airport_%s"), ICAO, ICAO)))
			{
				MakeAirport(ICAO, Name, Latitude, Longitude, ElevationFt, nullptr, Runways, { TowerAt(TowerTag, TowerName) }, Clear);
			}
		};
		Placeholder(TEXT("EHRD"), TEXT("Rotterdam The Hague"), 51.9569, 4.4372, -15.f, { Runway(TEXT("06/24"), TEXT(""), 2200.f) }, TEXT("Tower.EHRD"), TEXT("Rotterdam Tower"));
		Placeholder(TEXT("EHGG"), TEXT("Groningen Eelde"), 53.1197, 6.5794, 17.f, { Runway(TEXT("05/23"), TEXT(""), 2500.f) }, TEXT("Tower.EHGG"), TEXT("Eelde Tower"));
		Placeholder(TEXT("EHBK"), TEXT("Maastricht Aachen"), 50.9117, 5.7701, 375.f, { Runway(TEXT("03/21"), TEXT(""), 2750.f) }, TEXT("Tower.EHBK"), TEXT("Beek Tower"));
		Placeholder(TEXT("EHLE"), TEXT("Lelystad"), 52.4603, 5.5272, -13.f, { Runway(TEXT("05/23"), TEXT(""), 2700.f) }, TEXT("Tower.EHLE"), TEXT("Lelystad Tower"));

		FTowerPosition ShowcaseTower = TowerAt(TEXT("Tower.Showcase"), TEXT("Showcase tower"), FTransform(TowerView, TowerFloor));
		UAirportData* Showcase = GetOrCreate<UAirportData>(TEXT("/Game/Airports/Showcase/Data"), TEXT("DA_Airport_Showcase"));
		Showcase->ICAO = TEXT("SHOW");
		Showcase->DisplayName = FText::FromString(TEXT("ATC Sim showcase"));
		Showcase->Map = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Airports/Showcase/Maps/Map_Showcase_ATC.Map_Showcase_ATC")));
		Showcase->TowerPositions = { ShowcaseTower };
		Showcase->Runways = { Runway(TEXT("18/36"), TEXT("Showcase runway"), 1200.f) };
		Showcase->DefaultWeather = Clear;

		// Blueprint layer
		UClass* AircraftClass = MakeBlueprint(AATCAircraft::StaticClass(), TEXT("/ATCSim/Blueprints"), TEXT("BP_Aircraft_Base"));

		// Scenario: KLM1023 pushes back, taxis to the holding point, waits for clearance and lines up
		UScenarioData* Scenario = GetOrCreate<UScenarioData>(TEXT("/Game/Scenarios"), TEXT("DA_Scenario_Showcase"));
		Scenario->Title = FText::FromString(TEXT("Showcase: pushback, taxi en klaring"));
		Scenario->Airport = Showcase;
		Scenario->TowerPosition = ShowcaseTower.Tag;
		Scenario->StartTimeUTC = FDateTime(2026, 10, 13, 9, 30, 0);
		Scenario->Weather = Clear;

		FScenarioAircraft Klm;
		Klm.Callsign = TEXT("KLM1023");
		Klm.Type = B789;
		Klm.AircraftClass = TSoftClassPtr<AATCAircraft>(FSoftObjectPath(AircraftClass));
		Klm.Stand = Tag(TEXT("Stand.Showcase.S1"));
		Klm.TaxiRoute = Tag(TEXT("Route.Showcase.S1.Runway"));
		FScenarioAircraft Ezy;
		Ezy.Callsign = TEXT("EZY45LK");
		Ezy.Type = A320;
		Ezy.AircraftClass = Klm.AircraftClass;
		Ezy.Stand = Tag(TEXT("Stand.Showcase.S2"));
		Scenario->Aircraft = { Klm, Ezy };

		FScenarioEvent Push;
		Push.TimeOffsetSeconds = 5.f;
		Push.Callsign = Klm.Callsign;
		Push.Command = Tag(TEXT("Cmd.Pushback"));
		Push.Parameter = Tag(TEXT("Pushback.FaceEast"));
		FScenarioEvent Clearance;
		Clearance.TimeOffsetSeconds = 200.f;
		Clearance.Callsign = Klm.Callsign;
		Clearance.Command = Tag(TEXT("Cmd.Continue"));
		Scenario->Events = { Push, Clearance };

		Dirty.Append(ATCTaxiLights::Create());   // before the map: the routes pick up the lights when they are built
		UEditorLoadingAndSavingUtils::SavePackages(Dirty, false);
		UE_LOG(LogATC, Display, TEXT("ATC.CreateExampleContent: %d assets saved"), Dirty.Num());

		Dirty.Reset();
		BuildShowcaseMap(Scenario, Lenses, RenderTarget, Ground, Apron, RunwayMaterial, TaxiLine);
		UEditorLoadingAndSavingUtils::SavePackages(Dirty, false);   // Blueprints made while building the map
		UE_LOG(LogATC, Display, TEXT("ATC.CreateExampleContent: done, showcase map saved"));
	}

	FAutoConsoleCommand CreateExampleContentCommand(TEXT("ATC.CreateExampleContent"),
		TEXT("Create the example assets (aircraft types, weather, lenses, airports, scenario, Blueprints, materials) and the showcase map"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CreateExampleContent));
}
