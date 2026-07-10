#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MinTypes.h"
#include "MinWorld.generated.h"

// Logical world: terrain cells, deposits, excavation. Simulation only - no rendering.
UCLASS()
class UMinWorld : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	void Generate(int32 Seed);

	FMinCell& Cell(int32 X, int32 Y, int32 P) { return Cells[Min::CellIndex(X, Y, P)]; }
	const FMinCell& Cell(int32 X, int32 Y, int32 P) const { return Cells[Min::CellIndex(X, Y, P)]; }
	FMinCell& Cell(const FIntVector& C) { return Cells[Min::CellIndex(C.X, C.Y, C.Z)]; }

	// density used by the renderer for smooth terrain boundaries (>0.5 = solid)
	float VisualDensity(float X, float Y, int32 P) const;

	bool IsSolid(int32 X, int32 Y, int32 P) const;
	bool IsDiggable(int32 X, int32 Y, int32 P) const;
	bool IsOpen(int32 X, int32 Y, int32 P) const; // excavated / cavern / sky

	// Excavate a cell (marks dirty region for renderer). Returns false if not diggable.
	bool Excavate(int32 X, int32 Y, int32 P);

	TArray<FMinDeposit> Deposits;
	int32 WorldSeed = 0;
	TArray<FIntVector> ExcavatedList; // every player-excavated cell, for saves

	// dirty rectangles per plane for the texture renderer
	TArray<FIntRect> DirtyRects; // index = plane; IsEmpty when clean
	void MarkDirty(int32 X, int32 Y, int32 P, int32 Radius = 2);

	// deterministic noise helpers (value-noise FBM)
	static float Hash01(int32 X, int32 Y, int32 Seed);
	static float ValueNoise(float X, float Y, int32 Seed);
	static float Fbm(float X, float Y, int32 Seed, int32 Octaves = 4, float Lac = 2.f, float Gain = 0.5f);

private:
	TArray<FMinCell> Cells;
	void PlaceDeposits(FRandomStream& R);
	void CarveDeposit(EMinResource Res, int32 P, int32 CX, int32 CY, int32 Radius, int32 Amount, FRandomStream& R, bool bVein);
};
