#pragma once

#include "CoreMinimal.h"
#include "MinTypes.generated.h"

// ---------------------------------------------------------------------------
// World constants
//
// The V2 world is one 3D cell lattice. Six 2D planes live inside it:
//   - 3 FRONT planes: full-width vertical slices at depth  y = FrontY[i]
//   - 3 SIDE planes:  perpendicular vertical slices at     x = SideX[i]
// Where a front and a side plane cross they share a real column of cells,
// so routes drawn on either plane connect there. That is the only way to
// move cargo between planes.
// ---------------------------------------------------------------------------
namespace Min
{
	constexpr int32 GX = 36;          // cells west-east (front plane width)
	constexpr int32 GD = 12;          // cells front-back (side plane width)
	constexpr int32 GZ = 20;          // cells of depth (downward)
	constexpr int32 NumFront = 3;
	constexpr int32 NumSide = 3;
	constexpr int32 NP = 6;           // plane index: 0..2 front, 3..5 side
	inline constexpr int32 FrontY[NumFront] = { 2, 6, 10 };
	inline constexpr int32 SideX[NumSide] = { 6, 18, 30 };
	constexpr int32 SkyRows = 2;      // top rows are open sky
	constexpr float CellSize = 250.f; // unreal units per cell (big tunnels)
	constexpr int32 TexPerCell = 20;  // terrain texture pixels per cell

	// layer offsets toward the viewing camera, along the plane normal
	constexpr float DepthBack = -8.f;
	constexpr float DepthTerrain = 0.f;
	constexpr float DepthRoute = 12.f;
	constexpr float DepthMarker = 24.f;
	constexpr float DepthCart = 36.f;
	constexpr float DepthFX = 48.f;

	FORCEINLINE bool IsFront(int32 P) { return P < NumFront; }
	FORCEINLINE int32 PlaneW(int32 P) { return IsFront(P) ? GX : GD; }
	FORCEINLINE int32 PlaneH(int32 P) { return GZ; }

	FORCEINLINE int32 FrontIndexOfY(int32 Y)
	{
		for (int32 I = 0; I < NumFront; ++I) if (FrontY[I] == Y) return I;
		return -1;
	}
	FORCEINLINE int32 SideIndexOfX(int32 X)
	{
		for (int32 I = 0; I < NumSide; ++I) if (SideX[I] == X) return I;
		return -1;
	}

	FORCEINLINE bool InBox(const FIntVector& C)
	{
		return C.X >= 0 && C.X < GX && C.Y >= 0 && C.Y < GD && C.Z >= 0 && C.Z < GZ;
	}
	FORCEINLINE int32 CellIndex(const FIntVector& C) { return (C.Z * GD + C.Y) * GX + C.X; }

	// plane-local (u,v) -> 3D cell
	FORCEINLINE FIntVector PlaneToCell(int32 P, int32 U, int32 V)
	{
		return IsFront(P) ? FIntVector(U, FrontY[P], V) : FIntVector(SideX[P - NumFront], U, V);
	}
	// 3D cell -> plane-local (u,v); false if the cell is not on plane P
	FORCEINLINE bool CellToPlaneUV(const FIntVector& C, int32 P, FIntPoint& OutUV)
	{
		if (!InBox(C)) return false;
		if (IsFront(P))
		{
			if (C.Y != FrontY[P]) return false;
			OutUV = FIntPoint(C.X, C.Z);
			return true;
		}
		if (C.X != SideX[P - NumFront]) return false;
		OutUV = FIntPoint(C.Y, C.Z);
		return true;
	}
	// planes containing this cell (0..2 of them); returns count
	FORCEINLINE int32 PlanesOfCell(const FIntVector& C, int32 Out[2])
	{
		int32 N = 0;
		const int32 F = FrontIndexOfY(C.Y);
		const int32 S = SideIndexOfX(C.X);
		if (F >= 0) Out[N++] = F;
		if (S >= 0) Out[N++] = NumFront + S;
		return N;
	}
	FORCEINLINE bool OnAnyPlane(const FIntVector& C)
	{
		return FrontIndexOfY(C.Y) >= 0 || SideIndexOfX(C.X) >= 0;
	}
	// a "transfer column" cell belongs to both a front and a side plane
	FORCEINLINE bool IsTransferCell(const FIntVector& C)
	{
		return FrontIndexOfY(C.Y) >= 0 && SideIndexOfX(C.X) >= 0;
	}

