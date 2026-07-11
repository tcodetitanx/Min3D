#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MinTypes.h"
#include "MinPlaneActor.generated.h"

class UMinWorld;
class UMinAssets;
class UMinSim;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;

// Renders one 2D plane of the world - either a front plane or a perpendicular
// side plane. Everything is built in plane-local space; the actor transform
// (rotation for side planes) places it in the world.
UCLASS()
class AMinPlaneActor : public AActor
{
	GENERATED_BODY()

public:
	AMinPlaneActor();

	void Init(int32 InPlane, UMinWorld* InWorld, UMinAssets* InAssets);
	void RebuildTerrainTexture(const FIntRect& CellRect);

	// sim-driven rendering
	void RebuildRoutes(UMinSim* Sim);
	void RebuildMarkers(UMinSim* Sim); // pools, plants, pins, overflow rings, depot
	void UpdateCarts(UMinSim* Sim);
	void ShowPreview(const TArray<FIntPoint>& Cells, bool bValid, bool bErase);
	void ClearPreview();

	// visibility control (plane ghosting / matrix view)
	void SetViewOpacity(float TerrainOpacity, float ContentOpacity);

	int32 Plane = 0;

	UPROPERTY() TObjectPtr<UStaticMeshComponent> BackQuad;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TerrainQuad;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> RouteMesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> MarkerMesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> GhostRouteMesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BackMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TerrainMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RouteMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MarkerMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PreviewMID;
	UPROPERTY() TObjectPtr<UTexture2D> TerrainTex;
	UPROPERTY() TObjectPtr<UTexture2D> BackTex;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> DepotSprite;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> CartPool;
	UPROPERTY() TObjectPtr<UMinWorld> World;
	UPROPERTY() TObjectPtr<UMinAssets> Assets;

	float ContentOpacityCur = 1.f;

private:
	TArray<FColor> TerrainPixels; // RGB solid color, A = density
	TArray<FColor> BackPixels;    // open-space colors behind the terrain mask
	void ShadePixel(float CU, float CV, FColor& OutSolid, FColor& OutBack) const;
};
