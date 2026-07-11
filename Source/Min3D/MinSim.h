#pragma once

#include "CoreMinimal.h"
#include "MinTypes.h"
#include "MinSim.generated.h"

class UMinWorld;
class UMinAssets;
class AMinGameMode;
class UMinAudio;

// Cost/validity result for build previews
struct FMinBuildCheck
{
	bool bValid = false;
	int32 Cost = 0;
	FString Reason;
};

// The V2 game loop: a cell-based tunnel network, randomly spawning resource
// pools and processing plants, color-matched cart round trips, demand pins
// with an overflow fail timer, and a per-mode high score.
UCLASS()
class UMinSim : public UObject
{
	GENERATED_BODY()

public:
	void Init(UMinWorld* InWorld, UMinAssets* InAssets, AMinGameMode* InGM);
	void StartRun(EMinGameRules InRules);
	void Tick(float Dt);

	// ---- build / erase API ---------------------------------------------------
	FMinBuildCheck CheckRoute(const TArray<FIntPoint>& UVCells, int32 Plane) const;
	bool BuildRoute(const TArray<FIntPoint>& UVCells, int32 Plane);
	bool EraseAt(const FIntPoint& UV, int32 Plane); // removes one route section

	// route network: plain cells; the graph is implicit
	TSet<FIntVector> RouteCells;
	bool IsWalkable(const FIntVector& C) const;

	// ---- state ----------------------------------------------------------------
	TArray<FMinPool> Pools;
	TArray<FMinPlant> Plants;
	TArray<FMinCart> Carts;
	FIntVector DepotCell = FIntVector(-1, -1, -1);

	EMinGameRules Rules = EMinGameRules::SinglePlane;
	bool bRunStarted = false;
	bool bGameOver = false;
	bool bNewRecord = false;
	int32 Score = 0;
	int32 HighScore = 0;
	int32 SegmentsLeft = 30;
	int32 ColorsActive = 0;
	float PlayTime = 0.f;
	float SimSpeed = 1.f;
	bool bPaused = false;
	FString CurrentWarning;
	FString GameOverReason;

	int32 CartsInFlight() const;

	// dirty flags: plane actors rebuild meshes when set
	bool bTopologyDirty = false;
	bool bStructuresDirty = false;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMinEvent, const FString&);
	FOnMinEvent OnEvent; // toast messages for the HUD

	UMinAudio* Audio = nullptr;

	static constexpr int32 PinCap = 8;        // pins at/over this start the fail timer
	static constexpr float OverflowMax = 30.f;
	static constexpr int32 MaxPoolCarts = 2;

private:
	UPROPERTY() TObjectPtr<UMinWorld> World;
	UPROPERTY() TObjectPtr<UMinAssets> Assets;
	UPROPERTY() TObjectPtr<AMinGameMode> GM;

	FRandomStream Rng;
	float Difficulty01() const;

	// spawning
	void PlaceDepot();
	void ActivateColor(int32 ColorIdx);
	int32 PlaneForSpawn(int32 ColorIdx, bool bPlant, int32 AvoidPlane = -1);
	bool PickSpawnCell(int32 Plane, int32 MinDist, FIntVector& Out, const FIntVector* Near = nullptr);
	bool SpawnPool(EMinResource Res, int32 Plane, const FIntVector* Near = nullptr);
	bool SpawnPlant(EMinResource Res, int32 Plane);
	void TickSpawner(float Dt);
	float PoolSpawnT = 0.f, PlantSpawnT = 0.f, SegmentDropT = 0.f, DispatchT = 0.f;

	// logistics
	bool FindNearest(const FIntVector& From, TFunctionRef<bool(const FIntVector&)> IsGoal,
		TArray<FIntVector>& OutPath) const;
	void Dispatch();
	void TickCarts(float Dt);
	void TickPlants(float Dt);
	void TickPools(float Dt);
	void KillCart(FMinCart& Cart);
	void TriggerGameOver(const FMinPlant& Plant);

	// high scores (per rules mode)
	void LoadHighScore();
	void SaveHighScore();

	int32 NextCartId = 1;
};
