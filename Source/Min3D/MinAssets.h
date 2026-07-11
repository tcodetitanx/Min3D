#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MinAssets.generated.h"

class UTexture2D;
class UStaticMesh;
class UMaterial;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

// Runtime asset factory: chroma-keyed sprites from Content/SpriteSrc, parent
// materials from /Game/Gen, and a shared unit quad mesh for all billboards.
UCLASS()
class UMinAssets : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// sprite id like "mac_00_00"; returns nullptr + warning if missing
	UTexture2D* GetSprite(const FString& Id);
	FVector2D GetSpriteSize(const FString& Id) const; // source pixels (aspect)

	UStaticMesh* GetQuad(); // 100x100 unit quad in XZ plane facing -Y

	UMaterialInstanceDynamic* MakeSpriteMID(UObject* Outer, const FString& SpriteId,
		FLinearColor Tint = FLinearColor::White, float Opacity = 1.f);
	UMaterialInstanceDynamic* MakeTexMID(UObject* Outer, UTexture2D* Tex,
		FLinearColor Tint = FLinearColor::White, float Opacity = 1.f);
	UMaterial* GetVertexMaterial() const { return VertexMat; }
	UMaterial* GetSpriteMaterial() const { return SpriteMat; }
	// SDF-edge terrain material (falls back to the sprite material if missing)
	UMaterial* GetTerrainMaterial() const { return TerrainMat ? TerrainMat.Get() : SpriteMat.Get(); }

	// convenience: configured no-collision quad component showing a sprite.
	// SizeUU = desired height in uu; width follows source aspect.
	// SortPriority: translucent layer (terrain 0, deposits 1, routes 2, structures 3, carts 4, fx 5)
	UStaticMeshComponent* MakeSpriteComponent(AActor* Owner, const FString& SpriteId,
		float SizeUU, FLinearColor Tint = FLinearColor::White, float Opacity = 1.f,
		int32 SortPriority = 3);

private:
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> SpriteCache;
	UPROPERTY() TObjectPtr<UStaticMesh> QuadMesh;
	UPROPERTY() TObjectPtr<UMaterial> SpriteMat;
	UPROPERTY() TObjectPtr<UMaterial> VertexMat;
	UPROPERTY() TObjectPtr<UMaterial> TerrainMat;
	TMap<FString, FVector2D> SpriteSizes; // from manifest
	FString SpriteDir;
	void LoadManifest();
};
