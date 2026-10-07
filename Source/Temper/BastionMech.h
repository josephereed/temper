#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BastionMech.generated.h"

class UBastionMovementComponent;
class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

UENUM()
enum class ETemperCameraMode : uint8
{
	FirstPerson,
	ThirdPerson,
	Observe,
};

/** A movement event the scenario runner (and the log) can see. */
struct FBastionEvent
{
	FString Name;
	float Value = 0.f;
	uint64 Frame = 0;
};

/**
 * Bastion: heavy biped, ~4.3 m. Greybox body (boxes) on a procedural rig:
 *  - legs are two-bone IK chains to feet that stay planted on the ground until they step,
 *    so the feet never slide (the visual half of "no ice-skating");
 *  - the hull (legs/pelvis) turns toward the aim at a limited rate; the torso and cockpit
 *    follow the mouse 1:1, so aim is instant while the machine underneath lags;
 *  - pelvis-drop and torso-lean springs take kicks from footfalls, landings, thruster
 *    ignition and input changes. The input kick is what makes a keypress visible on the
 *    very next frame, long before the mass has built real speed.
 */
UCLASS()
class TEMPER_API ABastionMech : public ACharacter
{
	GENERATED_BODY()

public:
	ABastionMech(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Landed(const FHitResult& Hit) override;

	// Driven by the player controller.
	void SetMoveInput(const FVector2D& Input);
	void SetSprint(bool bOn);
	void Jet();
	void SetCameraMode(ETemperCameraMode Mode);
	/** Re-plant feet under the hull and zero the springs (after a teleport). */
	void ResetRig();

	UBastionMovementComponent* Move() const;
	float GetHullYaw() const { return GetActorRotation().Yaw; }
	FVector GetGroundLocation() const;
	/** Torso/cockpit world transform: what the player sees move on a keypress. */
	FTransform GetTorsoTransform() const;
	float GetJetCooldownLeft() const { return JetCooldownLeft; }

	TArray<FBastionEvent>& Events() { return EventLog; }

	// ---- Tuning (final values are listed in the done report) ----
	/** Hull turn: max rate (deg/s) and how fast it gets there (deg/s^2). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float HullTurnRate = 170.f;
	UPROPERTY(EditAnywhere, Category = "Bastion") float HullTurnAccel = 700.f;
	/** Thruster hop: vertical launch speed, forward boost, horizontal cap (cm/s), burn and cooldown (s). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float JetUpSpeed = 620.f;
	UPROPERTY(EditAnywhere, Category = "Bastion") float JetForwardBoost = 450.f;
	UPROPERTY(EditAnywhere, Category = "Bastion") float JetMaxHorizontal = 1100.f;
	UPROPERTY(EditAnywhere, Category = "Bastion") float JetBurnTime = 0.3f;
	UPROPERTY(EditAnywhere, Category = "Bastion") float JetCooldown = 1.6f;
	/** Jet presses up to this long before touchdown fire on landing (s). */
	UPROPERTY(EditAnywhere, Category = "Bastion") float JetBufferTime = 0.15f;

private:
	struct FFoot
	{
		float Side = 1.f;
		FVector Plant = FVector::ZeroVector;
		FVector From = FVector::ZeroVector;
		float T = 1.f;
		float Duration = 0.3f;
		bool bSwing = false;
		FVector Pos = FVector::ZeroVector;
	};

	struct FSpring
	{
		float X = 0.f;
		float V = 0.f;
		void Step(float Target, float Omega, float Zeta, float Dt);
	};

	void BuildBody();
	UStaticMeshComponent* AddPart(USceneComponent* Parent, const FVector& Loc, const FVector& Size, const FLinearColor& Color, bool bCastShadow = true);
	void UpdateHull(float Dt);
	void UpdateLegs(float Dt);
	void UpdateSprings(float Dt);
	void UpdateCameras(float Dt);
	FVector FootIdeal(const FFoot& Foot, float Lead) const;
	float GroundZAt(const FVector& P, float Fallback) const;
	void Footfall(float Strength);
	void PlaceSegment(UStaticMeshComponent* Seg, const FVector& A, const FVector& B, float Width, const FVector& SideAxis);
	void AddEvent(const FString& Name, float Value);
	void AddTrauma(float Amount) { Trauma = FMath::Min(1.f, Trauma + Amount); }
	void DoJet();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Visual;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Pelvis;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> TorsoPivot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> ChaseArm;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> ChaseCamera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> CockpitCamera;

	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> UpperBody; // hidden in first person
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> CockpitFrame; // first person only
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> LegParts;     // thigh, shin, foot x2
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Thrusters;

	FFoot Feet[2];
	FSpring Drop;      // pelvis drop (cm, down positive)
	FSpring LeanFwd;   // torso pitch (deg, nose-down positive)
	FSpring LeanSide;  // torso roll (deg)
	FVector PrevVelocity = FVector::ZeroVector;
	FVector2D MoveInput = FVector2D::ZeroVector;
	FVector2D PrevMoveInput = FVector2D::ZeroVector;
	float HullTurnSpeed = 0.f;
	float StillTime = 0.f;
	float Trauma = 0.f;
	float ShakeTime = 0.f;
	float JetCooldownLeft = 0.f;
	float JetBufferLeft = 0.f;
	float ThrusterGlow = 0.f;
	bool bWasFalling = false;
	ETemperCameraMode CameraMode = ETemperCameraMode::ThirdPerson;
	TArray<FBastionEvent> EventLog;
};
