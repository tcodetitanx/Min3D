#include "MinWorld.h"
#include "Min3D.h"

FString Min::PlaneName(int32 P)
{
	if (P < CurrentPlane) return FString::Printf(TEXT("B%d"), CurrentPlane - P);
	if (P == CurrentPlane) return TEXT("C");
	return FString::Printf(TEXT("F%d"), P - CurrentPlane);
}

const FMinResourceInfo& MinResInfo(EMinResource R)
{
	static const FMinResourceInfo Table[] = {
		{ TEXT("None"),        FLinearColor(0.5f, 0.5f, 0.5f), false },
		{ TEXT("Stone"),       FLinearColor(0.62f, 0.60f, 0.58f), false },
		{ TEXT("Coal"),        FLinearColor(0.16f, 0.15f, 0.16f), false },
		{ TEXT("Iron"),        FLinearColor(0.45f, 0.52f, 0.62f), false },
		{ TEXT("Copper"),      FLinearColor(0.80f, 0.45f, 0.28f), false },
		{ TEXT("Gold"),        FLinearColor(0.95f, 0.75f, 0.20f), false },
		{ TEXT("Crystal"),     FLinearColor(0.70f, 0.45f, 0.85f), false },
		{ TEXT("Water"),       FLinearColor(0.30f, 0.65f, 0.90f), true },
		{ TEXT("Iron Ingot"),  FLinearColor(0.60f, 0.68f, 0.80f), false },
		{ TEXT("Copper Ingot"),FLinearColor(0.90f, 0.55f, 0.35f), false },
		{ TEXT("Gold Bar"),    FLinearColor(1.00f, 0.85f, 0.30f), false },
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

float UMinWorld::ValueNoise(float X, float Y, int32 Seed)
{
	const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
	const float FX = X - X0, FY = Y - Y0;
	const float SX = FX * FX * (3.f - 2.f * FX), SY = FY * FY * (3.f - 2.f * FY);
	const float A = Hash01(X0, Y0, Seed), B = Hash01(X0 + 1, Y0, Seed);
	const float C = Hash01(X0, Y0 + 1, Seed), D = Hash01(X0 + 1, Y0 + 1, Seed);
	return FMath::Lerp(FMath::Lerp(A, B, SX), FMath::Lerp(C, D, SX), SY);
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

// ---------------------------------------------------------------------------
void UMinWorld::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DirtyRects.SetNum(Min::NP);
}

void UMinWorld::MarkDirty(int32 X, int32 Y, int32 P, int32 Radius)
{
	FIntRect R(FIntPoint(FMath::Max(0, X - Radius), FMath::Max(0, Y - Radius)),
		FIntPoint(FMath::Min(Min::NX, X + Radius + 1), FMath::Min(Min::NY, Y + Radius + 1)));
	if (DirtyRects[P].IsEmpty()) DirtyRects[P] = R;
	else DirtyRects[P].Union(R);
}

bool UMinWorld::IsSolid(int32 X, int32 Y, int32 P) const
{
	if (!Min::InGrid(X, Y, P)) return true;
	const FMinCell& C = Cell(X, Y, P);
	return C.Terrain != EMinTerrain::Sky && C.Terrain != EMinTerrain::Cavern
		&& C.Terrain != EMinTerrain::Excavated && !C.bExcavated;
}

bool UMinWorld::IsOpen(int32 X, int32 Y, int32 P) const
{
	return Min::InGrid(X, Y, P) && !IsSolid(X, Y, P);
}

bool UMinWorld::IsDiggable(int32 X, int32 Y, int32 P) const
{
	if (!Min::InGrid(X, Y, P)) return false;
	const FMinCell& C = Cell(X, Y, P);
	if (C.bExcavated || C.Terrain == EMinTerrain::Sky) return false;
	return C.Terrain != EMinTerrain::Granite;
}

bool UMinWorld::Excavate(int32 X, int32 Y, int32 P)
{
	if (!Min::InGrid(X, Y, P)) return false;
	FMinCell& C = Cell(X, Y, P);
	if (C.Terrain == EMinTerrain::Granite) return false;
	if (C.bExcavated || C.Terrain == EMinTerrain::Sky || C.Terrain == EMinTerrain::Cavern) return true;
	C.bExcavated = true;
	ExcavatedList.Add(FIntVector(X, Y, P));
	MarkDirty(X, Y, P);
	return true;
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------
void UMinWorld::Generate(int32 Seed)
{
	WorldSeed = Seed;
	Cells.SetNum(Min::NX * Min::NY * Min::NP);
	Deposits.Reset();
	ExcavatedList.Reset();
	for (int32 P = 0; P < Min::NP; ++P) DirtyRects[P] = FIntRect(0, 0, Min::NX, Min::NY);

	const double T0 = FPlatformTime::Seconds();
	for (int32 P = 0; P < Min::NP; ++P)
	{
		const int32 PS = Seed + P * 7919;
		for (int32 Y = 0; Y < Min::NY; ++Y)
		{
			for (int32 X = 0; X < Min::NX; ++X)
			{
				FMinCell& C = Cell(X, Y, P);
				C = FMinCell();

				// gently rolling surface line
				const float Surf = Min::SkyRows + Fbm(X * 0.03f, P * 0.35f, Seed + 17, 3) * 3.f;
				if (Y < Surf) { C.Terrain = EMinTerrain::Sky; continue; }

				// warped geological bands by depth
				const float Warp = Fbm(X * 0.02f, Y * 0.02f, PS + 31, 3) * 10.f;
				const float D = Y + Warp - Surf;

				if (D < 6.f)        C.Terrain = EMinTerrain::Soil;
				else if (D < 15.f)  C.Terrain = EMinTerrain::Sandstone;
				else if (D < 26.f)  C.Terrain = EMinTerrain::Limestone;
				else if (D < 36.f)  C.Terrain = EMinTerrain::Clay;
				else                C.Terrain = EMinTerrain::Obsidian;

				// granite obstacles - unbreakable blobs, more common deep
				const float G = Fbm(X * 0.08f, Y * 0.08f, PS + 57, 3);
				const float GThresh = 0.78f - (Y / float(Min::NY)) * 0.12f;
				if (D > 8.f && G > GThresh) C.Terrain = EMinTerrain::Granite;

				// caverns
				const float Cav = FMath::Abs(Fbm(X * 0.055f, Y * 0.055f, PS + 71, 4) - 0.5f) * 2.f;
				if (D > 4.f && Cav < 0.10f && C.Terrain != EMinTerrain::Granite)
					C.Terrain = EMinTerrain::Cavern;

				// aquifers - water filled pockets in the limestone band
				const float Aq = Fbm(X * 0.05f + 40.f, Y * 0.05f, PS + 87, 3);
				if (D > 12.f && D < 30.f && Aq > 0.74f && C.Terrain != EMinTerrain::Granite)
					C.Terrain = EMinTerrain::Aquifer;
			}
		}
	}

	FRandomStream R(Seed * 131 + 7);
	PlaceDeposits(R);

	UE_LOG(LogMin, Log, TEXT("World generated: seed=%d deposits=%d in %.1f ms"),
		Seed, Deposits.Num(), (FPlatformTime::Seconds() - T0) * 1000.0);
}

void UMinWorld::CarveDeposit(EMinResource Res, int32 P, int32 CX, int32 CY, int32 Radius,
	int32 Amount, FRandomStream& R, bool bVein)
{
	FMinDeposit Dep;
	Dep.Id = Deposits.Num();
	Dep.Resource = Res;
	Dep.Plane = P;
	Dep.Center = FIntPoint(CX, CY);

	auto TryAdd = [&](int32 X, int32 Y)
	{
		if (!Min::InGrid(X, Y, P)) return;
		FMinCell& C = Cell(X, Y, P);
		if (C.Terrain == EMinTerrain::Sky || C.Terrain == EMinTerrain::Granite
			|| C.Terrain == EMinTerrain::Cavern || C.DepositId >= 0) return;
		if (Res == EMinResource::Water && C.Terrain != EMinTerrain::Aquifer) return;
		C.Resource = Res;
		C.ResourceAmount = 100;
		C.DepositId = Dep.Id;
		Dep.Cells.Add(FIntPoint(X, Y));
	};

	if (bVein)
	{
		// wandering vein
		float VX = CX, VY = CY;
		float Ang = R.FRandRange(0.f, 2.f * PI);
		const int32 Steps = Radius * 4;
		for (int32 S = 0; S < Steps; ++S)
		{
			const int32 IX = FMath::RoundToInt(VX), IY = FMath::RoundToInt(VY);
			TryAdd(IX, IY); TryAdd(IX + 1, IY); TryAdd(IX, IY + 1);
			Ang += R.FRandRange(-0.6f, 0.6f);
			VX += FMath::Cos(Ang); VY += FMath::Sin(Ang) * 0.6f;
		}
	}
	else
	{
		for (int32 Y = CY - Radius; Y <= CY + Radius; ++Y)
			for (int32 X = CX - Radius; X <= CX + Radius; ++X)
			{
				const float DX = X - CX, DY = Y - CY;
				const float N = Hash01(X, Y, Dep.Id * 91 + WorldSeed);
				if (DX * DX + DY * DY <= Radius * Radius * (0.55f + N * 0.5f))
					TryAdd(X, Y);
			}
	}

	if (Dep.Cells.Num() < 3)
	{
		for (const FIntPoint& C : Dep.Cells)
		{
			Cell(C.X, C.Y, P).Resource = EMinResource::None;
			Cell(C.X, C.Y, P).DepositId = -1;
			Cell(C.X, C.Y, P).ResourceAmount = 0;
		}
		return;
	}
	Dep.TotalAmount = Amount;
	Dep.Remaining = Amount;
	// recenter on actual cells
	FIntPoint Sum(0, 0);
	for (const FIntPoint& C : Dep.Cells) Sum += C;
	Dep.Center = FIntPoint(Sum.X / Dep.Cells.Num(), Sum.Y / Dep.Cells.Num());
	Deposits.Add(MoveTemp(Dep));
}

void UMinWorld::PlaceDeposits(FRandomStream& R)
{
	struct FSpec { EMinResource Res; int32 PerPlane; int32 YMin, YMax; int32 Radius; int32 Amount; bool bVein; };
	const FSpec Specs[] = {
		{ EMinResource::Coal,    3, 6, 24, 3, 260, false },
		{ EMinResource::Iron,    3, 8, 30, 3, 300, false },
		{ EMinResource::Copper,  2, 16, 38, 2, 220, true  },
		{ EMinResource::Gold,    1, 30, 46, 2, 160, true  },
		{ EMinResource::Crystal, 1, 34, 46, 2, 140, false },
	};

	for (int32 P = 0; P < Min::NP; ++P)
	{
		for (const FSpec& S : Specs)
		{
			// bias some resources to back/front planes so cross-plane routing matters
			int32 Count = S.PerPlane;
			if (S.Res == EMinResource::Copper && P >= Min::CurrentPlane) Count -= 1;
			if (S.Res == EMinResource::Gold && (P % 3 != 0)) Count = R.FRand() < 0.4f ? 1 : 0;
			if (S.Res == EMinResource::Crystal && (P % 2 == 0)) Count = 0;
			for (int32 I = 0; I < Count; ++I)
			{
				const int32 CX = R.RandRange(4, Min::NX - 5);
				const int32 CY = R.RandRange(S.YMin, FMath::Min(S.YMax, Min::NY - 3));
				CarveDeposit(S.Res, P, CX, CY, S.Radius, S.Amount, R, S.bVein);
			}
		}
		// water deposits on aquifer pockets: find a few aquifer cells
		int32 Found = 0;
		for (int32 Attempt = 0; Attempt < 400 && Found < 3; ++Attempt)
		{
			const int32 X = R.RandRange(2, Min::NX - 3), Y = R.RandRange(10, Min::NY - 4);
			if (Cell(X, Y, P).Terrain == EMinTerrain::Aquifer && Cell(X, Y, P).DepositId < 0)
			{
				CarveDeposit(EMinResource::Water, P, X, Y, 4, 500, R, false);
				++Found;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Visual density for smooth terrain: bilinear over cell solidity with a little noise
// ---------------------------------------------------------------------------
float UMinWorld::VisualDensity(float X, float Y, int32 P) const
{
	// sample solidity of the 4 surrounding cells (cell centers at +0.5)
	const float GX = X - 0.5f, GY = Y - 0.5f;
	const int32 X0 = FMath::FloorToInt(GX), Y0 = FMath::FloorToInt(GY);
	const float FX = GX - X0, FY = GY - Y0;

	auto Solid = [&](int32 CX, int32 CY) -> float
	{
		if (CX < 0) CX = 0; if (CX >= Min::NX) CX = Min::NX - 1;
		if (CY < 0) CY = 0;
		if (CY >= Min::NY) return 1.f;
		return IsSolid(CX, CY, P) ? 1.f : 0.f;
	};

	const float A = Solid(X0, Y0), B = Solid(X0 + 1, Y0);
	const float C = Solid(X0, Y0 + 1), D = Solid(X0 + 1, Y0 + 1);
	float V = FMath::Lerp(FMath::Lerp(A, B, FX), FMath::Lerp(C, D, FX), FY);
	// organic boundary wobble
	V += (ValueNoise(X * 0.9f, Y * 0.9f, WorldSeed + P * 13 + 5) - 0.5f) * 0.16f;
	return V;
}
