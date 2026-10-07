#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BastionMovementComponent.generated.h"

/**
 * Heavy-biped ground movement. Replaces CMC's walking velocity integration with:
 *  - a speed-dependent acceleration curve: strongest from a standstill, tapering as the
 *    mech nears top speed, so the first step answers the stick immediately but full speed
 *    takes ~1.5 s ("responsive controls, heavy machine");
 *  - finite braking: the mech coasts to a stop over a couple of metres instead of halting;
 *  - lateral grip: velocity across the input direction is bled off hard, so changing
 *    direction costs speed instead of sliding sideways (no ice-skating);
 *  - a landing penalty that briefly cuts top speed after a hard touchdown.
 * Air movement and collision are stock CMC.
 */
UCLASS()
class TEMPER_API UBastionMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UBastionMovementComponent();

	virtual float GetMaxSpeed() const override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void PhysFalling(float DeltaTime, int32 Iterations) override;

	/** Top walking speed (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float WalkSpeed = 650.f;
	/** Top sprint speed (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float SprintSpeed = 900.f;
	/** Acceleration at a standstill (cm/s^2). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float AccelFromStop = 900.f;
	/** Acceleration as speed reaches WalkSpeed (cm/s^2). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float AccelAtWalk = 180.f;
	/** Acceleration from walk to sprint speed (cm/s^2). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float SprintAccel = 240.f;
	/** Deceleration with no input, or against the input (cm/s^2). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float BrakeDecel = 1150.f;
	/** How fast velocity across the input direction is removed (cm/s^2). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float LateralGrip = 1700.f;
	/** Fraction of input that sprinting needs along the facing direction. */
	UPROPERTY(EditAnywhere, Category = "Bastion") float SprintForwardDot = 0.6f;

	/** Landing: impact speed (cm/s, downward) that starts to count as hard, and that is maximal. */
	UPROPERTY(EditAnywhere, Category = "Bastion") float LandSoftSpeed = 350.f;
	UPROPERTY(EditAnywhere, Category = "Bastion") float LandHardSpeed = 1400.f;
	/** Top-speed multiplier right after a maximal landing, and how long it takes to recover (s). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float LandSpeedFloor = 0.35f;
	UPROPERTY(EditAnywhere, Category = "Bastion") float LandRecoverTime = 0.55f;

	/** Thruster: extra upward acceleration while the burn lasts (cm/s^2), and burn time (s). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float JetLift = 900.f;
	float JetBurnLeft = 0.f;

	bool bWantsSprint = false;
	/** Hull facing used for the sprint check (set by the pawn). */
	FVector Facing = FVector::ForwardVector;

	/** 0..1 severity of a landing, from downward impact speed. Starts the recovery penalty. */
	float RegisterLanding(float ImpactSpeed);
	bool IsSprinting() const;

private:
	float LandPenalty = 0.f; // 0..1, decays over LandRecoverTime
};
