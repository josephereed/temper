#include "TemperArena.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Temper.h"

namespace
{
	const FName ColorParam(TEXT("Color"));
	constexpr float Story = 550.f;

	UStaticMesh* CubeMesh()
	{
		static UStaticMesh* Mesh = TemperLoadPinned<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube"));
		return Mesh;
	}

	UMaterialInterface* TintMaterial()
	{
		static UMaterialInterface* Mat = TemperLoadPinned<UMaterialInterface>(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		return Mat;
	}

	/** The classic world-aligned grid: makes speed and stopping distance readable on every surface. */
	UMaterialInterface* GridMaterial()
	{
		static UMaterialInterface* Mat = TemperLoadPinned<UMaterialInterface>(TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
		return Mat;
	}

	FLinearColor Hex(const TCHAR* H) { return FLinearColor::FromSRGBColor(FColor::FromHex(H)); }
}

ATemperArena::ATemperArena()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

FTransform ATemperArena::PlayerSpawn()
{
	return FTransform(FRotator::ZeroRotator, FVector(-4000.f, 0.f, 240.f));
}

bool ATemperArena::IsInside(const FVector& P, float Margin)
{
	return FMath::Abs(P.X) <= HalfSize - Margin && FMath::Abs(P.Y) <= HalfSize - Margin && P.Z > -200.f;
}

UStaticMeshComponent* ATemperArena::AddBox(const FVector& Center, const FVector& Size, float Yaw, const FLinearColor& Tint, bool bVisible)
{
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
	Mesh->SetStaticMesh(CubeMesh());
	Mesh->SetupAttachment(Root);
	Mesh->SetRelativeLocation(Center);
	Mesh->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
	Mesh->SetRelativeScale3D(Size / 100.f);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetMobility(EComponentMobility::Static);
	if (bVisible)
	{
		if (Tint.A > 0.f)
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(TintMaterial(), Mesh);
			Mat->SetVectorParameterValue(ColorParam, Tint);
			Mesh->SetMaterial(0, Mat);
		}
		else
		{
			Mesh->SetMaterial(0, GridMaterial());
		}
	}
	else
	{
		Mesh->SetHiddenInGame(true);
		Mesh->SetCastShadow(false);
	}
	Mesh->RegisterComponent();

	const FVector Half = Size * 0.5f;
	const FTransform T(FRotator(0.f, Yaw, 0.f), Center);
	Solids.Add(FBox(-Half, Half).TransformBy(T));
	return Mesh;
}

void ATemperArena::Building(const FVector2D& Center, const FVector2D& Size, int32 Stories, float Yaw, const FLinearColor& Tint)
{
	const float H = Stories * Story;
	AddBox(FVector(Center, H * 0.5f), FVector(Size, H), Yaw, Tint);
	// Parapet lip and a floor-line band so the two stories read at mech scale.
	AddBox(FVector(Center, H + 60.f), FVector(Size.X + 40.f, Size.Y + 40.f, 120.f), Yaw, Tint * 0.8f);
	for (int32 S = 1; S < Stories; ++S)
	{
		AddBox(FVector(Center, S * Story), FVector(Size.X + 30.f, Size.Y + 30.f, 40.f), Yaw, Tint * 0.7f);
	}
}

void ATemperArena::Container(const FVector2D& Center, float Yaw, int32 Stack, const FLinearColor& Tint)
{
	// 12 x 2.5 x 2.6 m, i.e. hip-to-chest cover for a 4.2 m mech; stacked = full cover.
	for (int32 i = 0; i < Stack; ++i)
	{
		AddBox(FVector(Center, 130.f + i * 262.f), FVector(1200.f, 250.f, 260.f), Yaw + (i % 2) * 4.f, i % 2 ? Tint * 0.75f : Tint);
	}
}

void ATemperArena::BeginPlay()
{
	Super::BeginPlay();

	const FLinearColor Grid(0.f, 0.f, 0.f, 0.f); // alpha 0 = world grid material
	const FLinearColor Concrete = Hex(TEXT("8E8B84"));
	const FLinearColor Brick = Hex(TEXT("8A5A44"));
	const FLinearColor Steel = Hex(TEXT("5C6670"));
	const FLinearColor Rust = Hex(TEXT("9A4F2A"));
	const FLinearColor Teal = Hex(TEXT("3E6E6A"));
	const FLinearColor Ochre = Hex(TEXT("B08A3A"));

	// Ground slab (top at Z = 0) and the outer apron beyond the walls.
	AddBox(FVector(0.f, 0.f, -100.f), FVector(HalfSize * 2.f + 4000.f, HalfSize * 2.f + 4000.f, 200.f), 0.f, Grid);

	// Painted 4 m ground grid: the engine grid washes out at mech scale, and speed, stopping
	// distance and sliding are only readable against fixed marks on the floor.
	{
		UInstancedStaticMeshComponent* Lines = NewObject<UInstancedStaticMeshComponent>(this);
		Lines->SetStaticMesh(CubeMesh());
		Lines->SetupAttachment(Root);
		Lines->SetMobility(EComponentMobility::Static);
		Lines->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Lines->SetCastShadow(false);
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(TintMaterial(), Lines);
		Mat->SetVectorParameterValue(ColorParam, Hex(TEXT("4A4A48")));
		Lines->SetMaterial(0, Mat);
		Lines->RegisterComponent();
		for (float C = -HalfSize; C <= HalfSize + 1.f; C += 400.f)
		{
			const float W = FMath::IsNearlyZero(FMath::Fmod(C, 2000.f)) ? 0.24f : 0.12f; // every 20 m a heavier line
			Lines->AddInstance(FTransform(FRotator::ZeroRotator, FVector(C, 0.f, 0.5f), FVector(W, HalfSize * 2.f / 100.f, 0.01f)));
			Lines->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0.f, C, 0.5f), FVector(HalfSize * 2.f / 100.f, W, 0.01f)));
		}
	}

	// Perimeter: 14 m walls, and invisible blockers to 60 m so nothing can hop out.
	const float WallT = 200.f;
	const float WallH = 1400.f;
	const float Span = HalfSize * 2.f + WallT * 2.f;
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const bool bX = Side < 2;
		const float Sign = (Side % 2) ? 1.f : -1.f;
		const float Off = HalfSize + WallT * 0.5f;
		const FVector C = bX ? FVector(Sign * Off, 0.f, 0.f) : FVector(0.f, Sign * Off, 0.f);
		const FVector2D S = bX ? FVector2D(WallT, Span) : FVector2D(Span, WallT);
		AddBox(C + FVector(0.f, 0.f, WallH * 0.5f), FVector(S, WallH), 0.f, Concrete * 0.9f);
		AddBox(C + FVector(0.f, 0.f, 3000.f + 200.f), FVector(S, 5600.f), 0.f, Concrete, false);
	}
	// Lid: catches anything that somehow gets above the blockers.
	AddBox(FVector(0.f, 0.f, 6200.f), FVector(Span, Span, 200.f), 0.f, Concrete, false);

	// Ring of two-story structures, alleys between them. North side.
	Building(FVector2D(-3000.f, 3900.f), FVector2D(2600.f, 1400.f), 2, 0.f, Brick);
	Building(FVector2D(300.f, 4000.f), FVector2D(2800.f, 1200.f), 2, 0.f, Concrete);
	Building(FVector2D(3500.f, 3700.f), FVector2D(1800.f, 1800.f), 2, 0.f, Steel);
	// South side.
	Building(FVector2D(-3200.f, -3800.f), FVector2D(2200.f, 1600.f), 2, 0.f, Concrete);
	Building(FVector2D(-200.f, -4000.f), FVector2D(2400.f, 1200.f), 1, 0.f, Teal);
	Building(FVector2D(3300.f, -3600.f), FVector2D(2400.f, 2000.f), 2, 0.f, Brick);
	// East side (opposite the spawn street).
	Building(FVector2D(4000.f, 600.f), FVector2D(1200.f, 2400.f), 2, 0.f, Concrete);
	// West side: two blocks flanking the spawn street (lane along Y = 0 stays open).
	Building(FVector2D(-4100.f, 1900.f), FVector2D(1000.f, 1600.f), 2, 0.f, Steel);
	Building(FVector2D(-4100.f, -1900.f), FVector2D(1000.f, 1600.f), 2, 0.f, Brick);

	// Central yard: a raised loading dock with a ramp (landing tests, step-up), and the tower base.
	AddBox(FVector(1400.f, -900.f, 150.f), FVector(1600.f, 1000.f, 300.f), 0.f, Concrete);
	AddBox(FVector(300.f, -900.f, 75.f), FVector(800.f, 900.f, 150.f), 0.f, Concrete * 0.85f); // ramp step
	AddBox(FVector(1800.f, 1200.f, 700.f), FVector(500.f, 500.f, 1400.f), 0.f, Rust);          // tower column
	AddBox(FVector(1800.f, 1200.f, 1450.f), FVector(1100.f, 1100.f, 100.f), 0.f, Rust * 0.8f); // tower deck

	// Scattered cover: containers (single and stacked), low walls, barrier blocks.
	Container(FVector2D(-600.f, 1500.f), 15.f, 1, Rust);
	Container(FVector2D(600.f, 2400.f), 90.f, 2, Teal);
	Container(FVector2D(-1800.f, -1600.f), -20.f, 1, Ochre);
	Container(FVector2D(2600.f, -2400.f), 0.f, 2, Rust);
	Container(FVector2D(-2400.f, 2400.f), 70.f, 1, Steel);
	Container(FVector2D(3000.f, 2600.f), -35.f, 1, Ochre);
	AddBox(FVector(-200.f, -2200.f, 110.f), FVector(1600.f, 80.f, 220.f), 0.f, Concrete);   // low wall
	AddBox(FVector(2900.f, -400.f, 110.f), FVector(80.f, 1400.f, 220.f), 0.f, Concrete);    // low wall
	AddBox(FVector(-1000.f, 2700.f, 90.f), FVector(300.f, 300.f, 180.f), 30.f, Concrete);   // barriers
	AddBox(FVector(-3000.f, -2400.f, 90.f), FVector(300.f, 300.f, 180.f), 10.f, Concrete);
	AddBox(FVector(800.f, 900.f, 90.f), FVector(300.f, 300.f, 180.f), 45.f, Concrete);

	BuildLighting();
}

void ATemperArena::BuildLighting()
{
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this);
	Sun->SetupAttachment(Root);
	Sun->SetRelativeRotation(FRotator(-42.f, 55.f, 0.f));
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetIntensity(7.f);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetDynamicShadowDistanceMovableLight(15000.f);
	Sun->RegisterComponent();

	USkyAtmosphereComponent* Atmosphere = NewObject<USkyAtmosphereComponent>(this);
	Atmosphere->SetupAttachment(Root);
	Atmosphere->RegisterComponent();

	USkyLightComponent* Sky = NewObject<USkyLightComponent>(this);
	Sky->SetupAttachment(Root);
	Sky->SetMobility(EComponentMobility::Movable);
	Sky->bRealTimeCapture = true;
	Sky->RegisterComponent();

	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(this);
	Fog->SetupAttachment(Root);
	Fog->SetFogDensity(0.006f);
	Fog->RegisterComponent();
}
