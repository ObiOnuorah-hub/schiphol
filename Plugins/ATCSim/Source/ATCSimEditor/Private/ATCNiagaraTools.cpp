// Niagara helpers: inspect a template's parameters, and build the rain and snow effects from Epic's
// RecycleParticlesInView template (particles recycled inside the camera view: cheap enough for VR).

#include "ATCNiagaraTools.h"
#include "ATCUnits.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionLength.h"
#include "Materials/MaterialExpressionParticleSize.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Misc/PackageName.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"

namespace ATCNiagara
{
	void LogStore(const FString& Owner, const FNiagaraParameterStore& Store)
	{
		for (const FNiagaraVariableWithOffset& Variable : Store.ReadParameterVariables())
		{
			FString Value;
			if (Variable.GetType() == FNiagaraTypeDefinition::GetFloatDef())
			{
				Value = FString::SanitizeFloat(Store.GetParameterValue<float>(Variable));
			}
			else if (Variable.GetType() == FNiagaraTypeDefinition::GetVec3Def())
			{
				Value = Store.GetParameterValue<FVector3f>(Variable).ToString();
			}
			else if (Variable.GetType() == FNiagaraTypeDefinition::GetColorDef())
			{
				Value = Store.GetParameterValue<FLinearColor>(Variable).ToString();
			}
			else if (Variable.GetType() == FNiagaraTypeDefinition::GetIntDef())
			{
				Value = FString::FromInt(Store.GetParameterValue<int32>(Variable));
			}
			UE_LOG(LogATC, Display, TEXT("ATC.InspectNiagara: %s %s (%s) = %s"), *Owner, *Variable.GetName().ToString(), *Variable.GetType().GetName(), *Value);
		}
	}

