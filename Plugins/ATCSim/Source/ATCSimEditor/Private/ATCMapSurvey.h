#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Survey of an imported airport map: painted markings as points and a walkable taxi network built from them. */
namespace ATCMapSurvey
{
	/**
	 * Points every StepCm along every triangle painted with a material whose name contains Filter, or of every mesh whose
	 * name contains Filter (visible components only). Returns the number of components that matched.
	 */
	int32 SampleMaterial(UWorld* World, const FString& Filter, float StepCm, TArray<FVector>& OutPoints);
	bool SampleComponent(const class UStaticMeshComponent* Component, const FString& Filter, float StepCm, TArray<FVector>& OutPoints);

	/** Network of painted lines: nodes on a grid of MergeCm, linked when closer than LinkCm. */
	struct FNetwork
	{
		TArray<FVector> Nodes;
		TArray<TArray<int32>> Links;
		/** Connected part of every node; the largest part is the taxiway system, small parts are stand lines and loose marks. */
		TArray<int32> Part;
		int32 MainPart = INDEX_NONE;
		/** Parts of the paint itself, before gaps were bridged: one stand lead-in line is one line part. */
		TArray<int32> LinePart;
		TArray<TArray<int32>> LineMembers;
		int32 MainLinePart = INDEX_NONE;

		void Build(const TArray<FVector>& Points, float MergeCm, float LinkCm);
		/** Nearest node, optionally only in one part. */
		int32 Nearest(const FVector& Location, float MaxDistanceCm = 5000.f, int32 OnlyPart = INDEX_NONE) const;
		/** All nodes within a radius (2D). */
		void Query(const FVector& Location, float RadiusCm, TArray<int32>& OutNodes) const;
		/**
		 * Shortest path over the painted lines (A*), as node indices. Empty when not connected. NodeCost: a cost factor
		 * per node (1 = normal), for example to find another way than a given route.
		 */
		TArray<int32> FindPath(int32 Start, int32 Goal, const TArray<float>* NodeCost = nullptr) const;
		/**
		 * Route between two locations. A start or goal on a loose line (a stand lead-in that stops short of the taxilane)
		 * follows that line to its end nearest the taxiway system and bridges the gap in a straight line.
		 */
		TArray<FVector> Route(const FVector& From, const FVector& To, float MaxBridgeCm = 8000.f) const;

		/** Direction of the painted line at a node (unit, sign arbitrary). */
		FVector2D LineDirection(int32 Node) const;
		/** 1 on a centerline or single line, higher on a taxiway edge line (paint on one side only, about a half width away). */
		float LineCost(int32 Node) const;

	private:
		void Attach(int32 Node, float MaxBridgeCm, TArray<int32>& OutLocal, int32& OutMain) const;
		void ComputeParts();
		void AddBridges(float MaxBridgeCm, int32 MinPartNodes);

		float CellCm = 200.f;
		TMap<FIntPoint, TArray<int32>> Grid;
		TArray<TArray<int32>> Members;
		mutable TArray<float> CostCache;
		FIntPoint CellOf(const FVector& Location) const { return FIntPoint(FMath::FloorToInt(Location.X / CellCm), FMath::FloorToInt(Location.Y / CellCm)); }
	};

	/** Removes points that lie within ToleranceCm of the straight line between their neighbours (Douglas-Peucker). */
	TArray<FVector> Simplify(const TArray<FVector>& Points, float ToleranceCm);

	/** Replaces sharp corners by arcs of RadiusCm (smaller when the legs are short), sampled every StepCm. */
	TArray<FVector> RoundCorners(const TArray<FVector>& Points, float RadiusCm, float StepCm = 300.f);

	/** World Partition map: load every actor in the editor (a headless editor loads none). Once per world. */
	void LoadWholeWorld(UWorld* World);

	/** Survey of the editor world: painted yellow lines as a network (cached per world). */
	const FNetwork& PaintNetwork(UWorld* World);
	/** Points of the runway-holding position markings (actors labelled HoldMarkings*). */
	const TArray<FVector>& HoldMarkingPoints(UWorld* World);
	/** Points of the white runway markings. */
	const TArray<FVector>& RunwayMarkingPoints(UWorld* World);

	/** Top-down picture for checking by eye. North is up when +X is east and +Y is south. */
	struct FPlot
	{
		FPlot(const FBox2D& InArea, int32 Width);
		void Point(const FVector& Location, const FColor& Color, int32 Radius = 0);
		void Line(const FVector& A, const FVector& B, const FColor& Color, int32 Thickness = 1);
		void Label(const FVector& Location, const FString& Text, const FColor& Color, int32 Scale = 2);
		void Grid(float StepCm, const FColor& Color, bool bLabels);
		bool Save(const FString& Name) const;

		FBox2D Area;
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;

	private:
		FIntPoint ToPixel(const FVector& Location) const;
		void Set(int32 X, int32 Y, const FColor& Color);
	};
}
