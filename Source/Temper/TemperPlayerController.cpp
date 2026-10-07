#include "TemperPlayerController.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Temper.h"

namespace
{
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = Type;
		return Action;
	}

	/** Maps a key to one direction of a 2D/1D action (WASD-style). */
	void MapDirection(UInputMappingContext* Ctx, UInputAction* Action, FKey Key, bool bVertical, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = Ctx->MapKey(Action, Key);
		if (bVertical)
		{
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Ctx);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Mapping.Modifiers.Add(Swizzle);
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
		}
	}
}

ATemperPlayerController::ATemperPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATemperPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ViewPitchMin = -55.f;
		PlayerCameraManager->ViewPitchMax = 50.f;
	}
	SetControlRotation(InPawn->GetActorRotation());
	SetMechCamera(CameraMode);
}

bool ATemperPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (bScenarioInputOnly && !Params.IsSimulatedInput() && Params.Key != AllowRealKey)
	{
		return true; // scripted run: the desk's real keyboard/mouse must not steer the test
	}
	if (Params.Event == IE_Pressed && !Params.Key.IsAxis1D() && !Params.Key.IsAxis2D())
	{
		LastKeyFrame = GFrameNumber;
		LastKeyCycles = FPlatformTime::Cycles64();
		LastKey = Params.Key;
	}
	return Super::InputKey(Params);
}

void ATemperPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	Context = NewObject<UInputMappingContext>(this, TEXT("TemperContext"));
	MoveAction = MakeAction(this, TEXT("Move"), EInputActionValueType::Axis2D);
	LookAction = MakeAction(this, TEXT("Look"), EInputActionValueType::Axis2D);
	StickLookAction = MakeAction(this, TEXT("StickLook"), EInputActionValueType::Axis2D);
	SprintAction = MakeAction(this, TEXT("Sprint"), EInputActionValueType::Boolean);
	JetAction = MakeAction(this, TEXT("Jet"), EInputActionValueType::Boolean);
	CameraAction = MakeAction(this, TEXT("Camera"), EInputActionValueType::Boolean);
	RiseAction = MakeAction(this, TEXT("Rise"), EInputActionValueType::Axis1D);

	MapDirection(Context, MoveAction, EKeys::W, true, false);
	MapDirection(Context, MoveAction, EKeys::S, true, true);
	MapDirection(Context, MoveAction, EKeys::D, false, false);
	MapDirection(Context, MoveAction, EKeys::A, false, true);
	Context->MapKey(MoveAction, EKeys::Gamepad_Left2D);

	Context->MapKey(LookAction, EKeys::Mouse2D);
	{
		FEnhancedActionKeyMapping& Stick = Context->MapKey(StickLookAction, EKeys::Gamepad_Right2D);
		UInputModifierDeadZone* Dead = NewObject<UInputModifierDeadZone>(Context);
		Dead->LowerThreshold = 0.15f;
		Stick.Modifiers.Add(Dead);
	}

	Context->MapKey(SprintAction, EKeys::LeftShift);
	Context->MapKey(SprintAction, EKeys::Gamepad_LeftThumbstick);
	Context->MapKey(JetAction, EKeys::SpaceBar);
	Context->MapKey(JetAction, EKeys::Gamepad_FaceButton_Bottom);
	Context->MapKey(CameraAction, EKeys::V);
	Context->MapKey(CameraAction, EKeys::Gamepad_Special_Left);

	// Observe-mode altitude (the jet key doubles as "up" there).
	Context->MapKey(RiseAction, EKeys::E);
	Context->MapKey(RiseAction, EKeys::SpaceBar);
	{
		FEnhancedActionKeyMapping& Q = Context->MapKey(RiseAction, EKeys::Q);
		Q.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		FEnhancedActionKeyMapping& C = Context->MapKey(RiseAction, EKeys::LeftControl);
		C.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}

	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(InputComponent);
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATemperPlayerController::OnMove);
	Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &ATemperPlayerController::OnMoveStop);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ATemperPlayerController::OnLook);
	Input->BindAction(StickLookAction, ETriggerEvent::Triggered, this, &ATemperPlayerController::OnStickLook);
	Input->BindAction(SprintAction, ETriggerEvent::Triggered, this, &ATemperPlayerController::OnSprint);
	Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ATemperPlayerController::OnSprintStop);
	Input->BindAction(JetAction, ETriggerEvent::Started, this, &ATemperPlayerController::OnJet);
	Input->BindAction(CameraAction, ETriggerEvent::Started, this, &ATemperPlayerController::OnCamera);
	Input->BindAction(RiseAction, ETriggerEvent::Triggered, this, &ATemperPlayerController::OnRise);
	Input->BindAction(RiseAction, ETriggerEvent::Completed, this, &ATemperPlayerController::OnRise);

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(Context, 0);
	}
}

