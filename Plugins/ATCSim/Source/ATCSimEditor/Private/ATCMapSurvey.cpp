// Survey of an imported airport map (EHAM): where are the towers, the painted taxi lines and the parked aircraft.
// ATC.MapSurvey logs it and draws a top-down PNG; ATC.MapPlot draws a detail with a coordinate grid.

#include "ATCMapSurvey.h"
#include "ATCUnits.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorLoaderAdapter.h"

namespace ATCMapSurvey
{
	void AddTriangleSamples(const FVector& A, const FVector& B, const FVector& C, float StepCm, TArray<FVector>& Out)
	{
		// Painted lines are long thin quads: sampling the longest edge gives points along the line
		const FVector* P = &A;
		const FVector* Q = &B;
		float Longest = FVector::DistSquared(A, B);
		if (FVector::DistSquared(B, C) > Longest) { P = &B; Q = &C; Longest = FVector::DistSquared(B, C); }
		if (FVector::DistSquared(C, A) > Longest) { P = &C; Q = &A; Longest = FVector::DistSquared(C, A); }
		const int32 Count = FMath::Clamp(FMath::CeilToInt(FMath::Sqrt(Longest) / StepCm), 1, 10000);
		for (int32 Index = 0; Index <= Count; ++Index)
		{
			Out.Add(FMath::Lerp(*P, *Q, float(Index) / Count));
		}
	}

