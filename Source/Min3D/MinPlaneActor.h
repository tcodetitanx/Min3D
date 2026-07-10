#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MinTypes.h"
#include "MinPlaneActor.generated.h"

class UMinWorld;
class UMinAssets;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;

// Renders one 2D terrain plane: procedural pastel texture quad, deposit markers,
// route meshes, structures and carts are attached here by the sim renderer.
UCLASS()
class AMinPlaneActor : public AActor
{
	GENERATED_BODY()

public:
	AMinPlaneActor();

	void Init(int32 InPlane, UMinWorld* InWorld, UMinAssets* InAssets);
	void RebuildTerrainTexture(const FIntRect& CellRect);
	void RefreshDepositMarkers();

	// sim-driven rendering
	void RebuildRoutes(class UMinSim* Sim);
	void SyncStructures(class UMinSim* Sim);
	void UpdateCarts(class UMinSim* Sim);
	void ShowPreview(const TArray<FIntPoint>& Cells, bool bValid, EMinRoute Type);
	void ClearPreview();

	// visibility control (plane ghosting / matrix view)
	void SetViewOpacity(float TerrainOpacity, float ContentOpacity, bool bShowDeposits);

	int32 Plane = 0;

	UPROPERTY() TObjectPtr<UStaticMeshComponent> TerrainQuad;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> RouteMesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> GhostRouteMesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TerrainMID;
	UPROPERTY() TObjectPtr<UTexture2D> TerrainTex;
	UPROPERTY() TMap<int32, TObjectPtr<UStaticMeshComponent>> DepositSprites;
	UPROPERTY() TMap<int32, TObjectPtr<UStaticMeshComponent>> StructureSprites;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> CartPool;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RouteMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PreviewMID;
	UPROPERTY() TObjectPtr<UMinWorld> World;
	UPROPERTY() TObjectPtr<UMinAssets> Assets;

	float ContentOpacityCur = 1.f;

private:
	TArray<FString> CartPoolSprite; // sprite id currently applied to each pool quad
	TArray<FColor> Pixels;          // CPU copy of the terrain texture
	FColor ShadePixel(float CX, float CY) const;
};
