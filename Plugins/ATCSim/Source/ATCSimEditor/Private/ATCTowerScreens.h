#pragma once

#include "CoreMinimal.h"

class AGMICamera;
class AMonitorDisplay;
class UStaticMeshComponent;
class UWorld;

/** Display faces of a tower model in the map: where a camera feed can be laid over a console screen. */
namespace ATCTowerScreens
{
	struct FScreenFace
	{
		const UStaticMeshComponent* Component = nullptr;
		int32 MaterialIndex = 0;
		FVector Center = FVector::ZeroVector;
		/** Out of the screen, towards the viewer. */
		FVector Normal = FVector::ZeroVector;
		/** Right and up in the picture, for a viewer in front of it. */
		FVector Right = FVector::ZeroVector;
		FVector Up = FVector::ZeroVector;
		double WidthCm = 0.0;
		double HeightCm = 0.0;
		/** Share of the area that faces Normal (1 = flat). */
		double Flatness = 0.0;
		/** Area / (width x height) (1 = a rectangle). */
		double Fill = 0.0;
		int32 Triangles = 0;

		/** "actor label/material", for example SketchUp_Model13/TWR_Rechts_Mat: stays the same when other screens come or go. */
		FString Key() const;
	};

	/** Flat, rectangular, monitor-sized faces within RadiusCm of Eye that are turned towards it; nearest first. */
	TArray<FScreenFace> FindScreens(UWorld* World, const FVector& Eye, float RadiusCm);

	/** The screens around a working position: within 4 m of the eye, below it and at most 100 degrees from the view; nearest first. */
	TArray<FScreenFace> WorkingScreens(UWorld* World, const FVector& Eye, float ViewYaw);

	/** The screens with these keys (see FScreenFace::Key), in this order; a key that is not there is logged and left out. */
	TArray<FScreenFace> ScreensByKey(UWorld* World, const FVector& Eye, const TArray<FString>& Keys);

	/**
	 * A monitor laid over a screen of the tower model: it covers the whole face, the picture at the top and camera ID,
	 * UTC time and focal length under it. Outliner folder ATC.
	 */
	AMonitorDisplay* PlaceMonitor(UWorld* World, UClass* MonitorClass, const FScreenFace& Face, AGMICamera* Camera, int32 Number);
}
