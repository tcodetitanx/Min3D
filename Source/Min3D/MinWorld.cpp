#include "MinWorld.h"
#include "Min3D.h"

FString Min::PlaneName(int32 P)
{
	if (IsFront(P)) return FString::Printf(TEXT("F%d"), P + 1);
	return FString::Printf(TEXT("S%d"), P - NumFront + 1);
}

const FMinResourceInfo& MinResInfo(EMinResource R)
{
	// bright, clean pairing colors - pools and plants of a color match exactly
	static const FMinResourceInfo Table[] = {
		{ TEXT("None"),     FLinearColor(0.5f, 0.5f, 0.5f) },
		{ TEXT("Ruby"),     FLinearColor(0.90f, 0.28f, 0.26f) },
		{ TEXT("Amber"),    FLinearColor(0.96f, 0.68f, 0.18f) },
		{ TEXT("Cobalt"),   FLinearColor(0.26f, 0.52f, 0.92f) },
		{ TEXT("Jade"),     FLinearColor(0.28f, 0.74f, 0.48f) },
		{ TEXT("Amethyst"), FLinearColor(0.64f, 0.42f, 0.86f) },
	};
	const int32 I = FMath::Clamp((int32)R, 0, (int32)EMinResource::COUNT - 1);
	return Table[I];
}

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------
float UMinWorld::Hash01(int32 X, int32 Y, int32 Seed)
{
	uint32 H = (uint32)(X * 374761393 + Y * 668265263 + Seed * 2246822519u);
	H = (H ^ (H >> 13)) * 1274126177u;
	H ^= H >> 16;
	return (H & 0xFFFFFF) / float(0x1000000);
}

float UMinWorld::Hash01_3(int32 X, int32 Y, int32 Z, int32 Seed)
{
	uint32 H = (uint32)(X * 374761393 + Y * 668265263 + Z * 1103515245 + Seed * 2246822519u);
	H = (H ^ (H >> 13)) * 1274126177u;
	H ^= H >> 16;
	return (H & 0xFFFFFF) / float(0x1000000);
}

float UMinWorld::ValueNoise(float X, float Y, int32 Seed)
{
	const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
	const float FX = X - X0, FY = Y - Y0;
	const float SX = FX * FX * (3.f - 2.f * FX), SY = FY * FY * (3.f - 2.f * FY);
	const float A = Hash01(X0, Y0, Seed), B = Hash01(X0 + 1, Y0, Seed);
	const float C = Hash01(X0, Y0 + 1, Seed), D = Hash01(X0 + 1, Y0 + 1, Seed);
	return FMath::Lerp(FMath::Lerp(A, B, SX), FMath::Lerp(C, D, SX), SY);
}

float UMinWorld::ValueNoise3(float X, float Y, float Z, int32 Seed)
{
	const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y), Z0 = FMath::FloorToInt(Z);
	const float FX = X - X0, FY = Y - Y0, FZ = Z - Z0;
	const float SX = FX * FX * (3.f - 2.f * FX);
	const float SY = FY * FY * (3.f - 2.f * FY);
	const float SZ = FZ * FZ * (3.f - 2.f * FZ);
	auto Lerp2 = [&](int32 DZ)
	{
		const float A = Hash01_3(X0, Y0, Z0 + DZ, Seed), B = Hash01_3(X0 + 1, Y0, Z0 + DZ, Seed);
		const float C = Hash01_3(X0, Y0 + 1, Z0 + DZ, Seed), D = Hash01_3(X0 + 1, Y0 + 1, Z0 + DZ, Seed);
		return FMath::Lerp(FMath::Lerp(A, B, SX), FMath::Lerp(C, D, SX), SY);
	};
	return FMath::Lerp(Lerp2(0), Lerp2(1), SZ);
}

float UMinWorld::Fbm(float X, float Y, int32 Seed, int32 Octaves, float Lac, float Gain)
{
	float Sum = 0.f, Amp = 1.f, Freq = 1.f, Norm = 0.f;
	for (int32 I = 0; I < Octaves; ++I)
	{
		Sum += Amp * ValueNoise(X * Freq, Y * Freq, Seed + I * 101);
		Norm += Amp;
		Amp *= Gain; Freq *= Lac;
	}
	return Sum / Norm;
}

float UMinWorld::Fbm3(float X, float Y, float Z, int32 Seed, int32 Octaves, float Lac, float Gain)
{
	float Sum = 0.f, Amp = 1.f, Freq = 1.f, Norm = 0.f;
	for (int32 I = 0; I < Octaves; ++I)
	{
		Sum += Amp * ValueNoise3(X * Freq, Y * Freq, Z * Freq, Seed + I * 101);
		Norm += Amp;
		Amp *= Gain; Freq *= Lac;
	}
	return Sum / Norm;
}

// ---------------------------------------------------------------------------
void UMinWorld::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DirtyRects.SetNum(Min::NP);
}

void UMinWorld::MarkDirtyCell(const FIntVector& C, int32 Radius)
{
	int32 Planes[2];
	const int32 N = Min::PlanesOfCell(C, Planes);
	for (int32 I = 0; I < N; ++I)
	{
		const int32 P = Planes[I];
		FIntPoint UV;
		if (!Min::CellToPlaneUV(C, P, UV)) continue;
		FIntRect R(FIntPoint(FMath::Max(0, UV.X - Radius), FMath::Max(0, UV.Y - Radius)),
			FIntPoint(FMath::Min(Min::PlaneW(P), UV.X + Radius + 1), FMath::Min(Min::PlaneH(P), UV.Y + Radius + 1)));
		if (DirtyRects[P].IsEmpty()) DirtyRects[P] = R;
		else DirtyRects[P].Union(R);
	}
}

