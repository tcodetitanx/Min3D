#pragma once

#include "CoreMinimal.h"
#include "MinTypes.generated.h"

// ---------------------------------------------------------------------------
// World constants
// ---------------------------------------------------------------------------
namespace Min
{
	constexpr int32 NX = 96;          // cells horizontally (x)
	constexpr int32 NY = 48;          // cells of depth (y, downward)
	constexpr int32 NP = 10;          // parallel planes (z): B4..B1, C, F1..F5
	constexpr int32 CurrentPlane = 4; // index of the starting "CURRENT" plane
	constexpr int32 SkyRows = 3;      // top rows are open sky
	constexpr float CellSize = 100.f; // unreal units per cell
	constexpr float PlaneGap = 800.f; // world Y between planes
	constexpr int32 TexPerCell = 12;  // terrain texture pixels per cell

	// depth offsets (towards camera = -Y) for layering within one plane
	constexpr float DepthTerrain = 0.f;
	constexpr float DepthDeposit = -8.f;
	constexpr float DepthRoute = -16.f;
	constexpr float DepthStructure = -28.f;
	constexpr float DepthCart = -40.f;
	constexpr float DepthFX = -52.f;

	inline FVector CellToWorld(int32 X, int32 Y, int32 P, float DepthBias = 0.f)
	{
		return FVector(X * CellSize + CellSize * 0.5f, P * PlaneGap + DepthBias,
			-(Y * CellSize + CellSize * 0.5f));
	}
	inline FIntPoint WorldToCell(const FVector& W)
	{
		return FIntPoint(FMath::FloorToInt(W.X / CellSize), FMath::FloorToInt(-W.Z / CellSize));
	}
	FORCEINLINE int32 CellIndex(int32 X, int32 Y, int32 P) { return (P * NY + Y) * NX + X; }
	FORCEINLINE bool InGrid(int32 X, int32 Y, int32 P)
	{
		return X >= 0 && X < NX && Y >= 0 && Y < NY && P >= 0 && P < NP;
	}
	FString PlaneName(int32 P); // B4..B1, C, F1..F5
}

// ---------------------------------------------------------------------------
// Terrain / resources
// ---------------------------------------------------------------------------
UENUM()
enum class EMinTerrain : uint8
{
	Sky, Soil, Sandstone, Limestone, Clay, Granite, Obsidian, Aquifer, Cavern, Excavated
};

UENUM()
enum class EMinResource : uint8
{
	None, Stone, Coal, Iron, Copper, Gold, Crystal, Water,
	IronIngot, CopperIngot, GoldBar, COUNT
};

UENUM()
enum class EMinStructure : uint8
{
	None, Miner, Pump, Smelter, Depot, Generator, Lift
};

UENUM()
enum class EMinRoute : uint8
{
	None, Tunnel, Pipe, LiftShaft
};

UENUM()
enum class EMinTool : uint8
{
	None, Tunnel, Pipe, Miner, Pump, Smelter, Depot, Generator, Lift, Remove
};

struct FMinCell
{
	EMinTerrain Terrain = EMinTerrain::Soil;
	EMinResource Resource = EMinResource::None;
	uint16 ResourceAmount = 0;
	int16 DepositId = -1;
	int16 StructureId = -1;
	uint8 bRoute = 0;        // route occupancy bitmask of EMinRoute (1<<type)
	bool bExcavated = false;
};

struct FMinDeposit
{
	int32 Id = -1;
	EMinResource Resource = EMinResource::None;
	int32 Plane = 0;
	FIntPoint Center = FIntPoint::ZeroValue;
	TArray<FIntPoint> Cells;
	int32 TotalAmount = 0;
	int32 Remaining = 0;
};

// ---------------------------------------------------------------------------
// Route graph
// ---------------------------------------------------------------------------
struct FMinNode
{
	int32 Id = -1;
	FIntVector Cell = FIntVector::ZeroValue; // x, y, plane
	TArray<int32> Edges;
	int32 StructureId = -1;
};

struct FMinEdge
{
	int32 Id = -1;
	int32 A = -1, B = -1;            // node ids
	EMinRoute Type = EMinRoute::Tunnel;
	TArray<FIntVector> Path;         // inclusive cell path from A to B (same plane), or 2 cells for lifts
	float Length = 1.f;              // cells
	float Capacity = 3.f;            // max carts before slowdown
	int32 Occupancy = 0;             // carts currently on the edge
	bool bAlive = true;
	float CongestionSmoothed = 0.f;
};

struct FMinStructureInst
{
	int32 Id = -1;
	EMinStructure Type = EMinStructure::None;
	FIntVector Cell = FIntVector::ZeroValue;
	int32 TargetPlane = -1;          // lifts: other endpoint plane
	int32 NodeId = -1;               // graph node at our port
	int32 NodeIdB = -1;              // lifts: node on target plane
	int32 DepositId = -1;            // miners/pumps
	float Progress = 0.f;            // extraction/processing accumulation
	int32 Buffer = 0;                // output units waiting for a cart
	int32 InputBuffer = 0;           // smelter input, generator fuel
	EMinResource BufferRes = EMinResource::None;
	EMinResource InputRes = EMinResource::None;
	bool bPowered = false;
	float FuelSeconds = 0.f;         // generator remaining burn time
	float RetryTimer = 0.f;          // dispatch retry throttle
	bool bAlive = true;
};

struct FMinCart
{
	int32 Id = -1;
	EMinResource Cargo = EMinResource::None;
	int32 Amount = 0;
	TArray<int32> EdgePath;          // edge ids to traverse
	TArray<int32> NodePath;          // node ids, NodePath.Num() == EdgePath.Num()+1
	int32 Leg = 0;                   // current edge index in EdgePath
	float T = 0.f;                   // 0..1 along current edge
	int32 DestStructure = -1;
	bool bAlive = true;
	FVector WorldPos = FVector::ZeroVector;
	int32 Plane = 0;
	bool bFluid = false;
};

struct FMinContract
{
	FString Title;
	EMinResource Resource = EMinResource::None;
	int32 Needed = 0;
	int32 Delivered = 0;
	int32 RewardCredits = 0;
	FString Hint;
	bool bComplete = false;
};

// ---------------------------------------------------------------------------
// Resource metadata helpers
// ---------------------------------------------------------------------------
struct FMinResourceInfo
{
	const TCHAR* Name;
	FLinearColor Color;
	bool bFluid;
};
const FMinResourceInfo& MinResInfo(EMinResource R);