	// world position of a cell center (ignoring plane layering)
	FORCEINLINE FVector CellCenter(const FIntVector& C)
	{
		return FVector((C.X + 0.5f) * CellSize, (C.Y + 0.5f) * CellSize, -(C.Z + 0.5f) * CellSize);
	}
	// plane surface coordinates in world space
	FORCEINLINE float FrontPlaneY(int32 FrontIdx) { return (FrontY[FrontIdx] + 0.5f) * CellSize; }
	FORCEINLINE float SidePlaneX(int32 SideIdx) { return (SideX[SideIdx] + 0.5f) * CellSize; }
	// front camera watches from +Y looking -Y; side camera from -X looking +X.
	// Depth > 0 moves content toward the camera of that plane.
	FORCEINLINE FVector CellOnPlane(const FIntVector& C, int32 P, float Depth)
	{
		FVector W = CellCenter(C);
		if (IsFront(P)) W.Y = FrontPlaneY(P) + Depth;
		else W.X = SidePlaneX(P - NumFront) - Depth;
		return W;
	}

	// can carts/routes step between two adjacent 3D cells? The step must lie
	// on a plane that contains both cells.
	FORCEINLINE bool CellsLinked(const FIntVector& A, const FIntVector& B)
	{
		const FIntVector D = B - A;
		if (FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z) != 1) return false;
		if (D.X != 0) return FrontIndexOfY(A.Y) >= 0;                       // west-east: front plane
		if (D.Y != 0) return SideIndexOfX(A.X) >= 0;                        // front-back: side plane
		return FrontIndexOfY(A.Y) >= 0 || SideIndexOfX(A.X) >= 0;          // vertical: either
	}

	FString PlaneName(int32 P); // F1..F3 / S1..S3
}

// ---------------------------------------------------------------------------
// Terrain / resources
// ---------------------------------------------------------------------------
UENUM()
enum class EMinTerrain : uint8
{
	Sky, Soil, Sandstone, Limestone, Clay, Obsidian, Granite
};

// resource colors pair pools with their processing plants (Mini Motorways style)
UENUM()
enum class EMinResource : uint8
{
	None, Ruby, Amber, Cobalt, Jade, Amethyst, COUNT
};
constexpr int32 MinMaxColors = 5;

UENUM()
enum class EMinTool : uint8
{
	None, Tunnel, Erase
};

UENUM()
enum class EMinGameRules : uint8
{
	SinglePlane,   // each color's pools and plants share one plane
	CrossPlane     // colors are split across planes; use perpendicular tunnels
};

struct FMinCell
{
	EMinTerrain Terrain = EMinTerrain::Soil;
	bool bExcavated = false;
	int16 PoolId = -1;
	int16 PlantId = -1;
	bool bDepot = false;
};

// a small resource pool: produces carts that serve same-color plants
struct FMinPool
{
	int32 Id = -1;
	EMinResource Res = EMinResource::None;
	FIntVector Cell = FIntVector::ZeroValue;
	int32 CartsFree = 2;
	float SurplusT = 0.f;      // progress toward a bonus load for the depot
	bool bSurplusReady = false;
	bool bAlive = true;
};

// a processing plant: accumulates demand pins that connected pools must clear
struct FMinPlant
{
	int32 Id = -1;
	EMinResource Res = EMinResource::None;
	FIntVector Cell = FIntVector::ZeroValue;
	int32 Pins = 0;
	int32 Inbound = 0;         // carts already on the way
	float DemandT = 0.f;
	float OverflowT = 30.f;    // countdown while pinned to the cap; 0 = run over
	bool bOverflow = false;
	float Age = 0.f;           // grace period before demand starts
	bool bAlive = true;
};

struct FMinCart
{
	int32 Id = -1;
	EMinResource Res = EMinResource::None;
	TArray<FIntVector> Path;   // inclusive cell path
	int32 Leg = 0;             // index of the segment start cell
	float T = 0.f;             // 0..1 along current segment
	bool bReturning = false;
	bool bToDepot = false;
	int32 PoolId = -1;
	int32 PlantId = -1;
	bool bAlive = true;
	FVector WorldPos = FVector::ZeroVector;
	int32 Plane = 0;           // plane index used for rendering
};

// ---------------------------------------------------------------------------
// Resource metadata helpers
// ---------------------------------------------------------------------------
struct FMinResourceInfo
{
	const TCHAR* Name;
	FLinearColor Color;
};
const FMinResourceInfo& MinResInfo(EMinResource R);