bool UMinWorld::IsSolid(const FIntVector& C) const
{
	if (!Min::InBox(C)) return true;
	const FMinCell& K = Cell(C);
	return K.Terrain != EMinTerrain::Sky && !K.bExcavated;
}

bool UMinWorld::IsOpen(const FIntVector& C) const
{
	return Min::InBox(C) && !IsSolid(C);
}

bool UMinWorld::IsDiggable(const FIntVector& C) const
{
	if (!Min::InBox(C)) return false;
	const FMinCell& K = Cell(C);
	if (K.bExcavated || K.Terrain == EMinTerrain::Sky) return false;
	return K.Terrain != EMinTerrain::Granite;
}

int32 UMinWorld::SurfaceV(int32 P, int32 U) const
{
	for (int32 V = 0; V < Min::PlaneH(P); ++V)
		if (PlaneCell(P, U, V).Terrain != EMinTerrain::Sky) return V;
	return Min::PlaneH(P);
}

bool UMinWorld::Excavate(const FIntVector& C)
{
	if (!Min::InBox(C)) return false;
	FMinCell& K = Cell(C);
	if (K.Terrain == EMinTerrain::Granite) return false;
	if (K.bExcavated || K.Terrain == EMinTerrain::Sky) return true;
	K.bExcavated = true;
	MarkDirtyCell(C);
	return true;
}

// ---------------------------------------------------------------------------
// Generation - 3D noise keeps intersecting planes consistent where they share
// a column of cells.
// ---------------------------------------------------------------------------
void UMinWorld::Generate(int32 Seed)
{
	WorldSeed = Seed;
	Cells.SetNum(Min::GX * Min::GD * Min::GZ);
	for (int32 P = 0; P < Min::NP; ++P)
		DirtyRects[P] = FIntRect(0, 0, Min::PlaneW(P), Min::PlaneH(P));

	const double T0 = FPlatformTime::Seconds();
	for (int32 Z = 0; Z < Min::GZ; ++Z)
	for (int32 Y = 0; Y < Min::GD; ++Y)
	for (int32 X = 0; X < Min::GX; ++X)
	{
		FMinCell& C = Cell(FIntVector(X, Y, Z));
		C = FMinCell();

		// gently rolling surface line, continuous in both horizontal axes
		const float Surf = Min::SkyRows + Fbm(X * 0.07f, Y * 0.13f, Seed + 17, 3) * 1.8f;
		if (Z < Surf) { C.Terrain = EMinTerrain::Sky; continue; }

		// warped geological bands by depth
		const float Warp = Fbm3(X * 0.05f, Y * 0.09f, Z * 0.05f, Seed + 31, 3) * 4.5f;
		const float D = Z + Warp - Surf;

		if (D < 3.f)        C.Terrain = EMinTerrain::Soil;
		else if (D < 7.f)   C.Terrain = EMinTerrain::Sandstone;
		else if (D < 11.f)  C.Terrain = EMinTerrain::Limestone;
		else if (D < 15.f)  C.Terrain = EMinTerrain::Clay;
		else                C.Terrain = EMinTerrain::Obsidian;

		// granite obstacles - unbreakable blobs, more common deep
		const float G = Fbm3(X * 0.18f, Y * 0.30f, Z * 0.18f, Seed + 57, 3);
		const float GThresh = 0.80f - (Z / float(Min::GZ)) * 0.10f;
		if (D > 3.f && G > GThresh) C.Terrain = EMinTerrain::Granite;
	}

	UE_LOG(LogMin, Log, TEXT("World generated: seed=%d (%dx%dx%d) in %.1f ms"),
		Seed, Min::GX, Min::GD, Min::GZ, (FPlatformTime::Seconds() - T0) * 1000.0);
}

// ---------------------------------------------------------------------------
// Visual density: bilinear over cell solidity with organic wobble. Sampled in
// plane-local coordinates; the renderer turns this into a crisp SDF edge.
// ---------------------------------------------------------------------------
float UMinWorld::VisualDensity(int32 P, float U, float V) const
{
	const float GU = U - 0.5f, GV = V - 0.5f;
	const int32 U0 = FMath::FloorToInt(GU), V0 = FMath::FloorToInt(GV);
	const float FU = GU - U0, FV = GV - V0;

	auto Solid = [&](int32 CU, int32 CV) -> float
	{
		if (CU < 0) CU = 0; if (CU >= Min::PlaneW(P)) CU = Min::PlaneW(P) - 1;
		if (CV < 0) CV = 0;
		if (CV >= Min::PlaneH(P)) return 1.f;
		return IsSolid(Min::PlaneToCell(P, CU, CV)) ? 1.f : 0.f;
	};

	const float A = Solid(U0, V0), B = Solid(U0 + 1, V0);
	const float C = Solid(U0, V0 + 1), D = Solid(U0 + 1, V0 + 1);
	float Out = FMath::Lerp(FMath::Lerp(A, B, FU), FMath::Lerp(C, D, FU), FV);
	// organic boundary wobble
	Out += (ValueNoise(U * 1.1f, V * 1.1f, WorldSeed + P * 13 + 5) - 0.5f) * 0.14f;
	return Out;
}
