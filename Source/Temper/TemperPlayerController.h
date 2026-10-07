#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BastionMech.h"
#include "TemperPlayerController.generated.h"

class ACameraActor;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * Input for the Bastion and the three camera modes (V / gamepad Back cycles
 * first person -> third person -> observe). In observe the mech idles and the same
 * move/look input flies a free spectator camera (Space/E up, Ctrl/Q down, Shift fast).
 *
 * Every raw key event is stamped with the frame it arrived on, so latency can be
 * measured from the real input path (see LastKeyFrame).
 */
UCLASS()
class TEMPER_API ATemperPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATemperPlayerController();

	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	void CycleCamera();
	void SetMechCamera(ETemperCameraMode Mode);
	ETemperCameraMode GetCameraMode() const { return CameraMode; }
	ABastionMech* Mech() const { return Cast<ABastionMech>(GetPawn()); }
	ACameraActor* GetObserveCamera() const { return ObserveCamera; }

	/** Frame number and cycle timestamp of the most recent key-down event that reached the controller. */
	uint64 LastKeyFrame = 0;
	uint64 LastKeyCycles = 0;
	FKey LastKey;

	/** Scenario runs: drop real hardware input except AllowRealKey (the OS-input latency probe). */
	bool bScenarioInputOnly = false;
	FKey AllowRealKey;

	UPROPERTY(EditAnywhere, Category = "Input") float MouseSensitivity = 0.6f; // x0.07 engine axis scale = ~0.042 deg per mouse count
	UPROPERTY(EditAnywhere, Category = "Input") float StickLookRate = 160.f;
	UPROPERTY(EditAnywhere, Category = "Observe") float ObserveSpeed = 1500.f;

private:
	void OnMove(const FInputActionValue& Value);
	void OnMoveStop(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnStickLook(const FInputActionValue& Value);
	void OnSprint(const FInputActionValue& Value);
	void OnSprintStop(const FInputActionValue& Value);
	void OnJet();
	void OnCamera();
	void OnRise(const FInputActionValue& Value);
	void ApplyLook(const FVector2D& Deg);

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Context;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> StickLookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JetAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CameraAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RiseAction;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> ObserveCamera;

	ETemperCameraMode CameraMode = ETemperCameraMode::ThirdPerson;
	FVector2D ObserveMove = FVector2D::ZeroVector;
	float ObserveRise = 0.f;
	bool bObserveFast = false;
	FRotator ObserveRot = FRotator::ZeroRotator;
};
