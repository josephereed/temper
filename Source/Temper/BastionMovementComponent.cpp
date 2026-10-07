#include "BastionMovementComponent.h"

UBastionMovementComponent::UBastionMovementComponent()
{
	MaxWalkSpeed = 900.f; // GetMaxSpeed() is authoritative; this only bounds stock code paths
	MaxAcceleration = 2000.f; // input scale only: CalcVelocity uses the curve below
	GravityScale = 1.6f;
	AirControl = 0.12f;
	FallingLateralFriction = 0.2f;
	MaxStepHeight = 60.f;
	SetWalkableFloorAngle(40.f);
	bOrientRotationToMovement = false;
	bUseSeparateBrakingFriction = true;
	BrakingFrictionFactor = 0.f;
	// Never ease the pawn mesh toward the capsule: what you see is where the sim is.
	NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
	Mass = 60000.f;
}

bool UBastionMovementComponent::IsSprinting() const
{
	if (!bWantsSprint || Acceleration.IsNearlyZero())
	{
		return false;
	}
	return (Acceleration.GetSafeNormal2D() | Facing.GetSafeNormal2D()) >= SprintForwardDot;
}

float UBastionMovementComponent::GetMaxSpeed() const
{
	if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking)
	{
		return Super::GetMaxSpeed();
	}
	const float Top = IsSprinting() ? SprintSpeed : WalkSpeed;
	return Top * FMath::Lerp(1.f, LandSpeedFloor, LandPenalty);
}

float UBastionMovementComponent::RegisterLanding(float ImpactSpeed)
{
	const float Severity = FMath::Clamp((ImpactSpeed - LandSoftSpeed) / (LandHardSpeed - LandSoftSpeed), 0.f, 1.f);
	LandPenalty = FMath::Max(LandPenalty, Severity);
	return Severity;
}

void UBastionMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking)
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}
	if (HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	LandPenalty = FMath::Max(0.f, LandPenalty - DeltaTime / LandRecoverTime);

	const float Top = GetMaxSpeed();
	const float InputAmount = FMath::Clamp(Acceleration.Size2D() / FMath::Max(MaxAcceleration, 1.f), 0.f, 1.f);
	FVector V(Velocity.X, Velocity.Y, 0.f);

	if (InputAmount > KINDA_SMALL_NUMBER)
	{
		const FVector Dir = Acceleration.GetSafeNormal2D();
		const float Target = Top * InputAmount;
		float Along = V | Dir;
		FVector Lateral = V - Along * Dir;

		// Grip: kill sideways velocity hard so a heading change bleeds speed instead of sliding.
		const float LatSpeed = Lateral.Size();
		Lateral = LatSpeed > KINDA_SMALL_NUMBER ? Lateral * (FMath::Max(0.f, LatSpeed - LateralGrip * DeltaTime) / LatSpeed) : FVector::ZeroVector;

		if (Along < 0.f)
		{
			// Reversing: brake through zero first.
			Along = FMath::Min(Along + BrakeDecel * DeltaTime, Target);
		}
		else if (Along < Target)
		{
			// Integrate the accel curve in a few sub-steps so the ramp is frame-rate independent.
			const int32 Sub = FMath::Clamp(FMath::CeilToInt(DeltaTime / 0.004f), 1, 16);
			const float H = DeltaTime / Sub;
			for (int32 i = 0; i < Sub && Along < Target; ++i)
			{
				const float A = Along < WalkSpeed
					? FMath::Lerp(AccelFromStop, AccelAtWalk, Along / WalkSpeed)
					: SprintAccel;
				Along = FMath::Min(Along + A * H, Target);
			}
		}
		else
		{
			Along = FMath::Max(Along - BrakeDecel * DeltaTime, Target);
		}
		V = Along * Dir + Lateral;
	}
	else
	{
		const float Speed = V.Size();
		V = Speed > KINDA_SMALL_NUMBER ? V * (FMath::Max(0.f, Speed - BrakeDecel * DeltaTime) / Speed) : FVector::ZeroVector;
	}

	Velocity.X = V.X;
	Velocity.Y = V.Y;
}

void UBastionMovementComponent::PhysFalling(float DeltaTime, int32 Iterations)
{
	if (JetBurnLeft > 0.f)
	{
		const float Burn = FMath::Min(JetBurnLeft, DeltaTime);
		Velocity.Z += JetLift * Burn;
		JetBurnLeft -= DeltaTime;
	}
	Super::PhysFalling(DeltaTime, Iterations);
}