void ATemperPlayerController::OnMove(const FInputActionValue& Value)
{
	const FVector2D In = Value.Get<FVector2D>();
	if (CameraMode == ETemperCameraMode::Observe)
	{
		ObserveMove = In;
		return;
	}
	if (ABastionMech* M = Mech())
	{
		M->SetMoveInput(In);
	}
}

void ATemperPlayerController::OnMoveStop(const FInputActionValue& Value)
{
	ObserveMove = FVector2D::ZeroVector;
	if (ABastionMech* M = Mech())
	{
		M->SetMoveInput(FVector2D::ZeroVector);
	}
}

void ATemperPlayerController::OnLook(const FInputActionValue& Value)
{
	ApplyLook(Value.Get<FVector2D>() * MouseSensitivity);
}

void ATemperPlayerController::OnStickLook(const FInputActionValue& Value)
{
	ApplyLook(Value.Get<FVector2D>() * StickLookRate * GetWorld()->GetDeltaSeconds());
}

void ATemperPlayerController::ApplyLook(const FVector2D& Deg)
{
	if (CameraMode == ETemperCameraMode::Observe)
	{
		ObserveRot.Yaw += Deg.X;
		ObserveRot.Pitch = FMath::Clamp(ObserveRot.Pitch + Deg.Y, -89.f, 89.f);
		return;
	}
	// Straight into this frame's control rotation (UpdateRotation runs right after input).
	AddYawInput(Deg.X);
	AddPitchInput(-Deg.Y);
}

void ATemperPlayerController::OnSprint(const FInputActionValue& Value)
{
	bObserveFast = true;
	if (ABastionMech* M = Mech())
	{
		M->SetSprint(CameraMode != ETemperCameraMode::Observe);
	}
}

void ATemperPlayerController::OnSprintStop(const FInputActionValue& Value)
{
	bObserveFast = false;
	if (ABastionMech* M = Mech())
	{
		M->SetSprint(false);
	}
}

void ATemperPlayerController::OnJet()
{
	if (CameraMode == ETemperCameraMode::Observe)
	{
		return;
	}
	if (ABastionMech* M = Mech())
	{
		M->Jet();
	}
}

void ATemperPlayerController::OnRise(const FInputActionValue& Value)
{
	ObserveRise = Value.Get<float>();
}

void ATemperPlayerController::OnCamera()
{
	CycleCamera();
}

void ATemperPlayerController::CycleCamera()
{
	switch (CameraMode)
	{
	case ETemperCameraMode::FirstPerson: SetMechCamera(ETemperCameraMode::ThirdPerson); break;
	case ETemperCameraMode::ThirdPerson: SetMechCamera(ETemperCameraMode::Observe); break;
	default: SetMechCamera(ETemperCameraMode::FirstPerson); break;
	}
}

void ATemperPlayerController::SetMechCamera(ETemperCameraMode Mode)
{
	const ETemperCameraMode Old = CameraMode;
	CameraMode = Mode;
	ABastionMech* M = Mech();
	if (!M)
	{
		return;
	}

	if (Mode == ETemperCameraMode::Observe)
	{
		if (!ObserveCamera)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ObserveCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
			ObserveCamera->GetCameraComponent()->bConstrainAspectRatio = false;
			ObserveCamera->GetCameraComponent()->SetFieldOfView(90.f);
		}
		// Start where the player was looking from.
		const FMinimalViewInfo& View = PlayerCameraManager->GetCameraCacheView();
		ObserveRot = View.Rotation;
		ObserveCamera->SetActorLocationAndRotation(View.Location, ObserveRot);
		M->SetMoveInput(FVector2D::ZeroVector);
		M->SetSprint(false);
		M->SetCameraMode(ETemperCameraMode::ThirdPerson); // the body is visible to the spectator
		SetViewTarget(ObserveCamera);
	}
	else
	{
		M->SetCameraMode(Mode);
		if (Old == ETemperCameraMode::Observe || GetViewTarget() != M)
		{
			SetViewTarget(M);
		}
	}
	UE_LOG(LogTemper, Log, TEXT("CAMERA mode=%d"), static_cast<int32>(Mode));
}

void ATemperPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (CameraMode == ETemperCameraMode::Observe && ObserveCamera)
	{
		const FRotationMatrix R(ObserveRot);
		const FVector Dir = R.GetUnitAxis(EAxis::X) * ObserveMove.Y + R.GetUnitAxis(EAxis::Y) * ObserveMove.X + FVector::UpVector * ObserveRise;
		const float Speed = ObserveSpeed * (bObserveFast ? 3.f : 1.f);
		ObserveCamera->SetActorLocationAndRotation(ObserveCamera->GetActorLocation() + Dir.GetClampedToMaxSize(1.f) * Speed * DeltaTime, ObserveRot);
	}
}
