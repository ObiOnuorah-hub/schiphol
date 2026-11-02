// Schematic airport maps from OpenStreetMap, for airports without a detailed model (Rotterdam, Lelystad): runways,
// taxiways, aprons, buildings and the tower as simple meshes, with the same lighting rig, stands, taxi routes and
// scenario as the real maps. Map data (c) OpenStreetMap contributors, ODbL 1.0.

#include "ATCSchematic.h"
#include "ATCAirportSetup.h"
#include "ATCExampleContent.h"
#include "ATCMapSurvey.h"
#include "AircraftTypeData.h"
#include "AirportData.h"
#include "ATCUnits.h"
#include "Algo/Reverse.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/TextRenderComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"

namespace ATCSchematic
{
	FMeshBuilder::FMeshBuilder() : Attributes(Description)
	{
		Attributes.Register();
		Attributes.GetVertexInstanceUVs().SetNumChannels(1);
	}

	FPolygonGroupID FMeshBuilder::GroupFor(UMaterialInterface* Material)
	{
		if (const FPolygonGroupID* Existing = Groups.Find(Material))
		{
			return *Existing;
		}
		const FPolygonGroupID Group = Description.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = FName(*FString::Printf(TEXT("Slot%d"), Materials.Num()));
		Groups.Add(Material, Group);
		Materials.Add(Material);
		return Group;
	}

	void FMeshBuilder::Triangle(UMaterialInterface* Material, FVector A, FVector B, FVector C, const FVector& Facing)
	{
		// Unreal: the visible side has normal (C - A) x (B - A) (left-handed coordinates, counter-clockwise winding)
		FVector Normal = FVector::CrossProduct(C - A, B - A);
		if (Normal.SizeSquared() < 1e-4)
		{
			return;
		}
		if ((Normal | Facing) < 0.0)
		{
			Swap(B, C);
			Normal = -Normal;
		}
		Normal.Normalize();

		const FPolygonGroupID Group = GroupFor(Material);
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		TArray<FVertexInstanceID, TInlineAllocator<3>> Instances;
		for (const FVector& Point : { A, B, C })
		{
			const FVertexID Vertex = Description.CreateVertex();
			Positions[Vertex] = FVector3f(Point);
			const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
			Normals[Instance] = FVector3f(Normal);
			// UVs in metres: from above for flat faces, from the side for walls
			UVs.Set(Instance, 0, FMath::Abs(Normal.Z) > 0.5 ? FVector2f(Point.X / 100.0, Point.Y / 100.0) : FVector2f((Point.X + Point.Y) / 100.0, Point.Z / 100.0));
			Instances.Add(Instance);
		}
		Description.CreateTriangle(Group, Instances);
		++Triangles;
	}

