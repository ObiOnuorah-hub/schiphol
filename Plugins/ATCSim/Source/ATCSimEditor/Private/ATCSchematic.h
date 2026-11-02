#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

class UMaterialInterface;
class UStaticMesh;

/** Simple meshes from map data: flat polygons, strips along lines and extruded footprints. */
namespace ATCSchematic
{
	class FMeshBuilder
	{
	public:
		FMeshBuilder();

		/** Facing: the side that must be visible (the triangle order is fixed to match it). */
		void Triangle(UMaterialInterface* Material, FVector A, FVector B, FVector C, const FVector& Facing);
		/** Flat polygon at height Z, facing up. Outline without holes, in either direction. */
		bool Polygon(UMaterialInterface* Material, const TArray<FVector2D>& Outline, double Z);
		/** Strip of Width along a line at height Z, with round joints so bends have no gaps. */
		void Ribbon(UMaterialInterface* Material, const TArray<FVector>& Line, double Width, double Z, bool bRoundJoints = true);
		/** Rectangle centred on Center, Length along Direction, at height Z. */
		void Rectangle(UMaterialInterface* Material, const FVector& Center, const FVector& Direction, double Length, double Width, double Z);
		/** Footprint extruded from Bottom to Top: walls plus a flat roof. */
		void Prism(UMaterialInterface* Wall, UMaterialInterface* Roof, const TArray<FVector2D>& Outline, double Bottom, double Top);

		int32 NumTriangles() const { return Triangles; }
		/** Builds (or rebuilds) a static mesh asset; no lightmap UVs, no collision. */
		UStaticMesh* Save(const FString& PackageName);

	private:
		FPolygonGroupID GroupFor(UMaterialInterface* Material);

		FMeshDescription Description;
		FStaticMeshAttributes Attributes;
		TMap<UMaterialInterface*, FPolygonGroupID> Groups;
		TArray<UMaterialInterface*> Materials;
		int32 Triangles = 0;
	};

	/** Ear clipping; indices into the outline (closing point removed). False for degenerate outlines. */
	bool Triangulate(TArray<FVector2D> Outline, TArray<int32>& OutIndices);

	double SignedArea(const TArray<FVector2D>& Outline);
}
