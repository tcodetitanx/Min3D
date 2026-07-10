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

// The whole logistics simulation: route graph, structures, carts, power,
// processing, contracts. Owned and ticked by AMinGameMode.
UCLASS()
class UMinSim : public UObject
{
	GENERATED_BODY()

public:
	void Init(UMinWorld* InWorld, UMinAssets* InAssets, AMinGameMode* InGM);
	void Tick(float Dt);

	// ---- graph / build API -------------------------------------------------
	FMinBuildCheck CheckRoute(const TArray<FIntPoint>& Cells, int32 Plane, EMinRoute Type) const;
	bool BuildRoute(const TArray<FIntPoint>& Cells, int32 Plane, EMinRoute Type);
	FMinBuildCheck CheckStructure(EMinStructure Type, const FIntPoint& Cell, int32 Plane) const;
	bool BuildStructure(EMinStructure Type, const FIntPoint& Cell, int32 Plane);
	FMinBuildCheck CheckLift(const FIntPoint& Cell, int32 PlaneA, int32 PlaneB) const;
	bool BuildLift(const FIntPoint& Cell, int32 PlaneA, int32 PlaneB);
	bool RemoveAt(const FIntPoint& Cell, int32 Plane);

	static int32 StructureCost(EMinStructure Type);
	static const TCHAR* StructureName(EMinStructure Type);
	static const TCHAR* StructureSprite(EMinStructure Type);

	// the surface headquarters every supply chain ultimately feeds
	void EnsureBase();
	int32 BaseStructId = -1;
	FIntVector BaseCell() const;

	// ---- state -------------------------------------------------------------
	TArray<FMinNode> Nodes;
	TArray<FMinEdge> Edges;
	TArray<FMinStructureInst> Structures;
	TArray<FMinCart> Carts;
	TMap<FIntVector, int32> NodeAtCell;   // junction/endpoint lookup
	TMap<FIntVector, int32> EdgeAtCell;   // interior route cells -> edge id

	int32 Credits = 350;
	TMap<EMinResource, int32> Stock;
	TArray<FMinContract> Contracts;
	int32 ActiveContract = 0;
	float PlayTime = 0.f;
	int32 TotalDeliveries = 0;
	float SimSpeed = 1.f;
	bool bPaused = false;

	// stats for diagnostics
	int32 CartsInFlight() const;
	float AvgCongestion() const;
	FString CurrentWarning;

	// dirty flag: plane actors rebuild route meshes when set
	bool bTopologyDirty = false;
	bool bStructuresDirty = false;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMinEvent, const FString&);
	FOnMinEvent OnEvent; // toast messages for the HUD

	// save / load
	bool SaveGame(const FString& SlotName = TEXT("min3d_save")) const;
	bool LoadGame(const FString& SlotName = TEXT("min3d_save"));
	static bool SaveExists(const FString& SlotName = TEXT("min3d_save"));

	UMinAudio* Audio = nullptr;

private:
	UPROPERTY() TObjectPtr<UMinWorld> World;
	UPROPERTY() TObjectPtr<UMinAssets> Assets;
	UPROPERTY() TObjectPtr<AMinGameMode> GM;

	int32 EnsureNodeAt(const FIntVector& Cell, EMinRoute SplitType);
	int32 SplitEdgeAt(int32 EdgeId, const FIntVector& Cell);
	void RemoveEdge(int32 EdgeId);
	void RemoveStructure(int32 StructId);
	bool FindPath(int32 StartNode, int32 EndNode, bool bFluid, TArray<int32>& OutEdges, TArray<int32>& OutNodes, float* OutCost = nullptr) const;
	int32 PickDestination(const FMinStructureInst& Source, EMinResource Cargo, TArray<int32>& OutEdges, TArray<int32>& OutNodes);
	bool StructureAcceptsCargo(const FMinStructureInst& S, EMinResource Cargo) const;
	void DeliverCargo(FMinCart& Cart);
	void DispatchFrom(FMinStructureInst& S, EMinResource Cargo, int32 Amount);
	void UpdatePower();
	void TickStructures(float Dt);
	void TickCarts(float Dt);
	void TickContracts();
	void SetupContracts();
	float EdgeSpeedMult(const FMinEdge& E) const;
	int32 NextCartId = 1;
	float PowerTimer = 0.f;
};