	double SignedArea(const TArray<FVector2D>& Outline)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Outline.Num(); ++Index)
		{
			const FVector2D& P = Outline[Index];
			const FVector2D& Q = Outline[(Index + 1) % Outline.Num()];
			Area += P.X * Q.Y - Q.X * P.Y;
		}
		return 0.5 * Area;
	}

	bool Triangulate(TArray<FVector2D> Outline, TArray<int32>& OutIndices)
	{
		OutIndices.Reset();
		if (Outline.Num() > 3 && Outline[0].Equals(Outline.Last(), 1.0))
		{
			Outline.Pop();
		}
		if (Outline.Num() < 3)
		{
			return false;
		}
		// Work counter-clockwise (positive area in X/Y)
		TArray<int32> Remaining;
		for (int32 Index = 0; Index < Outline.Num(); ++Index)
		{
			Remaining.Add(Index);
		}
		if (SignedArea(Outline) < 0.0)
		{
			Algo::Reverse(Remaining);
		}

		auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };
		int32 Guard = 0;
		while (Remaining.Num() > 3 && Guard++ < 100000)
		{
			bool bClipped = false;
			for (int32 I = 0; I < Remaining.Num(); ++I)
			{
				const int32 Prev = Remaining[(I + Remaining.Num() - 1) % Remaining.Num()];
				const int32 Curr = Remaining[I];
				const int32 Next = Remaining[(I + 1) % Remaining.Num()];
				const FVector2D& A = Outline[Prev];
				const FVector2D& B = Outline[Curr];
				const FVector2D& C = Outline[Next];
				if (Cross(A, B, C) <= 0.0)
				{
					continue;   // reflex corner
				}
				bool bEar = true;
				for (int32 Other : Remaining)
				{
					if (Other == Prev || Other == Curr || Other == Next)
					{
						continue;
					}
					const FVector2D& P = Outline[Other];
					if (Cross(A, B, P) >= 0.0 && Cross(B, C, P) >= 0.0 && Cross(C, A, P) >= 0.0)
					{
						bEar = false;
						break;
					}
				}
				if (bEar)
				{
					OutIndices.Append({ Prev, Curr, Next });
					Remaining.RemoveAt(I);
					bClipped = true;
					break;
				}
			}
			if (!bClipped)
			{
				return false;   // self-intersecting outline
			}
		}
		OutIndices.Append({ Remaining[0], Remaining[1], Remaining[2] });
		return true;
	}

	bool FMeshBuilder::Polygon(UMaterialInterface* Material, const TArray<FVector2D>& Outline, double Z)
	{
		TArray<int32> Indices;
		if (!Triangulate(Outline, Indices))
		{
			return false;
		}
		for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
		{
			Triangle(Material, FVector(Outline[Indices[Index]], Z), FVector(Outline[Indices[Index + 1]], Z), FVector(Outline[Indices[Index + 2]], Z), FVector::UpVector);
		}
		return true;
	}

	void FMeshBuilder::Rectangle(UMaterialInterface* Material, const FVector& Center, const FVector& Direction, double Length, double Width, double Z)
	{
		const FVector Along = Direction.GetSafeNormal2D() * Length * 0.5;
		const FVector Side = FVector(-Direction.Y, Direction.X, 0.0).GetSafeNormal2D() * Width * 0.5;
		const FVector C(Center.X, Center.Y, Z);
		Triangle(Material, C - Along - Side, C + Along - Side, C + Along + Side, FVector::UpVector);
		Triangle(Material, C - Along - Side, C + Along + Side, C - Along + Side, FVector::UpVector);
	}

	void FMeshBuilder::Ribbon(UMaterialInterface* Material, const TArray<FVector>& Line, double Width, double Z, bool bRoundJoints)
	{
		for (int32 Index = 1; Index < Line.Num(); ++Index)
		{
			const FVector A = Line[Index - 1];
			const FVector B = Line[Index];
			const double Length = FVector::Dist2D(A, B);
			if (Length > 1.0)
			{
				Rectangle(Material, (A + B) * 0.5, B - A, Length, Width, Z);
			}
		}
		if (bRoundJoints)
		{
			// A disc at every point fills the wedge between two strips at a bend
			for (int32 Index = 1; Index < Line.Num() - 1; ++Index)
			{
				TArray<FVector2D> Disc;
				for (int32 Step = 0; Step < 12; ++Step)
				{
					const double Angle = Step * UE_DOUBLE_TWO_PI / 12.0;
					Disc.Add(FVector2D(Line[Index]) + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Width * 0.5);
				}
				Polygon(Material, Disc, Z);
			}
		}
	}

	void FMeshBuilder::Prism(UMaterialInterface* Wall, UMaterialInterface* Roof, const TArray<FVector2D>& InOutline, double Bottom, double Top)
	{
		TArray<FVector2D> Outline = InOutline;
		if (Outline.Num() > 3 && Outline[0].Equals(Outline.Last(), 1.0))
		{
			Outline.Pop();
		}
		if (Outline.Num() < 3)
		{
			return;
		}
		const bool bCounterClockwise = SignedArea(Outline) > 0.0;
		for (int32 Index = 0; Index < Outline.Num(); ++Index)
		{
			const FVector2D& P = Outline[Index];
			const FVector2D& Q = Outline[(Index + 1) % Outline.Num()];
			const FVector2D Edge = (Q - P).GetSafeNormal();
			// Outward: to the right of the edge for a counter-clockwise outline
			const FVector2D Out = bCounterClockwise ? FVector2D(Edge.Y, -Edge.X) : FVector2D(-Edge.Y, Edge.X);
			const FVector Facing(Out, 0.0);
			Triangle(Wall, FVector(P, Bottom), FVector(Q, Bottom), FVector(Q, Top), Facing);
			Triangle(Wall, FVector(P, Bottom), FVector(Q, Top), FVector(P, Top), Facing);
		}
		Polygon(Roof, Outline, Top);
	}

	UStaticMesh* FMeshBuilder::Save(const FString& PackageName)
	{
		const FString Name = FPackageName::GetLongPackageAssetName(PackageName);
		UPackage* Package = CreatePackage(*PackageName);
		UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, *Name);
		if (!Mesh && FPackageName::DoesPackageExist(PackageName))
		{
			Mesh = LoadObject<UStaticMesh>(nullptr, *(PackageName + TEXT(".") + Name));
		}
		const bool bNew = Mesh == nullptr;
		if (bNew)
		{
			Mesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		}
		Mesh->Modify();
		Mesh->GetStaticMaterials().Reset();
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			const FName Slot(*FString::Printf(TEXT("Slot%d"), Index));
			Mesh->GetStaticMaterials().Add(FStaticMaterial(Materials[Index], Slot, Slot));
		}
		Mesh->SetNumSourceModels(0);
		FStaticMeshSourceModel& Source = Mesh->AddSourceModel();
		Source.BuildSettings.bRecomputeNormals = false;
		Source.BuildSettings.bRecomputeTangents = true;
		Source.BuildSettings.bGenerateLightmapUVs = false;
		Source.BuildSettings.bRemoveDegenerates = true;
		FMeshDescription* Target = Mesh->CreateMeshDescription(0);
		*Target = Description;
		Mesh->CommitMeshDescription(0);
		Mesh->Build(false);
		Mesh->PostEditChange();
		Mesh->MarkPackageDirty();
		if (bNew)
		{
			FAssetRegistryModule::AssetCreated(Mesh);
		}
		UE_LOG(LogATC, Display, TEXT("Schematic mesh %s: %d triangles, %d materials"), *Name, Triangles, Materials.Num());
		return Mesh;
	}

	// ---------------------------------------------------------------- Schematic airport maps

	struct FConfig
	{
		const TCHAR* ICAO;
		const TCHAR* Name;
		FVector2D Tower;            // lat, lon
		const TCHAR* TowerSource;   // where the tower position comes from
		float CabFloorM;
		float ElevationFt;
		const TCHAR* StandRef;
		const TCHAR* Runway;
		const TCHAR* Callsign;
		const TCHAR* TypeICAO;
	};

	const FConfig Configs[] = {
		// Rotterdam: no tower in OSM; estimated 921 m from the runway middle (eAIP: ARP 921 m from TWR) towards the terminal
		{ TEXT("EHRD"), TEXT("Rotterdam The Hague"), FVector2D(51.95027, 4.43502), TEXT("geschat (eAIP: ARP 921 m van TWR)"), 30.f, -14.f,
		  TEXT("Stand A1"), TEXT("24"), TEXT("EZY7012"), TEXT("A320") },
		{ TEXT("EHLE"), TEXT("Lelystad"), FVector2D(52.455749, 5.524296), TEXT("OSM node 1494764972, hoogte geschat"), 25.f, -13.f,
		  TEXT("L6"), TEXT("23"), TEXT("EZY7250"), TEXT("A320") },
		// Groningen and Maastricht have maps in the project (Content/EHGG, Content/EHBK), but their airport objects are
		// imported at 1:100 (the tower is under half a metre), so they get a schematic map too
		{ TEXT("EHGG"), TEXT("Groningen Eelde"), FVector2D(53.128579, 6.587920), TEXT("OSM node 7227855766, hoogte geschat"), 25.f, 17.f,
		  TEXT("@53.12789,6.58538,315"), TEXT("23"), TEXT("EZY8920"), TEXT("A320") },
		{ TEXT("EHBK"), TEXT("Maastricht Aachen"), FVector2D(50.915004, 5.768546), TEXT("OSM way 278303070, 31 m hoog"), 27.f, 375.f,
		  TEXT("A2"), TEXT("21"), TEXT("EZY8813"), TEXT("A320") },
	};

	TArray<FVector> MapLine(const ATCSetup::FGeo& Geo, const ATCOsm::FElement& Element)
	{
		TArray<FVector> Line;
		for (const FVector2D& Point : Element.Points)
		{
			Line.Add(Geo.ToMap(Point));
		}
		return Line;
	}

	TArray<FVector2D> MapOutline(const ATCSetup::FGeo& Geo, const ATCOsm::FElement& Element)
	{
		TArray<FVector2D> Outline;
		for (const FVector2D& Point : Element.Points)
		{
			Outline.Add(FVector2D(Geo.ToMap(Point)));
		}
		return Outline;
	}

	TArray<FVector2D> Octagon(const FVector2D& Center, double Radius)
	{
		TArray<FVector2D> Outline;
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const double Angle = (Step + 0.5) * UE_DOUBLE_TWO_PI / 8.0;
			Outline.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Outline;
	}

	double Metres(const FString& Text, double Default)
	{
		const double Value = FCString::Atod(*Text);
		return Value > 0.0 ? Value * 100.0 : Default;
	}

	void BuildSchematicMap(const TArray<FString>& Args)
	{
		const FConfig* Config = nullptr;
		for (const FConfig& Candidate : Configs)
		{
			if (Args.Num() > 0 && Args[0] == Candidate.ICAO)
			{
				Config = &Candidate;
			}
		}
		if (!Config)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.BuildSchematicMap <EHRD|EHLE|EHGG|EHBK>"));
			return;
		}
		const FString ICAO = Config->ICAO;

		ATCSetup::FSite Site;
		Site.ICAO = ICAO;
		Site.Geo.Latitude = Config->Tower.X;
		Site.Geo.Longitude = Config->Tower.Y;
		Site.Geo.NorthYawDeg = -90.f;   // like EHAM: +X east, +Y south, tower at the origin
		if (!Site.Osm.Load(ICAO))
		{
			return;
		}
		Site.Runways = ATCSetup::MapRunways(Site.Geo, ATCOsm::Runways(Site.Osm), {});
		ATCSetup::AddOsmHoldingAndAprons(Site);
		ATCSetup::BuildOsmNetwork(Site, [](const FVector&) { return 8.0; });

		// Materials: plain colours that still react to wetness and snow (MPC_Environment)
		UMaterialParameterCollection* Collection = ATCExample::MakeEnvironmentParameters();
		const FString Folder = TEXT("/Game/Airports/Schematic/Materials");
		UMaterialInterface* Grass = ATCExample::MakeGroundMaterial(Folder, TEXT("M_SchematicGrass"), FLinearColor(0.09f, 0.13f, 0.06f), Collection, 0.95f);
		UMaterialInterface* RunwayMaterial = ATCExample::MakeGroundMaterial(Folder, TEXT("M_SchematicRunway"), FLinearColor(0.06f, 0.06f, 0.06f), Collection, 0.8f);
		UMaterialInterface* Taxiway = ATCExample::MakeGroundMaterial(Folder, TEXT("M_SchematicTaxiway"), FLinearColor(0.1f, 0.1f, 0.095f), Collection, 0.8f);
		UMaterialInterface* Apron = ATCExample::MakeGroundMaterial(Folder, TEXT("M_SchematicApron"), FLinearColor(0.3f, 0.3f, 0.28f), Collection, 0.8f);
		UMaterialInterface* White = ATCExample::MakeGroundMaterial(Folder, TEXT("M_SchematicMarkingWhite"), FLinearColor(0.75f, 0.75f, 0.75f), Collection, 0.6f);
		UMaterialInterface* Wall = ATCExample::MakeGroundMaterial(Folder, TEXT("M_SchematicBuilding"), FLinearColor(0.45f, 0.45f, 0.42f), Collection, 0.8f);
		UMaterialInterface* Roof = ATCExample::MakeGroundMaterial(Folder, TEXT("M_SchematicRoof"), FLinearColor(0.2f, 0.2f, 0.2f), Collection, 0.8f);
		UMaterialInterface* Glass = ATCExample::MakeGroundMaterial(Folder, TEXT("M_SchematicGlass"), FLinearColor(0.03f, 0.05f, 0.07f), Collection, 0.1f);
		UMaterialInterface* Yellow = LoadObject<UMaterialInterface>(nullptr, TEXT("/ATCSim/Materials/M_TaxiLine.M_TaxiLine"));
		TArray<UPackage*> Packages = { Collection->GetPackage() };
		for (UMaterialInterface* Material : { Grass, RunwayMaterial, Taxiway, Apron, White, Wall, Roof, Glass })
		{
			Packages.AddUnique(Material->GetPackage());
		}

		// Ground: grass, aprons, taxiways, runways (each a few centimetres higher than the one below)
		FMeshBuilder Ground;
		const double Half = 3000000.0;   // 30 km: from a 30 m tower the horizon is about 20 km away
		Ground.Polygon(Grass, { FVector2D(-Half, -Half), FVector2D(Half, -Half), FVector2D(Half, Half), FVector2D(-Half, Half) }, 0.0);
		int32 FailedAprons = 0;
		for (const ATCOsm::FElement* Element : Site.Osm.WithTag(TEXT("aeroway"), TEXT("apron")))
		{
			FailedAprons += Element->IsClosed() && !Ground.Polygon(Apron, MapOutline(Site.Geo, *Element), 4.0) ? 1 : 0;
		}
		for (const ATCOsm::FElement& Element : Site.Osm.Elements)
		{
			const FString Kind = Element.Tag(TEXT("aeroway"));
			if (Element.bWay && !Element.IsClosed() && (Kind == TEXT("taxiway") || Kind == TEXT("taxilane")))
			{
				Ground.Ribbon(Taxiway, MapLine(Site.Geo, Element), Metres(Element.Tag(TEXT("width")), Kind == TEXT("taxiway") ? 2300.0 : 1500.0), 6.0);
			}
		}
		for (const ATCSetup::FMapRunway& Runway : Site.Runways)
		{
			Ground.Ribbon(RunwayMaterial, { Runway.A, Runway.B }, Runway.HalfWidthCm * 2.0, 8.0, false);
		}
		UStaticMesh* GroundMesh = Ground.Save(FString::Printf(TEXT("/Game/Airports/%s/Schematic/SM_%s_Ground"), *ICAO, *ICAO));

		// Markings: taxiway centerlines (yellow by day, green lights at night), runway markings, holding positions
		FMeshBuilder Markings;
		for (const ATCOsm::FElement& Element : Site.Osm.Elements)
		{
			const FString Kind = Element.Tag(TEXT("aeroway"));
			if (Element.bWay && !Element.IsClosed() && (Kind == TEXT("taxiway") || Kind == TEXT("taxilane") || Kind == TEXT("parking_position")))
			{
				Markings.Ribbon(Yellow, MapLine(Site.Geo, Element), 30.0, 12.0);
			}
		}
		for (const ATCSetup::FMapRunway& Runway : Site.Runways)
		{
			const FVector Along = Runway.Along();
			const FVector Side(-Along.Y, Along.X, 0.0);
			const double Length = Runway.Length();
			const double Width = Runway.HalfWidthCm * 2.0;
			// Centerline: 30 m stripes with 20 m gaps, clear of the threshold markings
			for (double T = 9000.0; T + 3000.0 < Length - 9000.0; T += 5000.0)
			{
				Markings.Rectangle(White, Runway.A + Along * (T + 1500.0), Along, 3000.0, 90.0, 12.0);
			}
			// Side stripes along both edges
			for (const double Sign : { -1.0, 1.0 })
			{
				const FVector Offset = Side * Sign * (Runway.HalfWidthCm - 150.0);
				Markings.Ribbon(White, { Runway.A + Offset, Runway.B + Offset }, 90.0, 12.0, false);
			}
			// Threshold stripes and aiming points at both ends
			for (const bool bAtA : { true, false })
			{
				const FVector End = bAtA ? Runway.A : Runway.B;
				const FVector Inward = bAtA ? Along : -Along;
				// Stripes over the width minus 3 m on each side, as wide as the gaps between them
				const int32 Stripes = Width > 5000.0 ? 16 : 12;
				const double Pitch = (Width - 600.0) / Stripes;
				for (int32 Stripe = 0; Stripe < Stripes; ++Stripe)
				{
					const double Lateral = -(Runway.HalfWidthCm - 300.0) + Pitch * (Stripe + 0.5);
					Markings.Rectangle(White, End + Inward * 2100.0 + Side * Lateral, Along, 3000.0, Pitch * 0.5, 12.0);
				}
				for (const double Sign : { -1.0, 1.0 })
				{
					Markings.Rectangle(White, End + Inward * 32250.0 + Side * Sign * 900.0, Along, 4500.0, 600.0, 12.0);
				}
			}
		}
		for (const FVector& Hold : Site.HoldMarkings)
		{
			// Runway-holding position: two solid and two dashed lines across the taxiway (drawn solid here)
			const int32 Node = Site.Network->Nearest(Hold, 1000.f);
			const FVector2D Direction = Node != INDEX_NONE ? Site.Network->LineDirection(Node) : FVector2D(1.0, 0.0);
			const FVector Along(Direction, 0.0);
			for (const double Offset : { -75.0, -45.0, 15.0, 45.0 })
			{
				Markings.Rectangle(Yellow, Hold + Along * Offset, FVector(-Along.Y, Along.X, 0.0), 2000.0, 15.0, 13.0);
			}
		}
		UStaticMesh* MarkingMesh = Markings.Save(FString::Printf(TEXT("/Game/Airports/%s/Schematic/SM_%s_Markings"), *ICAO, *ICAO));

		// Buildings: OSM footprints at their height (or levels x 3.2 m, hangars 14 m, others 7 m), and the tower
		FMeshBuilder Buildings;
		int32 BuildingCount = 0;
		for (const ATCOsm::FElement& Element : Site.Osm.Elements)
		{
			if (!Element.bWay || !Element.IsClosed() || (Element.Tag(TEXT("building")).IsEmpty() && Element.Tag(TEXT("aeroway")) != TEXT("hangar")))
			{
				continue;
			}
			const FString Kind = Element.Tag(TEXT("building")) + Element.Tag(TEXT("aeroway"));
			double Height = Metres(Element.Tag(TEXT("height")), 0.0);
			if (Height <= 0.0 && !Element.Tag(TEXT("building:levels")).IsEmpty())
			{
				Height = FCString::Atod(*Element.Tag(TEXT("building:levels"))) * 320.0;
			}
			if (Height <= 0.0)
			{
				Height = Kind.Contains(TEXT("hangar")) ? 1400.0 : Kind.Contains(TEXT("terminal")) ? 1200.0 : 700.0;
			}
			Buildings.Prism(Wall, Roof, MapOutline(Site.Geo, Element), 0.0, Height);
			++BuildingCount;
		}
		const FVector2D TowerXY(Site.Geo.ToMap(Config->Tower));
		const double CabFloor = Config->CabFloorM * 100.0;
		Buildings.Prism(Wall, Roof, Octagon(TowerXY, 250.0), 0.0, CabFloor);
		Buildings.Polygon(Roof, Octagon(TowerXY, 450.0), CabFloor);
		Buildings.Prism(Glass, Roof, Octagon(TowerXY, 450.0), CabFloor, CabFloor + 320.0);
		Buildings.Prism(Wall, Roof, Octagon(TowerXY, 520.0), CabFloor + 320.0, CabFloor + 380.0);
		UStaticMesh* BuildingMesh = Buildings.Save(FString::Printf(TEXT("/Game/Airports/%s/Schematic/SM_%s_Buildings"), *ICAO, *ICAO));
		Packages.Append({ GroundMesh->GetPackage(), MarkingMesh->GetPackage(), BuildingMesh->GetPackage() });
		UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
		UE_LOG(LogATC, Display, TEXT("ATC.BuildSchematicMap %s: %d runways, %d buildings, %d aprons failed to triangulate"), *ICAO, Site.Runways.Num(),
			BuildingCount, FailedAprons);

		// Map
		UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
		ATCExample::SpawnEnvironment(World);
		for (UStaticMesh* Mesh : { GroundMesh, MarkingMesh, BuildingMesh })
		{
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			Actor->GetStaticMeshComponent()->SetCastShadow(Mesh == BuildingMesh);
			Actor->SetActorLabel(Mesh->GetName());
		}

		// The tower view looks at the middle of the first runway
		const FVector RunwayMiddle = Site.Runways.Num() > 0 ? (Site.Runways[0].A + Site.Runways[0].B) * 0.5 : FVector(100000.0, 0.0, 0.0);
		const FVector ToRunway = RunwayMiddle - FVector(TowerXY, 0.0);
		const float ViewYaw = FMath::RadiansToDegrees(FMath::Atan2(ToRunway.Y, ToRunway.X));
		const float ViewPitch = -FMath::RadiansToDegrees(FMath::Atan2(CabFloor, ToRunway.Size2D()));
		const FTransform Cab = ATCExample::CabView(FVector(TowerXY, 0.0), CabFloor, ViewYaw, ViewPitch, 150.f);
		World->SpawnActor<APlayerStart>(Cab.GetLocation() + FVector(0.0, 0.0, 100.0), FRotator(0.f, ViewYaw, 0.f));

		// Label at the foot of the tower, readable from the cab
		const FVector LabelLocation = FVector(TowerXY, 300.0) + ToRunway.GetSafeNormal2D() * 6000.0;
		ATextRenderActor* Label = World->SpawnActor<ATextRenderActor>(LabelLocation, (FVector(TowerXY, LabelLocation.Z) - LabelLocation).Rotation());
		Label->GetTextRender()->SetText(FText::FromString(TEXT("SCHEMATISCHE KAART - OpenStreetMap")));
		Label->GetTextRender()->SetWorldSize(250.f);
		Label->GetTextRender()->SetHorizontalAlignment(EHTA_Center);
		Label->GetTextRender()->SetTextRenderColor(FColor(255, 200, 40));
		Label->SetActorLabel(TEXT("Label_Schematic"));

		// Departure
		UAircraftTypeData* Type = LoadObject<UAircraftTypeData>(nullptr, *FString::Printf(TEXT("/Game/Aircraft/%s/DA_AircraftType_%s.DA_AircraftType_%s"),
			Config->TypeICAO, Config->TypeICAO, Config->TypeICAO));
		ATCSetup::FDeparture Departure;
		Departure.StandRef = Config->StandRef;
		Departure.Runway = Config->Runway;
		Departure.Type = Type;
		// "Stand A1" -> A1; a stand placed by hand (@lat,lon,heading) is A1
		const FString StandName = Departure.StandRef.StartsWith(TEXT("@")) ? FString(TEXT("A1"))
			: FString(Config->StandRef).Replace(TEXT(" "), TEXT("")).Replace(TEXT("Stand"), TEXT(""));
		Departure.StandTag = FName(*FString::Printf(TEXT("Stand.%s.%s"), *ICAO, *StandName));
		Departure.PushRouteTag = FName(*FString::Printf(TEXT("Route.%s.%s.Pushback"), *ICAO, *StandName));
		Departure.TaxiRouteTag = FName(*FString::Printf(TEXT("Route.%s.%s.Rwy%s"), *ICAO, *StandName, Config->Runway));
		if (!ATCSetup::BuildDeparture(Site, Departure) || !ATCSetup::SpawnDeparture(World, Site, Departure))
		{
			UE_LOG(LogATC, Error, TEXT("ATC.BuildSchematicMap %s: departure failed, map not saved"), *ICAO);
			return;
		}
		ATCSetup::PlotSite(Site, { Departure }, FString::Printf(TEXT("%s_routes"), *ICAO));

		const FString MapPackage = FString::Printf(TEXT("/Game/Airports/%s/Maps/Map_%s_Schematic"), *ICAO, *ICAO);
		UEditorLoadingAndSavingUtils::SaveMap(World, MapPackage);

		// Airport data and scenario
		FTowerPosition Tower;
		Tower.Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*(TEXT("Tower.") + ICAO)), true);
		Tower.Name = FText::FromString(FString::Printf(TEXT("%s Tower (schematisch; positie %s)"), Config->Name, Config->TowerSource));
		Tower.Transform = Cab;
		ATCSetup::UpdateAirport(Site, FString::Printf(TEXT("%s (schematisch)"), Config->Name), MapPackage, { Tower }, Config->ElevationFt);
		ATCSetup::MakeDepartureScenario(Site, Departure, Config->Callsign, Tower.Tag, FString::Printf(TEXT("%s (schematisch): vertrek %s naar baan %s"),
			Config->Name, Config->StandRef, Config->Runway));
		UE_LOG(LogATC, Display, TEXT("ATC.BuildSchematicMap %s: done, map %s"), *ICAO, *MapPackage);
	}

	FAutoConsoleCommand BuildSchematicMapCommand(TEXT("ATC.BuildSchematicMap"),
		TEXT("ATC.BuildSchematicMap <EHRD|EHLE|EHGG|EHBK>: schematic airport map from OpenStreetMap, with stand, routes and scenario"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&BuildSchematicMap));
}
