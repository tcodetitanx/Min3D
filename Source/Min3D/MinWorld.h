#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MinTypes.h"
#include "MinWorld.generated.h"

// Logical world: one 3D cell lattice holding terrain and excavation state.
// Simulation only - no rendering.
UCLASS()
class UMinWorld : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	void Generate(int32 Seed);

	FMinCell& Cell(const FIntVector& C) { return Cells[Min::CellIndex(C)]; }
	const FMinCell& Cell(const FIntVector& C) const { return Cells[Min::CellIndex(C)]; }
	FMinCell& PlaneCell(int32 P, int32 U, int32 V) { return Cell(Min::PlaneToCell(P, U, V)); }
	const FMinCell& PlaneCell(int32 P, int32 U, int32 V) const { return Cell(Min::PlaneToCell(P, U, V)); }

	bool IsSolid(const FIntVector& C) const;
	bool IsDiggable(const FIntVector& C) const;
	bool IsOpen(const FIntVector& C) const;
	// first non-sky row at a column of plane P
	int32 SurfaceV(int32 P, int32 U) const;

	// density used by the renderer for smooth terrain boundaries (>0.5 = solid),
	// sampled in plane-local coordinates
	float VisualDensity(int32 P, float U, float V) const;

	// Excavate a cell (marks dirty region for renderer). Returns false if not diggable.
	bool Excavate(const FIntVector& C);

	int32 WorldSeed = 0;

	// dirty plane-local rectangles for the texture renderer; index = plane
	TArray<FIntRect> DirtyRects;
	void MarkDirtyCell(const FIntVector& C, int32 Radius = 2);

	// deterministic noise helpers (value-noise FBM)
	static float Hash01(int32 X, int32 Y, int32 Seed);
	static float Hash01_3(int32 X, int32 Y, int32 Z, int32 Seed);
	static float ValueNoise(float X, float Y, int32 Seed);
	static float ValueNoise3(float X, float Y, float Z, int32 Seed);
	static float Fbm(float X, float Y, int32 Seed, int32 Octaves = 4, float Lac = 2.f, float Gain = 0.5f);
	static float Fbm3(float X, float Y, float Z, int32 Seed, int32 Octaves = 3, float Lac = 2.f, float Gain = 0.5f);

private:
	TArray<FMinCell> Cells;
};
