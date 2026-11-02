// Screens of a tower model in the map: the flat display faces of its consoles, so camera feeds can be laid over them.

#include "ATCTowerScreens.h"
#include "AirportData.h"
#include "ATCUnits.h"
#include "GMICamera.h"
#include "MonitorDisplay.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace ATCTowerScreens
{
	namespace
	{
		/** Union-find over vertex IDs: triangles that share a vertex belong to one face. */
		int32 Root(TArray<int32>& Parent, int32 Index)
		{
			while (Parent[Index] != Index)
			{
				Parent[Index] = Parent[Parent[Index]];
				Index = Parent[Index];
			}
			return Index;
		}

		FString TextureNames(const UMaterialInterface* Material)
		{
			if (!Material)
			{
				return TEXT("-");
			}
			TArray<UTexture*> Textures;
			Material->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
			FString Names;
			for (const UTexture* Texture : Textures)
			{
				if (Texture && Names.Len() < 120)
				{
					Names += (Names.IsEmpty() ? TEXT("") : TEXT(",")) + Texture->GetName();
				}
			}
			return Names.IsEmpty() ? TEXT("-") : Names;
		}

		/** Every connected, flat-ish face per material of one component, within RadiusCm of Eye. */
		void FindFaces(const UStaticMeshComponent* Component, const FVector& Eye, float RadiusCm, TArray<FScreenFace>& OutFaces)
		{
			const UStaticMesh* Mesh = Component->GetStaticMesh();
			const FMeshDescription* Description = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
			if (!Description)
			{
				return;
			}
			FStaticMeshConstAttributes Attributes(*Description);
			const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
			const TPolygonGroupAttributesConstRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
			const FTransform Transform = Component->GetComponentTransform();

			// Material index of every polygon group, the way the component draws it
			TMap<FPolygonGroupID, int32> GroupMaterial;
			for (const FPolygonGroupID Group : Description->PolygonGroups().GetElementIDs())
			{
				const int32 Index = Mesh->GetStaticMaterials().IndexOfByPredicate([&](const FStaticMaterial& Material)
				{
					return Material.ImportedMaterialSlotName == SlotNames[Group];
				});
				GroupMaterial.Add(Group, Index == INDEX_NONE ? Group.GetValue() : Index);
			}

			TArray<int32> Parent;
			Parent.SetNumUninitialized(Description->Vertices().GetArraySize());
			for (int32 Index = 0; Index < Parent.Num(); ++Index)
			{
				Parent[Index] = Index;
			}
			for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
			{
				const TArrayView<const FVertexID> Vertices = Description->GetTriangleVertices(Triangle);
				const int32 A = Root(Parent, Vertices[0].GetValue());
				Parent[Root(Parent, Vertices[1].GetValue())] = A;
				Parent[Root(Parent, Vertices[2].GetValue())] = A;
			}

			struct FCluster
			{
				int32 Material = 0;
				TArray<FVector> Points;
				FVector AreaNormal = FVector::ZeroVector;
				double Area = 0.0;
				TArray<FVector> Normals;
				TArray<double> Areas;
			};
			TMap<TPair<int32, int32>, FCluster> Clusters;
			for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
			{
				const TArrayView<const FVertexID> Vertices = Description->GetTriangleVertices(Triangle);
				const FVector P0 = Transform.TransformPosition(FVector(Positions[Vertices[0]]));
				const FVector P1 = Transform.TransformPosition(FVector(Positions[Vertices[1]]));
				const FVector P2 = Transform.TransformPosition(FVector(Positions[Vertices[2]]));
				if (FVector::Dist(P0, Eye) > RadiusCm)
				{
					continue;
				}
				const int32 Material = GroupMaterial.FindRef(Description->GetTrianglePolygonGroup(Triangle));
				FCluster& Cluster = Clusters.FindOrAdd(TPair<int32, int32>(Material, Root(Parent, Vertices[0].GetValue())));
				Cluster.Material = Material;
				// Same rule as the engine's normals: the front of a triangle is where (P2 - P0) x (P1 - P0) points
				const FVector Cross = (P2 - P0) ^ (P1 - P0);
				const double Area = Cross.Size() * 0.5;
				Cluster.Points.Append({ P0, P1, P2 });
				Cluster.AreaNormal += Cross * 0.5;
				Cluster.Area += Area;
				Cluster.Normals.Add(Cross.GetSafeNormal());
				Cluster.Areas.Add(Area);
			}

			for (TPair<TPair<int32, int32>, FCluster>& Pair : Clusters)
			{
				const FCluster& Cluster = Pair.Value;
				const FVector Normal = Cluster.AreaNormal.GetSafeNormal();
				if (Normal.IsNearlyZero() || Cluster.Area < 100.0)
				{
					continue;
				}
				// Flat: (almost) all of the area faces the same way
				double FlatArea = 0.0;
				for (int32 Index = 0; Index < Cluster.Normals.Num(); ++Index)
				{
					FlatArea += (Cluster.Normals[Index] | Normal) > 0.995 ? Cluster.Areas[Index] : 0.0;
				}
				const FVector Up = (FVector::UpVector - Normal * (FVector::UpVector | Normal)).GetSafeNormal();
				if (Up.IsNearlyZero())
				{
					continue;
				}
				const FVector Right = Up ^ -Normal;
				FVector2D Min(BIG_NUMBER, BIG_NUMBER);
				FVector2D Max(-BIG_NUMBER, -BIG_NUMBER);
				const FVector Origin = Cluster.Points[0];
				for (const FVector& Point : Cluster.Points)
				{
					const FVector2D Local((Point - Origin) | Right, (Point - Origin) | Up);
					Min = FVector2D::Min(Min, Local);
					Max = FVector2D::Max(Max, Local);
				}
				FScreenFace Face;
				Face.Component = Component;
				Face.MaterialIndex = Cluster.Material;
				Face.Normal = Normal;
				Face.Right = Right;
				Face.Up = Up;
				Face.WidthCm = Max.X - Min.X;
				Face.HeightCm = Max.Y - Min.Y;
				Face.Center = Origin + Right * (Min.X + Max.X) * 0.5 + Up * (Min.Y + Max.Y) * 0.5;
				Face.Flatness = FlatArea / Cluster.Area;
				Face.Fill = Cluster.Area / FMath::Max(Face.WidthCm * Face.HeightCm, 1.0);
				Face.Triangles = Cluster.Normals.Num();
				OutFaces.Add(Face);
			}
		}
	}

	FString FScreenFace::Key() const
	{
		const UMaterialInterface* Material = Component->GetMaterial(MaterialIndex);
		return Component->GetOwner()->GetActorLabel() + TEXT("/") + (Material ? Material->GetName() : FString::FromInt(MaterialIndex));
	}

	TArray<FScreenFace> FindScreens(UWorld* World, const FVector& Eye, float RadiusCm)
	{
		TArray<FScreenFace> Screens;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			// The camera screens of an earlier setup lie over the model's screens: the model counts, not those
			if (It->IsA<AMonitorDisplay>())
			{
				continue;
			}
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				if (!Component->GetStaticMesh() || !Component->IsVisible() || Component->Bounds.GetBox().ComputeSquaredDistanceToPoint(Eye) > FMath::Square(RadiusCm))
				{
					continue;
				}
				TArray<FScreenFace> Faces;
				FindFaces(Component, Eye, RadiusCm, Faces);
				for (const FScreenFace& Face : Faces)
				{
					// A display: flat, (nearly) rectangular, monitor-sized and turned towards the eye
					const bool bSize = Face.WidthCm >= 15.0 && Face.HeightCm >= 10.0 && Face.WidthCm <= 250.0 && Face.HeightCm <= 150.0;
					if (bSize && Face.Flatness > 0.98 && Face.Fill > 0.85 && ((Eye - Face.Center) | Face.Normal) > 0.0)
					{
						Screens.Add(Face);
					}
				}
			}
		}
		Screens.Sort([&Eye](const FScreenFace& A, const FScreenFace& B) { return FVector::DistSquared(A.Center, Eye) < FVector::DistSquared(B.Center, Eye); });
		return Screens;
	}

	TArray<FScreenFace> WorkingScreens(UWorld* World, const FVector& Eye, float ViewYaw)
	{
		TArray<FScreenFace> Screens = FindScreens(World, Eye, 400.f);
		Screens.RemoveAll([&Eye, ViewYaw](const FScreenFace& Face)
		{
			const FRotator Direction = (Face.Center - Eye).Rotation();
			return Direction.Pitch > 5.f || Direction.Pitch < -70.f || FMath::Abs(FMath::FindDeltaAngleDegrees(ViewYaw, Direction.Yaw)) > 100.f;
		});
		return Screens;
	}

	TArray<FScreenFace> ScreensByKey(UWorld* World, const FVector& Eye, const TArray<FString>& Keys)
	{
		const TArray<FScreenFace> All = FindScreens(World, Eye, 1200.f);
		TArray<FScreenFace> Result;
		for (const FString& Key : Keys)
		{
			const FScreenFace* Face = All.FindByPredicate([&Key](const FScreenFace& Candidate) { return Candidate.Key().Equals(Key, ESearchCase::IgnoreCase); });
			if (Face)
			{
				Result.Add(*Face);
			}
			else
			{
				UE_LOG(LogATC, Warning, TEXT("ATC.TowerScreens: no screen %s within 12 m of the eye"), *Key);
			}
		}
		return Result;
	}

	AMonitorDisplay* PlaceMonitor(UWorld* World, UClass* MonitorClass, const FScreenFace& Face, AGMICamera* Camera, int32 Number)
	{
		// +X out of the screen towards the viewer, +Z up in the picture
		const FRotator Rotation = FRotationMatrix::MakeFromXZ(Face.Normal, Face.Up).Rotator();
		AMonitorDisplay* Monitor = World->SpawnActor<AMonitorDisplay>(MonitorClass, Face.Center, Rotation);
		Monitor->WidthCm = Face.WidthCm;
		Monitor->HeightCm = Face.HeightCm;
		Monitor->MonitorNumber = Number;
		Monitor->SourceCamera = Camera;
		Monitor->RerunConstructionScripts();
		Monitor->SetActorLabel(FString::Printf(TEXT("TowerScreen_%d"), Number));
		Monitor->SetFolderPath(TEXT("ATC"));
		return Monitor;
	}

	namespace
	{
		/** ATC.TowerScreens [tower tag] [radius m]: the display faces around the eye in that tower, nearest first. */
		void ListScreens(const TArray<FString>& Args)
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			const UAirportData* Airport = World ? UAirportData::FindForWorld(World) : nullptr;
			if (!Airport || Airport->TowerPositions.Num() == 0)
			{
				UE_LOG(LogATC, Warning, TEXT("ATC.TowerScreens: open an airport map with tower positions"));
				return;
			}
			const FTowerPosition* Tower = Args.Num() > 0 ? Airport->FindTower(UGameplayTagsManager::Get().RequestGameplayTag(FName(*Args[0]), false)) : &Airport->TowerPositions[0];
			if (!Tower)
			{
				UE_LOG(LogATC, Warning, TEXT("ATC.TowerScreens: no tower %s"), *Args[0]);
				return;
			}
			const float RadiusCm = (Args.Num() > 1 ? FCString::Atof(*Args[1]) : 12.f) * ATCUnits::MetersToCm;
			const FVector Eye = Tower->Transform.GetLocation() + FVector(0.f, 0.f, Tower->EyeHeightM * ATCUnits::MetersToCm);
			const float ViewYaw = Tower->Transform.Rotator().Yaw;

			const TArray<FScreenFace> Screens = FindScreens(World, Eye, RadiusCm);
			for (int32 Index = 0; Index < Screens.Num(); ++Index)
			{
				const FScreenFace& Face = Screens[Index];
				// Where it is seen from the eye: bearing from the default view, elevation, distance
				const FVector Delta = Face.Center - Eye;
				const UMaterialInterface* Material = Face.Component->GetMaterial(Face.MaterialIndex);
				UE_LOG(LogATC, Display, TEXT("ATC.TowerScreens #%d %s: %.0fx%.0f cm bearing %+.0f elev %+.0f dist %.2f m at X=%.2f Y=%.2f Z=%.2f m facing yaw %.0f pitch %.0f flat %.2f fill %.2f tris %d | %s slot %d | tex %s"),
					Index, *Face.Key(), Face.WidthCm, Face.HeightCm, FMath::FindDeltaAngleDegrees(ViewYaw, Delta.Rotation().Yaw), Delta.Rotation().Pitch, Delta.Size() / 100.0,
					Face.Center.X / 100.0, Face.Center.Y / 100.0, Face.Center.Z / 100.0, Face.Normal.Rotation().Yaw, Face.Normal.Rotation().Pitch,
					Face.Flatness, Face.Fill, Face.Triangles, *Face.Component->GetStaticMesh()->GetName(), Face.MaterialIndex, *TextureNames(Material));
			}
			UE_LOG(LogATC, Display, TEXT("ATC.TowerScreens %s: %d screens within %.0f m of the eye (X=%.1f Y=%.1f Z=%.1f m, view yaw %.0f)"), *Tower->Tag.ToString(),
				Screens.Num(), RadiusCm / 100.0, Eye.X / 100.0, Eye.Y / 100.0, Eye.Z / 100.0, ViewYaw);
		}

		FAutoConsoleCommand ListCommand(TEXT("ATC.TowerScreens"), TEXT("ATC.TowerScreens [tower tag] [radius m]: display faces of the tower model around the eye"),
			FConsoleCommandWithArgsDelegate::CreateStatic(&ListScreens));
	}
}