	void LogScripts(const FString& Owner, const FVersionedNiagaraEmitterData* Data)
	{
		TArray<UNiagaraScript*> Scripts;
		Data->GetScripts(Scripts, false);
		for (const UNiagaraScript* Script : Scripts)
		{
			if (Script)
			{
				LogStore(Owner + TEXT("/") + Script->GetName(), Script->RapidIterationParameters);
			}
		}
		for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			UE_LOG(LogATC, Display, TEXT("ATC.InspectNiagara: %s renderer %s enabled=%d"), *Owner, *Renderer->GetClass()->GetName(), Renderer->GetIsEnabled() ? 1 : 0);
		}
	}

	void Inspect(const TArray<FString>& Args)
	{
		UObject* Asset = Args.Num() > 0 ? LoadObject<UObject>(nullptr, *Args[0]) : nullptr;
		if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Asset))
		{
			LogScripts(Emitter->GetName(), Emitter->GetLatestEmitterData());
		}
		else if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset))
		{
			LogStore(TEXT("System/Spawn"), System->GetSystemSpawnScript()->RapidIterationParameters);
			LogStore(TEXT("System/Update"), System->GetSystemUpdateScript()->RapidIterationParameters);
			for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
			{
				LogScripts(Handle.GetUniqueInstanceName(), Handle.GetEmitterData());
			}
		}
		else
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.InspectNiagara <path>: not a Niagara system or emitter."));
		}
		UE_LOG(LogATC, Display, TEXT("ATC.InspectNiagara: done"));
	}

	/** Sets a module input ("GravityForce", "Gravity") in every script of the system that has it. */
	template<typename T>
	void SetInput(UNiagaraSystem* System, const TCHAR* Module, const TCHAR* Input, const FNiagaraTypeDefinition& Type, const T& Value)
	{
		bool bFound = false;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			TArray<UNiagaraScript*> Scripts = { System->GetSystemSpawnScript(), System->GetSystemUpdateScript() };
			Handle.GetEmitterData()->GetScripts(Scripts, false);
			const FNiagaraVariable Variable(Type, FName(FString::Printf(TEXT("Constants.%s.%s.%s"), *Handle.GetUniqueInstanceName(), Module, Input)));
			for (UNiagaraScript* Script : Scripts)
			{
				if (Script && Script->RapidIterationParameters.IndexOf(Variable) != INDEX_NONE)
				{
					Script->RapidIterationParameters.SetParameterValue(Value, Variable);
					bFound = true;
				}
			}
		}
		if (!bFound)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC precipitation: %s.%s not found in %s"), Module, Input, *System->GetName());
		}
	}

	UMaterial* StartParticleMaterial(const TCHAR* Name)
	{
		const FString PackageName = FString(TEXT("/ATCSim/Environment/")) + Name;
		UMaterial* Material = FPackageName::DoesPackageExist(PackageName) ? LoadObject<UMaterial>(nullptr, *(PackageName + TEXT(".") + Name)) : nullptr;
		if (!Material)
		{
			Material = NewObject<UMaterial>(CreatePackage(*PackageName), Name, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Material);
		}
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->bUsedWithNiagaraSprites = true;
		return Material;
	}

	/**
	 * How much precipitation is drawn: MPC_Environment.PrecipitationRate (0..1) against a fixed random number per
	 * particle. Niagara gives every particle a random size between SizeMin and SizeMax that never changes, so the size
	 * is that random number. Rate 0.3 draws 30 % of the particles; the effect itself always runs at full count.
	 */
	UMaterialExpression* RateMask(UMaterial* Material, float SizeMin, float SizeMax, int32 X, int32 Y)
	{
		auto Make = [Material](UClass* Class, int32 InX, int32 InY) { return UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, InX, InY); };
		UMaterialExpression* Size = Make(UMaterialExpressionParticleSize::StaticClass(), X - 800, Y);
		UMaterialExpressionComponentMask* SizeX = Cast<UMaterialExpressionComponentMask>(Make(UMaterialExpressionComponentMask::StaticClass(), X - 650, Y));
		SizeX->R = true;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Size, TEXT(""), SizeX, TEXT(""));
		UMaterialExpressionSubtract* FromMin = Cast<UMaterialExpressionSubtract>(Make(UMaterialExpressionSubtract::StaticClass(), X - 500, Y));
		FromMin->ConstB = SizeMin;
		UMaterialEditingLibrary::ConnectMaterialExpressions(SizeX, TEXT(""), FromMin, TEXT("A"));
		UMaterialExpressionMultiply* Random = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), X - 350, Y));
		Random->ConstB = 1.f / FMath::Max(SizeMax - SizeMin, 0.01f);
		UMaterialEditingLibrary::ConnectMaterialExpressions(FromMin, TEXT(""), Random, TEXT("A"));

		UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/ATCSim/Environment/MPC_Environment.MPC_Environment"));
		UMaterialExpressionCollectionParameter* Rate = Cast<UMaterialExpressionCollectionParameter>(Make(UMaterialExpressionCollectionParameter::StaticClass(), X - 500, Y + 150));
		Rate->Collection = Collection;
		Rate->ParameterName = TEXT("PrecipitationRate");
		Rate->ParameterId = Collection ? Collection->GetParameterId(TEXT("PrecipitationRate")) : FGuid();
		if (!Rate->ParameterId.IsValid())
		{
			UE_LOG(LogATC, Warning, TEXT("ATC precipitation: MPC_Environment has no PrecipitationRate; run ATC.CreateExampleContent."));
		}

		UMaterialExpressionSubtract* Margin = Cast<UMaterialExpressionSubtract>(Make(UMaterialExpressionSubtract::StaticClass(), X - 200, Y + 50));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Rate, TEXT(""), Margin, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Random, TEXT(""), Margin, TEXT("B"));
		UMaterialExpressionMultiply* Sharp = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), X - 100, Y + 50));
		Sharp->ConstB = 100.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Margin, TEXT(""), Sharp, TEXT("A"));
		UMaterialExpression* Mask = Make(UMaterialExpressionSaturate::StaticClass(), X, Y + 50);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Sharp, TEXT(""), Mask, TEXT(""));
		return Mask;
	}

	/** Soft round dot: a snowflake. */
	UMaterial* MakeSnowFlakeMaterial(float SizeMin, float SizeMax)
	{
		UMaterial* Material = StartParticleMaterial(TEXT("M_SnowFlake"));
		auto Make = [Material](UClass* Class, int32 X, int32 Y) { return UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y); };
		UMaterialExpression* UV = Make(UMaterialExpressionTextureCoordinate::StaticClass(), -1000, 0);
		UMaterialExpressionSubtract* Centered = Cast<UMaterialExpressionSubtract>(Make(UMaterialExpressionSubtract::StaticClass(), -850, 0));
		Centered->ConstB = 0.5f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(UV, TEXT(""), Centered, TEXT("A"));
		UMaterialExpression* Length = Make(UMaterialExpressionLength::StaticClass(), -700, 0);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Centered, TEXT(""), Length, TEXT(""));
		UMaterialExpressionMultiply* Double = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -550, 0));
		Double->ConstB = 2.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Length, TEXT(""), Double, TEXT("A"));
		UMaterialExpression* Inside = Make(UMaterialExpressionOneMinus::StaticClass(), -400, 0);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Double, TEXT(""), Inside, TEXT(""));
		UMaterialExpression* Dot = Make(UMaterialExpressionSaturate::StaticClass(), -250, 0);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Inside, TEXT(""), Dot, TEXT(""));

		UMaterialExpression* ParticleColor = Make(UMaterialExpressionParticleColor::StaticClass(), -400, -200);
		UMaterialExpressionMultiply* Shape = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -100, 0));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Dot, TEXT(""), Shape, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(ParticleColor, TEXT("A"), Shape, TEXT("B"));
		UMaterialExpressionMultiply* Opacity = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), 100, 100));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Shape, TEXT(""), Opacity, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(RateMask(Material, SizeMin, SizeMax, -100, 300), TEXT(""), Opacity, TEXT("B"));

		UMaterialEditingLibrary::ConnectMaterialProperty(ParticleColor, TEXT("RGB"), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);
		UMaterialEditingLibrary::RecompileMaterial(Material);
		Material->MarkPackageDirty();
		return Material;
	}

	/** Thin vertical line on a velocity-aligned sprite: a rain streak. */
	UMaterial* MakeRainStreakMaterial(float SizeMin, float SizeMax)
	{
		UMaterial* Material = StartParticleMaterial(TEXT("M_RainStreak"));
		auto Make = [Material](UClass* Class, int32 X, int32 Y) { return UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y); };
		UMaterialExpression* UV = Make(UMaterialExpressionTextureCoordinate::StaticClass(), -1200, 0);

		// Across the sprite: 1 in the middle, 0 at 1/24 of the width
		UMaterialExpressionComponentMask* U = Cast<UMaterialExpressionComponentMask>(Make(UMaterialExpressionComponentMask::StaticClass(), -1000, 0));
		U->R = true;
		UMaterialEditingLibrary::ConnectMaterialExpressions(UV, TEXT(""), U, TEXT(""));
		UMaterialExpressionSubtract* CenterU = Cast<UMaterialExpressionSubtract>(Make(UMaterialExpressionSubtract::StaticClass(), -850, 0));
		CenterU->ConstB = 0.5f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(U, TEXT(""), CenterU, TEXT("A"));
		UMaterialExpression* AbsU = Make(UMaterialExpressionAbs::StaticClass(), -700, 0);
		UMaterialEditingLibrary::ConnectMaterialExpressions(CenterU, TEXT(""), AbsU, TEXT(""));
		UMaterialExpressionMultiply* WidthU = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -550, 0));
		WidthU->ConstB = 24.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(AbsU, TEXT(""), WidthU, TEXT("A"));
		UMaterialExpression* SatU = Make(UMaterialExpressionSaturate::StaticClass(), -400, 0);
		UMaterialEditingLibrary::ConnectMaterialExpressions(WidthU, TEXT(""), SatU, TEXT(""));
		UMaterialExpression* Line = Make(UMaterialExpressionOneMinus::StaticClass(), -250, 0);
		UMaterialEditingLibrary::ConnectMaterialExpressions(SatU, TEXT(""), Line, TEXT(""));

		// Along the sprite: fade out at both ends
		UMaterialExpressionComponentMask* V = Cast<UMaterialExpressionComponentMask>(Make(UMaterialExpressionComponentMask::StaticClass(), -1000, 200));
		V->G = true;
		UMaterialEditingLibrary::ConnectMaterialExpressions(UV, TEXT(""), V, TEXT(""));
		UMaterialExpressionSubtract* CenterV = Cast<UMaterialExpressionSubtract>(Make(UMaterialExpressionSubtract::StaticClass(), -850, 200));
		CenterV->ConstB = 0.5f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(V, TEXT(""), CenterV, TEXT("A"));
		UMaterialExpression* AbsV = Make(UMaterialExpressionAbs::StaticClass(), -700, 200);
		UMaterialEditingLibrary::ConnectMaterialExpressions(CenterV, TEXT(""), AbsV, TEXT(""));
		UMaterialExpressionMultiply* DoubleV = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -550, 200));
		DoubleV->ConstB = 2.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(AbsV, TEXT(""), DoubleV, TEXT("A"));
		UMaterialExpression* Fade = Make(UMaterialExpressionOneMinus::StaticClass(), -400, 200);
		UMaterialEditingLibrary::ConnectMaterialExpressions(DoubleV, TEXT(""), Fade, TEXT(""));

		UMaterialExpression* ParticleColor = Make(UMaterialExpressionParticleColor::StaticClass(), -400, -200);
		UMaterialExpressionMultiply* Shape = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -100, 100));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Line, TEXT(""), Shape, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Fade, TEXT(""), Shape, TEXT("B"));
		UMaterialExpressionMultiply* Tinted = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), 50, 100));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Shape, TEXT(""), Tinted, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(ParticleColor, TEXT("A"), Tinted, TEXT("B"));
		UMaterialExpressionMultiply* Opacity = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), 200, 150));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Tinted, TEXT(""), Opacity, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(RateMask(Material, SizeMin, SizeMax, 50, 350), TEXT(""), Opacity, TEXT("B"));

		UMaterialEditingLibrary::ConnectMaterialProperty(ParticleColor, TEXT("RGB"), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);
		UMaterialEditingLibrary::RecompileMaterial(Material);
		Material->MarkPackageDirty();
		return Material;
	}

	UNiagaraSystem* MakeSystem(const TCHAR* Name, UMaterialInterface* SpriteMaterial, bool bVelocityAligned)
	{
		const FString PackageName = FString(TEXT("/ATCSim/Environment/")) + Name;
		UNiagaraSystem* System = FPackageName::DoesPackageExist(PackageName) ? LoadObject<UNiagaraSystem>(nullptr, *(PackageName + TEXT(".") + Name)) : nullptr;
		if (!System)
		{
			UNiagaraEmitter* Template = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/RecycleParticlesInView.RecycleParticlesInView"));
			if (!Template)
			{
				UE_LOG(LogATC, Error, TEXT("ATC precipitation: Niagara template RecycleParticlesInView not found."));
				return nullptr;
			}
			System = NewObject<UNiagaraSystem>(CreatePackage(*PackageName), Name, RF_Public | RF_Standalone | RF_Transactional);
			UNiagaraSystemFactoryNew::InitializeSystem(System, true);
			FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
			FAssetRegistryModule::AssetCreated(System);
		}

		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			for (UNiagaraRendererProperties* Renderer : Handle.GetEmitterData()->GetRenderers())
			{
				UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer);
				Renderer->Modify();
				Renderer->SetIsEnabled(Sprite != nullptr);
				if (Sprite)
				{
					if (SpriteMaterial)
					{
						Sprite->Material = SpriteMaterial;
					}
					Sprite->Alignment = bVelocityAligned ? ENiagaraSpriteAlignment::VelocityAligned : ENiagaraSpriteAlignment::Automatic;
				}
				Renderer->PostEditChange();
			}
		}

		System->RequestCompile(false);
		System->WaitForCompilationComplete();
		return System;
	}

	TArray<UPackage*> CreatePrecipitation()
	{
		TArray<UPackage*> Packages;
		const FNiagaraTypeDefinition FloatType = FNiagaraTypeDefinition::GetFloatDef();
		const FNiagaraTypeDefinition IntType = FNiagaraTypeDefinition::GetIntDef();
		const FNiagaraTypeDefinition VectorType = FNiagaraTypeDefinition::GetVec3Def();
		const FNiagaraTypeDefinition ColorType = FNiagaraTypeDefinition::GetColorDef();

		// Rain: 3000 streaks of 25-35 cm, falling at the terminal speed of a raindrop (~9 m/s). The sizes double as the
		// random number of the rate mask, so material and effect must use the same range
		const float RainMin = 25.f, RainMax = 35.f, SnowMin = 2.f, SnowMax = 4.f;
		UMaterial* Streak = MakeRainStreakMaterial(RainMin, RainMax);
		Packages.Add(Streak->GetPackage());
		if (UNiagaraSystem* Rain = MakeSystem(TEXT("NS_Rain"), Streak, true))
		{
			SetInput(Rain, TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count"), IntType, 3000);
			SetInput(Rain, TEXT("InitializeParticle"), TEXT("Uniform Sprite Size Min"), FloatType, RainMin);
			SetInput(Rain, TEXT("InitializeParticle"), TEXT("Uniform Sprite Size Max"), FloatType, RainMax);
			SetInput(Rain, TEXT("InitializeParticle"), TEXT("Color"), ColorType, FLinearColor(0.75f, 0.8f, 0.85f, 0.35f));
			SetInput(Rain, TEXT("GravityForce"), TEXT("Gravity"), VectorType, FVector3f(0.f, 0.f, -980.f));
			SetInput(Rain, TEXT("SolveForcesAndVelocity"), TEXT("Speed Limit"), FloatType, 900.f);
			SetInput(Rain, TEXT("ViewRecycler"), TEXT("Recycle Velocity"), VectorType, FVector3f(0.f, 0.f, -900.f));
			// Heavy drops: almost no drag or turbulence, so the streaks fall nearly straight down
			SetInput(Rain, TEXT("AerodynamicDrag"), TEXT("Aerodynamic Drag"), FloatType, 0.02f);
			SetInput(Rain, TEXT("WindForce"), TEXT("Scale"), FloatType, 0.05f);
			SetInput(Rain, TEXT("WindForce"), TEXT("Wind Speed"), VectorType, FVector3f(150.f, 0.f, 0.f));
			Rain->MarkPackageDirty();
			Packages.Add(Rain->GetPackage());
		}

		// Snow: 3000 small flakes drifting down at ~1.2 m/s with more turbulence
		UMaterial* Flake = MakeSnowFlakeMaterial(SnowMin, SnowMax);
		Packages.Add(Flake->GetPackage());
		if (UNiagaraSystem* Snow = MakeSystem(TEXT("NS_Snow"), Flake, false))
		{
			SetInput(Snow, TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count"), IntType, 3000);
			SetInput(Snow, TEXT("InitializeParticle"), TEXT("Uniform Sprite Size Min"), FloatType, SnowMin);
			SetInput(Snow, TEXT("InitializeParticle"), TEXT("Uniform Sprite Size Max"), FloatType, SnowMax);
			SetInput(Snow, TEXT("InitializeParticle"), TEXT("Color"), ColorType, FLinearColor(1.f, 1.f, 1.f, 0.9f));
			SetInput(Snow, TEXT("GravityForce"), TEXT("Gravity"), VectorType, FVector3f(0.f, 0.f, -150.f));
			SetInput(Snow, TEXT("SolveForcesAndVelocity"), TEXT("Speed Limit"), FloatType, 120.f);
			SetInput(Snow, TEXT("ViewRecycler"), TEXT("Recycle Velocity"), VectorType, FVector3f(0.f, 0.f, -100.f));
			SetInput(Snow, TEXT("WindForce"), TEXT("Scale"), FloatType, 1.5f);
			Snow->MarkPackageDirty();
			Packages.Add(Snow->GetPackage());
		}
		return Packages;
	}

	void CreatePrecipitationCommand(const TArray<FString>& Args)
	{
		UEditorLoadingAndSavingUtils::SavePackages(CreatePrecipitation(), false);
		UE_LOG(LogATC, Display, TEXT("ATC.CreatePrecipitation: done"));
	}

	FAutoConsoleCommand InspectCommand(TEXT("ATC.InspectNiagara"), TEXT("ATC.InspectNiagara <system or emitter path>: log rapid iteration parameters"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Inspect));
	FAutoConsoleCommand CreatePrecipitationCmd(TEXT("ATC.CreatePrecipitation"), TEXT("Build NS_Rain and NS_Snow from the RecycleParticlesInView template"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CreatePrecipitationCommand));
}
