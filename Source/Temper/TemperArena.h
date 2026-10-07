#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TemperArena.generated.h"

class UStaticMeshComponent;

/**
 * Greybox arena: a small, dense urban block (internal reference: CoD "Rust" layout ideas -
 * open central yard, two-story structures around it, scattered container-sized cover).
 * Built at runtime from engine cubes with simple box collision; no copied art.
 *
 * Scale is for a 4.2 m mech: one story is 5.5 m, containers are waist-to-shoulder cover.
 * The playable area is a 96 m square closed by 14 m perimeter walls plus invisible
 * blockers up to 60 m, far above anything a thruster hop reaches.
 */
UCLASS()
class TEMPER_API ATemperArena : public AActor
{
	GENERATED_BODY()

public:
	ATemperArena();
	virtual void BeginPlay() override;

	/** Half-size of the playable square (cm), inside the perimeter walls. */
	static constexpr float HalfSize = 4800.f;

	/** Player spawn: west street, facing east along a lane kept clear for the movement script. */
	static FTransform PlayerSpawn();

	/** True if a point is within the playable square (with a margin, cm). */
	static bool IsInside(const FVector& P, float Margin = 0.f);

	/** Every solid box, for the "not inside geometry" checks. */
	const TArray<FBox>& GetSolids() const { return Solids; }

private:
	UStaticMeshComponent* AddBox(const FVector& Center, const FVector& Size, float Yaw, const FLinearColor& Tint, bool bVisible = true);
	void Building(const FVector2D& Center, const FVector2D& Size, int32 Stories, float Yaw, const FLinearColor& Tint);
	void Container(const FVector2D& Center, float Yaw, int32 Stack, const FLinearColor& Tint);
	void BuildLighting();

	UPROPERTY(Transient) TObjectPtr<USceneComponent> Root;
	TArray<FBox> Solids;
};