	bool SampleComponent(const UStaticMeshComponent* Component, const FString& Filter, float StepCm, TArray<FVector>& OutPoints)
	{
		UStaticMesh* Mesh = Component->GetStaticMesh();
		if (!Mesh || !Component->IsVisible())
		{
			return false;
		}

		// The filter matches a material (only those triangles) or the mesh name (all triangles)
		const bool bWholeMesh = Mesh->GetName().Contains(Filter);
		TSet<int32> MaterialIndices;
		for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
		{
			const UMaterialInterface* Material = Component->GetMaterial(Index);
			if (bWholeMesh || (Material && Material->GetName().Contains(Filter)))
			{
				MaterialIndices.Add(Index);
			}
		}
		if (MaterialIndices.IsEmpty())
		{
			return false;
		}
		const FTransform Transform = Component->GetComponentTransform();

		if (const FMeshDescription* Description = Mesh->GetMeshDescription(0))
		{
			FStaticMeshConstAttributes Attributes(*Description);
			const TPolygonGroupAttributesConstRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
			const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
			const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();

			// A polygon group belongs to a material slot by name, or by index when the import gave no names
			TSet<FPolygonGroupID> Groups;
			for (const FPolygonGroupID Group : Description->PolygonGroups().GetElementIDs())
			{
				int32 SlotIndex = Materials.IndexOfByPredicate([&](const FStaticMaterial& Slot) { return Slot.ImportedMaterialSlotName == SlotNames[Group]; });
				if (SlotIndex == INDEX_NONE || SlotNames[Group].IsNone())
				{
					SlotIndex = Group.GetValue();
				}
				if (MaterialIndices.Contains(SlotIndex))
				{
					Groups.Add(Group);
				}
			}
			for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
			{
				if (Groups.Contains(Description->GetTrianglePolygonGroup(Triangle)))
				{
					const TArrayView<const FVertexID> Vertices = Description->GetTriangleVertices(Triangle);
					AddTriangleSamples(Transform.TransformPosition(FVector(Positions[Vertices[0]])), Transform.TransformPosition(FVector(Positions[Vertices[1]])),
						Transform.TransformPosition(FVector(Positions[Vertices[2]])), StepCm, OutPoints);
				}
			}
			return true;
		}

		const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		if (!RenderData || RenderData->LODResources.Num() == 0)
		{
			return false;
		}
		const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
		TArray<uint32> Indices;
		LOD.IndexBuffer.GetCopy(Indices);
		const FPositionVertexBuffer& Positions = LOD.VertexBuffers.PositionVertexBuffer;
		if (Indices.Num() == 0 || !Positions.GetVertexData())
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.MapSurvey: no geometry data for %s"), *Mesh->GetName());
			return false;
		}
		for (const FStaticMeshSection& Section : LOD.Sections)
		{
			if (!MaterialIndices.Contains(Section.MaterialIndex))
			{
				continue;
			}
			for (uint32 Triangle = 0; Triangle < Section.NumTriangles; ++Triangle)
			{
				const uint32 First = Section.FirstIndex + Triangle * 3;
				AddTriangleSamples(Transform.TransformPosition(FVector(Positions.VertexPosition(Indices[First]))),
					Transform.TransformPosition(FVector(Positions.VertexPosition(Indices[First + 1]))),
					Transform.TransformPosition(FVector(Positions.VertexPosition(Indices[First + 2]))), StepCm, OutPoints);
			}
		}
		return true;
	}

	int32 SampleMaterial(UWorld* World, const FString& Filter, float StepCm, TArray<FVector>& OutPoints)
	{
		int32 Components = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> MeshComponents(*It);
			for (const UStaticMeshComponent* Component : MeshComponents)
			{
				Components += SampleComponent(Component, Filter, StepCm, OutPoints) ? 1 : 0;
			}
		}
		return Components;
	}

	void FNetwork::Build(const TArray<FVector>& Points, float MergeCm, float LinkCm)
	{
		Nodes.Reset();
		Links.Reset();
		Grid.Reset();
		CellCm = LinkCm;

		TSet<FIntPoint> Taken;
		for (const FVector& Point : Points)
		{
			const FIntPoint Key(FMath::FloorToInt(Point.X / MergeCm), FMath::FloorToInt(Point.Y / MergeCm));
			if (!Taken.Contains(Key))
			{
				Taken.Add(Key);
				Grid.FindOrAdd(CellOf(Point)).Add(Nodes.Add(Point));
			}
		}

		Links.SetNum(Nodes.Num());
		const float LinkSquared = LinkCm * LinkCm;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FIntPoint Cell = CellOf(Nodes[Index]);
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					if (const TArray<int32>* Others = Grid.Find(Cell + FIntPoint(DX, DY)))
					{
						for (int32 Other : *Others)
						{
							if (Other != Index && FVector::DistSquared2D(Nodes[Index], Nodes[Other]) <= LinkSquared)
							{
								Links[Index].Add(Other);
							}
						}
					}
				}
			}
		}

		ComputeParts();
		LinePart = Part;
		LineMembers = Members;
		MainLinePart = MainPart;
		AddBridges(7000.f, 25);
		ComputeParts();
		CostCache.Init(-1.f, Nodes.Num());
	}

	FVector2D FNetwork::LineDirection(int32 Node) const
	{
		const FVector2D Center(Nodes[Node]);
		double XX = 0.0, XY = 0.0, YY = 0.0;
		for (int32 Other : Links[Node])
		{
			const FVector2D D = FVector2D(Nodes[Other]) - Center;
			if (D.SizeSquared() <= CellCm * CellCm)
			{
				XX += D.X * D.X;
				XY += D.X * D.Y;
				YY += D.Y * D.Y;
			}
		}
		const double Angle = 0.5 * FMath::Atan2(2.0 * XY, XX - YY);
		return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
	}

	void FNetwork::AddBridges(float MaxBridgeCm, int32 MinPartNodes)
	{
		// A painted line that stops (a stand lead-in short of the taxilane, a taxilane short of the taxiway) is continued
		// in its own direction to the first other line ahead of it
		int32 Count = 0;
		const int32 Range = FMath::CeilToInt(MaxBridgeCm / CellCm);
		const TArray<TArray<int32>> Original = Links;
		for (int32 Node = 0; Node < Nodes.Num(); ++Node)
		{
			if (Members[Part[Node]].Num() < MinPartNodes || Original[Node].Num() == 0)
			{
				continue;
			}
			const FVector2D Center(Nodes[Node]);
			const FVector2D Along = LineDirection(Node);
			int32 Ahead = 0;
			int32 Behind = 0;
			FVector2D Mean = FVector2D::ZeroVector;
			for (int32 Other : Original[Node])
			{
				const double Projection = (FVector2D(Nodes[Other]) - Center) | Along;
				(Projection > 0.0 ? Ahead : Behind) += FMath::Abs(Projection) > 10.0 ? 1 : 0;
				Mean += FVector2D(Nodes[Other]) - Center;
			}
			if (Ahead > 0 && Behind > 0)
			{
				continue;   // not a line end
			}
			const FVector2D Out = -Mean.GetSafeNormal();

			int32 Best = INDEX_NONE;
			double BestDistance = MaxBridgeCm;
			const FIntPoint Cell = CellOf(Nodes[Node]);
			for (int32 DX = -Range; DX <= Range; ++DX)
			{
				for (int32 DY = -Range; DY <= Range; ++DY)
				{
					const TArray<int32>* Others = Grid.Find(Cell + FIntPoint(DX, DY));
					if (!Others)
					{
						continue;
					}
					for (int32 Other : *Others)
					{
						if (Part[Other] == Part[Node])
						{
							continue;
						}
						const FVector2D D = FVector2D(Nodes[Other]) - Center;
						const double Distance = D.Size();
						if (Distance < BestDistance && Distance > 1.0 && (D / Distance | Out) > FMath::Cos(FMath::DegreesToRadians(20.0)))
						{
							BestDistance = Distance;
							Best = Other;
						}
					}
				}
			}
			if (Best != INDEX_NONE)
			{
				Links[Node].AddUnique(Best);
				Links[Best].AddUnique(Node);
				++Count;
			}
		}
		UE_LOG(LogATC, Display, TEXT("ATC map network: %d bridges over gaps in the paint"), Count);
	}

	void FNetwork::ComputeParts()
	{
		// Connected parts (flood fill); the largest is the taxiway system
		Part.Init(INDEX_NONE, Nodes.Num());
		Members.Reset();
		MainPart = INDEX_NONE;
		for (int32 Seed = 0; Seed < Nodes.Num(); ++Seed)
		{
			if (Part[Seed] != INDEX_NONE)
			{
				continue;
			}
			const int32 Id = Members.AddDefaulted();
			TArray<int32> Stack = { Seed };
			Part[Seed] = Id;
			while (Stack.Num() > 0)
			{
				const int32 Node = Stack.Pop(EAllowShrinking::No);
				Members[Id].Add(Node);
				for (int32 Next : Links[Node])
				{
					if (Part[Next] == INDEX_NONE)
					{
						Part[Next] = Id;
						Stack.Add(Next);
					}
				}
			}
			if (MainPart == INDEX_NONE || Members[Id].Num() > Members[MainPart].Num())
			{
				MainPart = Id;
			}
		}
	}

	float FNetwork::LineCost(int32 Node) const
	{
		if (CostCache[Node] >= 0.f)
		{
			return CostCache[Node];
		}

		const FVector2D Center(Nodes[Node]);
		float Cost = 1.f;
		if (Links[Node].Num() >= 2)
		{
			const FVector2D Along = LineDirection(Node);
			const FVector2D Side(-Along.Y, Along.X);

			// A taxiway edge line has paint (the centerline) on one side only, 5 to 14 m away
			bool bLeft = false;
			bool bRight = false;
			const int32 Range = FMath::CeilToInt(1400.f / CellCm);
			const FIntPoint Cell = CellOf(Nodes[Node]);
			for (int32 DX = -Range; DX <= Range && !(bLeft && bRight); ++DX)
			{
				for (int32 DY = -Range; DY <= Range; ++DY)
				{
					const TArray<int32>* Others = Grid.Find(Cell + FIntPoint(DX, DY));
					if (!Others)
					{
						continue;
					}
					for (int32 Other : *Others)
					{
						const FVector2D D = FVector2D(Nodes[Other]) - Center;
						const double Lateral = D | Side;
						if (FMath::Abs(D | Along) < 150.0 && FMath::Abs(Lateral) > 500.0 && FMath::Abs(Lateral) < 1400.0)
						{
							(Lateral > 0.0 ? bLeft : bRight) = true;
						}
					}
				}
			}
			Cost = bLeft != bRight ? 4.f : 1.f;
		}
		CostCache[Node] = Cost;
		return Cost;
	}

	int32 FNetwork::Nearest(const FVector& Location, float MaxDistanceCm, int32 OnlyPart) const
	{
		int32 Best = INDEX_NONE;
		float BestDistance = MaxDistanceCm * MaxDistanceCm;
		const int32 Range = FMath::CeilToInt(MaxDistanceCm / CellCm);
		const FIntPoint Cell = CellOf(Location);
		for (int32 DX = -Range; DX <= Range; ++DX)
		{
			for (int32 DY = -Range; DY <= Range; ++DY)
			{
				if (const TArray<int32>* Indices = Grid.Find(Cell + FIntPoint(DX, DY)))
				{
					for (int32 Index : *Indices)
					{
						const float Distance = FVector::DistSquared2D(Nodes[Index], Location);
						if (Distance < BestDistance && (OnlyPart == INDEX_NONE || Part[Index] == OnlyPart))
						{
							BestDistance = Distance;
							Best = Index;
						}
					}
				}
			}
		}
		return Best;
	}

	void FNetwork::Query(const FVector& Location, float RadiusCm, TArray<int32>& OutNodes) const
	{
		const int32 Range = FMath::CeilToInt(RadiusCm / CellCm);
		const FIntPoint Cell = CellOf(Location);
		for (int32 DX = -Range; DX <= Range; ++DX)
		{
			for (int32 DY = -Range; DY <= Range; ++DY)
			{
				if (const TArray<int32>* Indices = Grid.Find(Cell + FIntPoint(DX, DY)))
				{
					for (int32 Index : *Indices)
					{
						if (FVector::DistSquared2D(Nodes[Index], Location) <= RadiusCm * RadiusCm)
						{
							OutNodes.Add(Index);
						}
					}
				}
			}
		}
	}

	TArray<int32> FNetwork::FindPath(int32 Start, int32 Goal, const TArray<float>* NodeCost) const
	{
		if (!Nodes.IsValidIndex(Start) || !Nodes.IsValidIndex(Goal))
		{
			return {};
		}

		struct FOpen
		{
			float Estimate;
			int32 Node;
			bool operator<(const FOpen& Other) const { return Estimate < Other.Estimate; }
		};
		TArray<float> Cost;
		Cost.Init(TNumericLimits<float>::Max(), Nodes.Num());
		TArray<int32> From;
		From.Init(INDEX_NONE, Nodes.Num());
		TArray<FOpen> Open;
		Cost[Start] = 0.f;
		Open.HeapPush({ static_cast<float>(FVector::Dist2D(Nodes[Start], Nodes[Goal])), Start });

		while (Open.Num() > 0)
		{
			FOpen Current;
			Open.HeapPop(Current);
			if (Current.Node == Goal)
			{
				break;
			}
			if (Current.Estimate > Cost[Current.Node] + FVector::Dist2D(Nodes[Current.Node], Nodes[Goal]) + 1.0)
			{
				continue;   // stale heap entry
			}
			for (int32 Next : Links[Current.Node])
			{
				// Painted centerlines first; taxiway edge lines, bridges over unpainted gaps and other stands' lines cost more
				const float Step = FVector::Dist2D(Nodes[Current.Node], Nodes[Next]);
				const int32 Line = LinePart[Next];
				const bool bOffNetwork = Line != MainLinePart && Line != LinePart[Start] && Line != LinePart[Goal];
				const float NewCost = Cost[Current.Node] + Step * 0.5f * (LineCost(Current.Node) + LineCost(Next)) * (Step > CellCm ? 1.5f : 1.f) * (bOffNetwork ? 3.f : 1.f)
					* (NodeCost ? (*NodeCost)[Next] : 1.f);
				if (NewCost < Cost[Next])
				{
					Cost[Next] = NewCost;
					From[Next] = Current.Node;
					Open.HeapPush({ NewCost + static_cast<float>(FVector::Dist2D(Nodes[Next], Nodes[Goal])), Next });
				}
			}
		}

		TArray<int32> Path;
		if (From[Goal] == INDEX_NONE && Goal != Start)
		{
			return Path;
		}
		for (int32 Node = Goal; Node != INDEX_NONE; Node = From[Node])
		{
			Path.Insert(Node, 0);
		}
		return Path;
	}

	void FNetwork::Attach(int32 Node, float MaxBridgeCm, TArray<int32>& OutLocal, int32& OutMain) const
	{
		OutLocal.Reset();
		OutMain = INDEX_NONE;
		if (Part[Node] == MainPart)
		{
			OutMain = Node;
			return;
		}

		// The end of the loose line that is closest to the taxiway system
		float Best = MaxBridgeCm;
		int32 BestLocal = INDEX_NONE;
		for (int32 Member : Members[Part[Node]])
		{
			const int32 Main = Nearest(Nodes[Member], Best, MainPart);
			if (Main != INDEX_NONE)
			{
				Best = FVector::Dist2D(Nodes[Member], Nodes[Main]);
				BestLocal = Member;
				OutMain = Main;
			}
		}
		if (BestLocal != INDEX_NONE)
		{
			OutLocal = FindPath(Node, BestLocal);
		}
	}

	TArray<FVector> FNetwork::Route(const FVector& From, const FVector& To, float MaxBridgeCm) const
	{
		TArray<FVector> Result;
		const int32 Start = Nearest(From);
		const int32 Goal = Nearest(To);
		if (Start == INDEX_NONE || Goal == INDEX_NONE)
		{
			return Result;
		}
		if (Part[Start] == Part[Goal])
		{
			for (int32 Node : FindPath(Start, Goal))
			{
				Result.Add(Nodes[Node]);
			}
			return Result;
		}

		TArray<int32> StartLine, GoalLine;
		int32 StartMain, GoalMain;
		Attach(Start, MaxBridgeCm, StartLine, StartMain);
		Attach(Goal, MaxBridgeCm, GoalLine, GoalMain);
		if (StartMain == INDEX_NONE || GoalMain == INDEX_NONE)
		{
			return Result;
		}
		const TArray<int32> Middle = FindPath(StartMain, GoalMain);
		if (Middle.IsEmpty())
		{
			return Result;
		}
		for (int32 Node : StartLine)
		{
			Result.Add(Nodes[Node]);
		}
		for (int32 Node : Middle)
		{
			Result.Add(Nodes[Node]);
		}
		for (int32 Index = GoalLine.Num() - 1; Index >= 0; --Index)
		{
			Result.Add(Nodes[GoalLine[Index]]);
		}
		return Result;
	}

	TArray<FVector> RoundCorners(const TArray<FVector>& Points, float RadiusCm, float StepCm)
	{
		if (Points.Num() < 3)
		{
			return Points;
		}
		TArray<FVector> Result = { Points[0] };
		for (int32 Index = 1; Index < Points.Num() - 1; ++Index)
		{
			const FVector& P = Points[Index];
			const FVector In = (P - Points[Index - 1]).GetSafeNormal2D();
			const FVector Out = (Points[Index + 1] - P).GetSafeNormal2D();
			const float Turn = FMath::Acos(FMath::Clamp(FVector::DotProduct(In, Out), -1.f, 1.f));
			if (Turn < FMath::DegreesToRadians(8.f))
			{
				Result.Add(P);
				continue;
			}
			// Tangent length for this radius, limited to half of each leg so neighbouring arcs do not overlap
			const float LegIn = FVector::Dist2D(P, Points[Index - 1]);
			const float LegOut = FVector::Dist2D(P, Points[Index + 1]);
			const float Tangent = FMath::Min3(RadiusCm * FMath::Tan(Turn * 0.5f), LegIn * 0.5f, LegOut * 0.5f);
			const FVector A = P - In * Tangent;
			const FVector B = P + Out * Tangent;
			// Quadratic Bezier A-P-B: close to a circular arc and always tangent to both legs
			const int32 Steps = FMath::Clamp(FMath::CeilToInt(Tangent * Turn / StepCm), 2, 24);
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const float T = float(Step) / Steps;
				Result.Add(FMath::Lerp(FMath::Lerp(A, P, T), FMath::Lerp(P, B, T), T));
			}
		}
		Result.Add(Points.Last());
		return Result;
	}

	TArray<FVector> Simplify(const TArray<FVector>& Points, float ToleranceCm)
	{
		if (Points.Num() < 3)
		{
			return Points;
		}
		TArray<bool> Keep;
		Keep.Init(false, Points.Num());
		Keep[0] = Keep.Last() = true;

		TArray<TPair<int32, int32>> Stack = { { 0, Points.Num() - 1 } };
		while (Stack.Num() > 0)
		{
			const TPair<int32, int32> Span = Stack.Pop();
			float Worst = 0.f;
			int32 WorstIndex = INDEX_NONE;
			for (int32 Index = Span.Key + 1; Index < Span.Value; ++Index)
			{
				const float Distance = FMath::PointDistToSegment(Points[Index], Points[Span.Key], Points[Span.Value]);
				if (Distance > Worst)
				{
					Worst = Distance;
					WorstIndex = Index;
				}
			}
			if (WorstIndex != INDEX_NONE && Worst > ToleranceCm)
			{
				Keep[WorstIndex] = true;
				Stack.Add({ Span.Key, WorstIndex });
				Stack.Add({ WorstIndex, Span.Value });
			}
		}

		TArray<FVector> Result;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			if (Keep[Index])
			{
				Result.Add(Points[Index]);
			}
		}
		return Result;
	}

	// ---------------------------------------------------------------- Plot

	FPlot::FPlot(const FBox2D& InArea, int32 InWidth) : Area(InArea), Width(InWidth)
	{
		const FVector2D Size = Area.GetSize();
		Height = FMath::Max(1, FMath::RoundToInt(Width * Size.Y / FMath::Max(Size.X, 1.0)));
		Pixels.Init(FColor(18, 20, 24), Width * Height);
	}

	FIntPoint FPlot::ToPixel(const FVector& Location) const
	{
		const FVector2D Size = Area.GetSize();
		return FIntPoint(FMath::FloorToInt((Location.X - Area.Min.X) / Size.X * Width), FMath::FloorToInt((Location.Y - Area.Min.Y) / Size.Y * Height));
	}

	void FPlot::Set(int32 X, int32 Y, const FColor& Color)
	{
		if (X >= 0 && Y >= 0 && X < Width && Y < Height)
		{
			Pixels[Y * Width + X] = Color;
		}
	}

	void FPlot::Point(const FVector& Location, const FColor& Color, int32 Radius)
	{
		const FIntPoint P = ToPixel(Location);
		for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			for (int32 DY = -Radius; DY <= Radius; ++DY)
			{
				Set(P.X + DX, P.Y + DY, Color);
			}
		}
	}

	void FPlot::Line(const FVector& A, const FVector& B, const FColor& Color, int32 Thickness)
	{
		const FIntPoint PA = ToPixel(A);
		const FIntPoint PB = ToPixel(B);
		const int32 Steps = FMath::Max(FMath::Abs(PB.X - PA.X), FMath::Abs(PB.Y - PA.Y)) + 1;
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Alpha = float(Step) / Steps;
			const int32 X = FMath::RoundToInt(FMath::Lerp(float(PA.X), float(PB.X), Alpha));
			const int32 Y = FMath::RoundToInt(FMath::Lerp(float(PA.Y), float(PB.Y), Alpha));
			for (int32 DX = -(Thickness / 2); DX <= Thickness / 2; ++DX)
			{
				for (int32 DY = -(Thickness / 2); DY <= Thickness / 2; ++DY)
				{
					Set(X + DX, Y + DY, Color);
				}
			}
		}
	}

	void FPlot::Label(const FVector& Location, const FString& Text, const FColor& Color, int32 Scale)
	{
		// 3x5 pixel font: digits, minus, a few letters. Each glyph is 15 bits, row by row from the top.
		static const TMap<TCHAR, uint16> Glyphs = {
			{ '0', 0b111101101101111 }, { '1', 0b010110010010111 }, { '2', 0b111001111100111 }, { '3', 0b111001111001111 },
			{ '4', 0b101101111001001 }, { '5', 0b111100111001111 }, { '6', 0b111100111101111 }, { '7', 0b111001010010010 },
			{ '8', 0b111101111101111 }, { '9', 0b111101111001111 }, { '-', 0b000000111000000 }, { 'T', 0b111010010010010 },
			{ 'S', 0b111100111001111 }, { 'A', 0b111101111101101 }, { 'R', 0b110101110101101 }, { 'W', 0b101101101111101 },
			{ 'H', 0b101101111101101 }, { 'P', 0b111101111100100 }, { 'E', 0b111100111100111 }, { 'N', 0b111101101101101 },
			{ 'D', 0b110101101101110 }, { 'C', 0b111100100100111 }, { 'B', 0b110101110101110 }, { 'M', 0b101111111101101 } };

		const FIntPoint Origin = ToPixel(Location) + FIntPoint(3, 3);
		int32 Cursor = 0;
		for (const TCHAR Character : Text)
		{
			if (const uint16* Glyph = Glyphs.Find(FChar::ToUpper(Character)))
			{
				for (int32 Row = 0; Row < 5; ++Row)
				{
					for (int32 Column = 0; Column < 3; ++Column)
					{
						if (*Glyph & (1 << (14 - (Row * 3 + Column))))
						{
							for (int32 S = 0; S < Scale * Scale; ++S)
							{
								Set(Origin.X + Cursor + Column * Scale + S % Scale, Origin.Y + Row * Scale + S / Scale, Color);
							}
						}
					}
				}
			}
			Cursor += 4 * Scale;
		}
	}

	void FPlot::Grid(float StepCm, const FColor& Color, bool bLabels)
	{
		for (double X = FMath::CeilToDouble(Area.Min.X / StepCm) * StepCm; X <= Area.Max.X; X += StepCm)
		{
			Line(FVector(X, Area.Min.Y, 0.0), FVector(X, Area.Max.Y, 0.0), Color);
			if (bLabels)
			{
				Label(FVector(X, Area.Min.Y, 0.0), FString::Printf(TEXT("%d"), FMath::RoundToInt(X / 100.0)), FColor(200, 200, 200));
			}
		}
		for (double Y = FMath::CeilToDouble(Area.Min.Y / StepCm) * StepCm; Y <= Area.Max.Y; Y += StepCm)
		{
			Line(FVector(Area.Min.X, Y, 0.0), FVector(Area.Max.X, Y, 0.0), Color);
			if (bLabels)
			{
				Label(FVector(Area.Min.X, Y, 0.0), FString::Printf(TEXT("%d"), FMath::RoundToInt(Y / 100.0)), FColor(200, 200, 200));
			}
		}
	}

	bool FPlot::Save(const FString& Name) const
	{
		const FString File = FPaths::ProjectSavedDir() / TEXT("ATC") / Name + TEXT(".png");
		FImageView View(const_cast<FColor*>(Pixels.GetData()), Width, Height);
		const bool bSaved = FImageUtils::SaveImageByExtension(*File, View);
		UE_LOG(LogATC, Display, TEXT("ATC plot: %s (%dx%d)"), *File, Width, Height);
		return bSaved;
	}

	// ---------------------------------------------------------------- Survey state and commands

	struct FSurveyCache
	{
		TWeakObjectPtr<UWorld> World;
		TArray<FVector> Yellow;
		TArray<FVector> White;
		TArray<FVector> Aircraft;
		TArray<FVector> Hold;
		FNetwork Network;
	};
	FSurveyCache Cache;

	void LoadWholeWorld(UWorld* World)
	{
		static TWeakObjectPtr<UWorld> Loaded;
		UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr;
		if (!WorldPartition || Loaded == World)
		{
			return;
		}
		UWorldPartitionEditorLoaderAdapter* Adapter = WorldPartition->CreateEditorLoaderAdapter<FLoaderAdapterShape>(World,
			FBox(FVector(-HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TEXT("ATC: whole map"));
		Adapter->GetLoaderAdapter()->SetUserCreated(true);
		Adapter->GetLoaderAdapter()->Load();
		Loaded = World;
		UE_LOG(LogATC, Display, TEXT("World Partition: all actors of %s loaded"), *World->GetName());
	}

	UWorld* EditorWorld()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		LoadWholeWorld(World);
		return World;
	}

	const FSurveyCache& Survey(UWorld* World)
	{
		if (Cache.World == World && Cache.Network.Nodes.Num() > 0)
		{
			return Cache;
		}
		Cache = FSurveyCache();
		Cache.World = World;
		const int32 YellowComponents = SampleMaterial(World, TEXT("Markings_Yellow"), 100.f, Cache.Yellow);
		const int32 WhiteComponents = SampleMaterial(World, TEXT("RunwayMarkings"), 300.f, Cache.White);
		Cache.Network.Build(Cache.Yellow, 75.f, 220.f);

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				if (Component->IsVisible() && Component->GetStaticMesh() && Component->GetStaticMesh()->GetPathName().Contains(TEXT("/Vliegtuigen/")))
				{
					Cache.Aircraft.Add(It->GetComponentsBoundingBox().GetCenter());
					break;
				}
			}
			// The runway-holding position markings are separate actors in the imported EHAM map
			if (It->GetActorLabel().StartsWith(TEXT("HoldMarkings")))
			{
				for (const UStaticMeshComponent* Component : Components)
				{
					SampleComponent(Component, TEXT("Markings_Yellow"), 50.f, Cache.Hold);
				}
			}
		}
		UE_LOG(LogATC, Display, TEXT("ATC.MapSurvey: yellow %d components %d points -> %d nodes; white %d components %d points; %d parked aircraft"),
			YellowComponents, Cache.Yellow.Num(), Cache.Network.Nodes.Num(), WhiteComponents, Cache.White.Num(), Cache.Aircraft.Num());
		return Cache;
	}

	const FNetwork& PaintNetwork(UWorld* World)
	{
		return Survey(World).Network;
	}

	const TArray<FVector>& HoldMarkingPoints(UWorld* World)
	{
		return Survey(World).Hold;
	}

	const TArray<FVector>& RunwayMarkingPoints(UWorld* World)
	{
		return Survey(World).White;
	}

	void Draw(FPlot& Plot, const FSurveyCache& Data, int32 PointRadius)
	{
		for (const FVector& Point : Data.White)
		{
			Plot.Point(Point, FColor(110, 110, 110), 0);
		}
		for (const FVector& Node : Data.Network.Nodes)
		{
			Plot.Point(Node, FColor(240, 200, 30), PointRadius);
		}
		for (const FVector& Location : Data.Aircraft)
		{
			Plot.Point(Location, FColor(60, 140, 255), PointRadius + 3);
		}
	}

	void MapSurvey(const TArray<FString>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			return;
		}

		// Marking materials and how often they are used
		TMap<FString, int32> MaterialUse;
		struct FTall { FString Name; FVector Center; float Height; float Top; };
		TArray<FTall> Tall;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				if (!Component->GetStaticMesh() || !Component->IsVisible())
				{
					continue;
				}
				for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
				{
					if (const UMaterialInterface* Material = Component->GetMaterial(Index); Material && Material->GetName().StartsWith(TEXT("Decal")))
					{
						MaterialUse.FindOrAdd(Material->GetName())++;
					}
				}
				const FBox Box = Component->Bounds.GetBox();
				const FVector Size = Box.GetSize();
				if (Size.Z > 2500.f && FMath::Max(Size.X, Size.Y) < 8000.f)
				{
					Tall.Add({ Component->GetStaticMesh()->GetName(), Box.GetCenter(), static_cast<float>(Size.Z), static_cast<float>(Box.Max.Z) });
				}
			}
		}
		MaterialUse.ValueSort([](int32 A, int32 B) { return A > B; });
		for (const TPair<FString, int32>& Pair : MaterialUse)
		{
			UE_LOG(LogATC, Display, TEXT("ATC.MapSurvey: material %-45s %d components"), *Pair.Key, Pair.Value);
		}

		const FSurveyCache& Data = Survey(World);
		FBox Bounds(ForceInit);
		if (Data.Yellow.Num() > 0)
		{
			Bounds = FBox(Data.Yellow).ExpandBy(FVector(50000.f, 50000.f, 0.f));
		}
		else
		{
			// No painted lines: everything with a reasonable size (skips sky spheres and horizon panoramas)
			for (const FTall& Item : Tall)
			{
				Bounds += Item.Center;
			}
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				TInlineComponentArray<UStaticMeshComponent*> Components(*It);
				for (const UStaticMeshComponent* Component : Components)
				{
					if (Component->GetStaticMesh() && Component->Bounds.BoxExtent.Size2D() < 300000.0)
					{
						Bounds += Component->Bounds.GetBox();
					}
				}
			}
		}
		if (!Bounds.IsValid)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.MapSurvey: nothing found in this map."));
			return;
		}

		Tall.RemoveAll([&Bounds](const FTall& Item) { return !Bounds.IsInsideXY(Item.Center); });
		Tall.Sort([](const FTall& A, const FTall& B) { return A.Height > B.Height; });
		Tall.SetNum(FMath::Min(Tall.Num(), 30));

		FPlot Plot(FBox2D(FVector2D(Bounds.Min), FVector2D(Bounds.Max)), 2400);
		Plot.Grid(100000.f, FColor(45, 50, 60), true);
		Draw(Plot, Data, 0);
		if (Data.Yellow.IsEmpty())
		{
			// No paint: the outline of every object shows the layout
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				TInlineComponentArray<UStaticMeshComponent*> Components(*It);
				for (const UStaticMeshComponent* Component : Components)
				{
					const FBox Box = Component->Bounds.GetBox();
					if (Component->GetStaticMesh() && Component->IsVisible() && Box.GetExtent().Size2D() < 300000.0)
					{
						const FColor Color = Box.GetSize().Z > 1500.0 ? FColor(200, 120, 60) : FColor(100, 100, 100);
						Plot.Line(FVector(Box.Min.X, Box.Min.Y, 0.0), FVector(Box.Max.X, Box.Min.Y, 0.0), Color);
						Plot.Line(FVector(Box.Max.X, Box.Min.Y, 0.0), FVector(Box.Max.X, Box.Max.Y, 0.0), Color);
						Plot.Line(FVector(Box.Max.X, Box.Max.Y, 0.0), FVector(Box.Min.X, Box.Max.Y, 0.0), Color);
						Plot.Line(FVector(Box.Min.X, Box.Max.Y, 0.0), FVector(Box.Min.X, Box.Min.Y, 0.0), Color);
					}
				}
			}
		}
		for (int32 Index = 0; Index < Tall.Num(); ++Index)
		{
			UE_LOG(LogATC, Display, TEXT("ATC.MapSurvey: tall #%d %-35s height %.0f m top %.0f m at X=%.0f Y=%.0f"), Index, *Tall[Index].Name,
				Tall[Index].Height / 100.f, Tall[Index].Top / 100.f, Tall[Index].Center.X, Tall[Index].Center.Y);
			Plot.Point(Tall[Index].Center, FColor::Red, 4);
			Plot.Label(Tall[Index].Center, FString::FromInt(Index), FColor::Red, 3);
		}
		UE_LOG(LogATC, Display, TEXT("ATC.MapSurvey: area X %.0f..%.0f Y %.0f..%.0f"), Bounds.Min.X, Bounds.Max.X, Bounds.Min.Y, Bounds.Max.Y);
		Plot.Save(TEXT("survey"));
	}

	/** ATC.MapPlot <minX> <minY> <maxX> <maxY> <name> [grid m]: detail plot with coordinates in metres. */
	void MapPlot(const TArray<FString>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World || Args.Num() < 5)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.MapPlot <minX> <minY> <maxX> <maxY> <name> [grid m] (coordinates in metres)"));
			return;
		}
		const FBox2D Area(FVector2D(FCString::Atof(*Args[0]), FCString::Atof(*Args[1])) * 100.0, FVector2D(FCString::Atof(*Args[2]), FCString::Atof(*Args[3])) * 100.0);
		const float GridM = Args.Num() > 5 ? FCString::Atof(*Args[5]) : 50.f;
		FPlot Plot(Area, 1600);
		Plot.Grid(GridM * 100.f, FColor(45, 50, 60), true);
		Draw(Plot, Survey(World), 1);
		Plot.Save(Args[4]);
	}

	/** ATC.MapNear <x m> <y m> <radius m>: meshes around a point, with a height profile (widest extent per 2 m) of the tallest one. */
	void MapNear(const TArray<FString>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World || Args.Num() < 3)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.MapNear <x m> <y m> <radius m>"));
			return;
		}
		const FVector Center(FCString::Atof(*Args[0]) * 100.0, FCString::Atof(*Args[1]) * 100.0, 0.0);
		const double Radius = FCString::Atof(*Args[2]) * 100.0;

		const UStaticMeshComponent* Tallest = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				if (!Component->GetStaticMesh() || !Component->IsVisible())
				{
					continue;
				}
				const FBox Box = Component->Bounds.GetBox();
				if (FVector::Dist2D(Box.GetCenter(), Center) > Radius || FMath::Max(Box.GetSize().X, Box.GetSize().Y) > 4.0 * Radius)
				{
					continue;
				}
				UE_LOG(LogATC, Display, TEXT("ATC.MapNear: %-30s %-30s at X=%.0f Y=%.0f m size %.0fx%.0fx%.0f m Z %.1f..%.1f m"), *It->GetActorLabel(), *Component->GetStaticMesh()->GetName(),
					Box.GetCenter().X / 100.0, Box.GetCenter().Y / 100.0, Box.GetSize().X / 100.0, Box.GetSize().Y / 100.0, Box.GetSize().Z / 100.0, Box.Min.Z / 100.0, Box.Max.Z / 100.0);
				if (!Tallest || Box.Max.Z > Tallest->Bounds.GetBox().Max.Z)
				{
					Tallest = Component;
				}
			}
		}

		const FMeshDescription* Description = Tallest ? Tallest->GetStaticMesh()->GetMeshDescription(0) : nullptr;
		if (!Description)
		{
			return;
		}
		// Widest horizontal extent per 2 m slice: a control tower cab shows up as a wide band near the top
		FStaticMeshConstAttributes Attributes(*Description);
		const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		const FTransform Transform = Tallest->GetComponentTransform();
		TMap<int32, FBox2D> Slices;
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector World3 = Transform.TransformPosition(FVector(Positions[Vertex]));
			FBox2D& Slice = Slices.FindOrAdd(FMath::FloorToInt(World3.Z / 200.0), FBox2D(ForceInit));
			Slice += FVector2D(World3);
		}
		Slices.KeySort([](int32 A, int32 B) { return A > B; });
		for (const TPair<int32, FBox2D>& Pair : Slices)
		{
			UE_LOG(LogATC, Display, TEXT("ATC.MapNear: profile Z %3d..%3d m  %5.1f x %5.1f m  center X=%.1f Y=%.1f m"), Pair.Key * 2, Pair.Key * 2 + 2,
				Pair.Value.GetSize().X / 100.0, Pair.Value.GetSize().Y / 100.0, Pair.Value.GetCenter().X / 100.0, Pair.Value.GetCenter().Y / 100.0);
		}
	}

	/** ATC.MapPath <x1> <y1> <x2> <y2> <name> (metres): shortest path over the painted lines, plotted in red. */
	void MapPath(const TArray<FString>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World || Args.Num() < 5)
		{
			UE_LOG(LogATC, Warning, TEXT("ATC.MapPath <x1> <y1> <x2> <y2> <name> (metres)"));
			return;
		}
		const FSurveyCache& Data = Survey(World);
		const FVector From(FCString::Atof(*Args[0]) * 100.0, FCString::Atof(*Args[1]) * 100.0, 0.0);
		const FVector To(FCString::Atof(*Args[2]) * 100.0, FCString::Atof(*Args[3]) * 100.0, 0.0);
		const double StartTime = FPlatformTime::Seconds();
		const TArray<FVector> Points = Data.Network.Route(From, To);
		const TArray<FVector> Simple = RoundCorners(Simplify(Points, 30.f), 3000.f);
		double Length = 0.0;
		for (int32 Index = 1; Index < Simple.Num(); ++Index)
		{
			Length += FVector::Dist2D(Simple[Index - 1], Simple[Index]);
		}
		UE_LOG(LogATC, Display, TEXT("ATC.MapPath %s: %d nodes -> %d points, %.0f m (%.1f s)"), *Args[4], Points.Num(), Simple.Num(), Length / 100.0,
			FPlatformTime::Seconds() - StartTime);

		FBox Box(Points.Num() > 0 ? Points : TArray<FVector>{ From, To });
		Box = Box.ExpandBy(FVector(5000.f, 5000.f, 0.f));
		FPlot Plot(FBox2D(FVector2D(Box.Min), FVector2D(Box.Max)), 1600);
		Plot.Grid(Box.GetSize().X > 200000.0 ? 50000.f : 10000.f, FColor(45, 50, 60), true);
		Draw(Plot, Data, 0);
		for (int32 Index = 1; Index < Simple.Num(); ++Index)
		{
			Plot.Line(Simple[Index - 1], Simple[Index], FColor::Red, 3);
		}
		Plot.Point(From, FColor::Green, 5);
		Plot.Point(To, FColor::Cyan, 5);
		Plot.Save(Args[4]);
	}

	/** ATC.MapLayers <minX> <minY> <maxX> <maxY> <name>: every component with yellow paint in its own colour, to see which one holds which lines. */
	void MapLayers(const TArray<FString>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World || Args.Num() < 5)
		{
			return;
		}
		const FBox2D Area(FVector2D(FCString::Atof(*Args[0]), FCString::Atof(*Args[1])) * 100.0, FVector2D(FCString::Atof(*Args[2]), FCString::Atof(*Args[3])) * 100.0);
		FPlot Plot(Area, 1600);
		Plot.Grid(Area.GetSize().X > 100000.0 ? 10000.f : 1000.f, FColor(45, 50, 60), true);
		int32 Index = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (UStaticMeshComponent* Component : Components)
			{
				bool bYellow = false;
				for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
				{
					bYellow |= Component->GetMaterial(Slot) && Component->GetMaterial(Slot)->GetName().Contains(TEXT("Markings_Yellow"));
				}
				if (!bYellow || !Component->IsVisible())
				{
					continue;
				}
				TArray<FVector> Points;
				SampleComponent(Component, TEXT("Markings_Yellow"), 100.f, Points);
				const FColor Color = FLinearColor::MakeFromHSV8(uint8(Index * 47), 200, 255).ToFColor(true);
				for (const FVector& Point : Points)
				{
					Plot.Point(Point, Color, 1);
				}
				UE_LOG(LogATC, Display, TEXT("ATC.MapLayers: #%d %s / %s: %d points, colour %s"), Index, *It->GetActorLabel(), *Component->GetName(), Points.Num(), *Color.ToHex());
				Plot.Label(Points.Num() > 0 ? Points[Points.Num() / 2] : FVector::ZeroVector, FString::FromInt(Index), Color, 2);
				++Index;
			}
		}
		Plot.Save(Args[4]);
	}

	/** ATC.MapConnect <link cm> <name>: which parts of the painted network are connected at this link distance. */
	void MapConnect(const TArray<FString>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World || Args.Num() < 2)
		{
			return;
		}
		const FSurveyCache& Data = Survey(World);
		FNetwork Network;
		Network.Build(Data.Yellow, 75.f, FCString::Atof(*Args[0]));

		TArray<int32> Label;
		Label.Init(INDEX_NONE, Network.Nodes.Num());
		TArray<int32> Sizes;
		for (int32 Seed = 0; Seed < Network.Nodes.Num(); ++Seed)
		{
			if (Label[Seed] != INDEX_NONE)
			{
				continue;
			}
			const int32 Id = Sizes.Add(0);
			TArray<int32> Stack = { Seed };
			Label[Seed] = Id;
			while (Stack.Num() > 0)
			{
				const int32 Node = Stack.Pop(EAllowShrinking::No);
				++Sizes[Id];
				for (int32 Next : Network.Links[Node])
				{
					if (Label[Next] == INDEX_NONE)
					{
						Label[Next] = Id;
						Stack.Add(Next);
					}
				}
			}
		}

		TArray<int32> Order;
		for (int32 Id = 0; Id < Sizes.Num(); ++Id)
		{
			Order.Add(Id);
		}
		Order.Sort([&Sizes](int32 A, int32 B) { return Sizes[A] > Sizes[B]; });
		TMap<int32, int32> Rank;
		for (int32 Index = 0; Index < FMath::Min(Order.Num(), 16); ++Index)
		{
			Rank.Add(Order[Index], Index);
			UE_LOG(LogATC, Display, TEXT("ATC.MapConnect link %s cm: part %d has %d nodes"), *Args[0], Index, Sizes[Order[Index]]);
		}
		UE_LOG(LogATC, Display, TEXT("ATC.MapConnect link %s cm: %d parts"), *Args[0], Sizes.Num());

		FBox Box(Data.Yellow);
		FPlot Plot(FBox2D(FVector2D(Box.Min), FVector2D(Box.Max)), 2400);
		Plot.Grid(50000.f, FColor(45, 50, 60), true);
		for (int32 Node = 0; Node < Network.Nodes.Num(); ++Node)
		{
			const int32* NodeRank = Rank.Find(Label[Node]);
			const FColor Color = NodeRank ? FLinearColor::MakeFromHSV8(uint8(*NodeRank * 37), 230, 255).ToFColor(true) : FColor(90, 90, 90);
			Plot.Point(Network.Nodes[Node], Color, 0);
		}
		for (int32 Index = 0; Index < FMath::Min(Order.Num(), 16); ++Index)
		{
			const int32 First = Label.IndexOfByKey(Order[Index]);
			Plot.Label(Network.Nodes[First], FString::FromInt(Index), FColor::White, 3);
		}
		Plot.Save(Args[1]);
	}

	FAutoConsoleCommand ConnectCommand(TEXT("ATC.MapConnect"), TEXT("ATC.MapConnect <link cm> <name>: connected parts of the painted network"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&MapConnect));
	FAutoConsoleCommand LayersCommand(TEXT("ATC.MapLayers"), TEXT("ATC.MapLayers <minX> <minY> <maxX> <maxY> <name>: yellow paint per component"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&MapLayers));

	FAutoConsoleCommand PathCommand(TEXT("ATC.MapPath"), TEXT("ATC.MapPath <x1> <y1> <x2> <y2> <name> (metres): shortest path over the painted lines"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&MapPath));
	/** ATC.MapFind <filter>: every mesh component whose mesh or material name contains the filter, with its bounds. */
	void MapFind(const TArray<FString>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World || Args.Num() < 1)
		{
			return;
		}
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				if (!Component->GetStaticMesh())
				{
					continue;
				}
				bool bMatch = Component->GetStaticMesh()->GetName().Contains(Args[0]);
				for (int32 Slot = 0; Slot < Component->GetNumMaterials() && !bMatch; ++Slot)
				{
					bMatch = Component->GetMaterial(Slot) && Component->GetMaterial(Slot)->GetName().Contains(Args[0]);
				}
				if (bMatch && Count++ < 60)
				{
					const FBox Box = Component->Bounds.GetBox();
					UE_LOG(LogATC, Display, TEXT("ATC.MapFind: %-28s %-32s visible %d at X=%.1f Y=%.1f m size %.0fx%.0fx%.0f m Z %.1f..%.1f m"), *It->GetActorLabel(),
						*Component->GetStaticMesh()->GetName(), Component->IsVisible() ? 1 : 0, Box.GetCenter().X / 100.0, Box.GetCenter().Y / 100.0,
						Box.GetSize().X / 100.0, Box.GetSize().Y / 100.0, Box.GetSize().Z / 100.0, Box.Min.Z / 100.0, Box.Max.Z / 100.0);
				}
			}
		}
		UE_LOG(LogATC, Display, TEXT("ATC.MapFind %s: %d components"), *Args[0], Count);
	}

	FAutoConsoleCommand FindCommand(TEXT("ATC.MapFind"), TEXT("ATC.MapFind <filter>: mesh components by mesh or material name"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&MapFind));
	FAutoConsoleCommand NearCommand(TEXT("ATC.MapNear"), TEXT("ATC.MapNear <x m> <y m> <radius m>: meshes around a point plus height profile of the tallest"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&MapNear));
	FAutoConsoleCommand SurveyCommand(TEXT("ATC.MapSurvey"), TEXT("Survey the editor map: marking materials, tall structures, taxi line network (Saved/ATC/survey.png)"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&MapSurvey));
	FAutoConsoleCommand PlotCommand(TEXT("ATC.MapPlot"), TEXT("ATC.MapPlot <minX> <minY> <maxX> <maxY> <name> [grid m]: detail plot in metres"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&MapPlot));
}
