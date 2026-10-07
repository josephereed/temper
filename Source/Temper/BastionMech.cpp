#include "BastionMech.h"

#include "BastionMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Temper.h"

namespace
{
	const FName ColorParam(TEXT("Color"));

	constexpr float CapsuleRadius = 165.f;
	constexpr float CapsuleHalfHeight = 215.f;
	constexpr float HipHeight = 235.f;     // above ground at rest
	constexpr float HipHalfWidth = 85.f;
	constexpr float StanceHalfWidth = 95.f; // feet sit a little wider than the hips
	constexpr float ThighLength = 150.f;
	constexpr float ShinLength = 150.f;

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

	FLinearColor Hex(const TCHAR* H) { return FLinearColor::FromSRGBColor(FColor::FromHex(H)); }

	FVector Flat(const FVector& V) { return FVector(V.X, V.Y, 0.f); }

	float SmoothStep01(float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * (3.f - 2.f * T); }
}

void ABastionMech::FSpring::Step(float Target, float Omega, float Zeta, float Dt)
{
	// Semi-implicit damped spring, sub-stepped for stability at low frame rates.
	const int32 Sub = FMath::Clamp(FMath::CeilToInt(Dt / 0.005f), 1, 20);
	const float H = Dt / Sub;
	for (int32 i = 0; i < Sub; ++i)
	{
		const float A = Omega * Omega * (Target - X) - 2.f * Zeta * Omega * V;
		V += A * H;
		X += V * H;
	}
}

ABastionMech::ABastionMech(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UBastionMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	// The rig reads the movement result of this frame, so it ticks after movement.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
	GetMesh()->SetVisibility(false);

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(GetCapsuleComponent());
	Visual->SetRelativeLocation(FVector(0.f, 0.f, -CapsuleHalfHeight));

	Pelvis = CreateDefaultSubobject<USceneComponent>(TEXT("Pelvis"));
	Pelvis->SetupAttachment(Visual);
	Pelvis->SetRelativeLocation(FVector(0.f, 0.f, HipHeight));

	TorsoPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TorsoPivot"));
	TorsoPivot->SetupAttachment(Pelvis);
	TorsoPivot->SetRelativeLocation(FVector(0.f, 0.f, 35.f));

	CockpitCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
	CockpitCamera->SetupAttachment(TorsoPivot);
	CockpitCamera->SetRelativeLocation(FVector(125.f, 0.f, 125.f));
	CockpitCamera->SetFieldOfView(95.f);
	CockpitCamera->bUsePawnControlRotation = false;

	ChaseArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ChaseArm"));
	ChaseArm->SetupAttachment(GetCapsuleComponent());
	ChaseArm->SetRelativeLocation(FVector(0.f, 0.f, 170.f));
	ChaseArm->TargetArmLength = 1100.f;
	ChaseArm->SocketOffset = FVector(0.f, 230.f, 110.f);
	ChaseArm->bUsePawnControlRotation = true;
	ChaseArm->bDoCollisionTest = true;
	ChaseArm->ProbeSize = 20.f;
	// Translation lag only: the camera trails the mass, but aim stays 1:1 with the mouse.
	ChaseArm->bEnableCameraLag = true;
	ChaseArm->CameraLagSpeed = 6.f;
	ChaseArm->CameraLagMaxDistance = 260.f;
	ChaseArm->bEnableCameraRotationLag = false;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(ChaseArm, USpringArmComponent::SocketName);
	ChaseCamera->SetFieldOfView(90.f);
	ChaseCamera->bUsePawnControlRotation = false;
}

UBastionMovementComponent* ABastionMech::Move() const
{
	return Cast<UBastionMovementComponent>(GetCharacterMovement());
}

UStaticMeshComponent* ABastionMech::AddPart(USceneComponent* Parent, const FVector& Loc, const FVector& Size, const FLinearColor& Color, bool bCastShadow)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetStaticMesh(CubeMesh());
	Part->SetupAttachment(Parent);
	Part->SetRelativeLocation(Loc);
	Part->SetRelativeScale3D(Size / 100.f);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCastShadow(bCastShadow);
	UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(TintMaterial(), Part);
	Mat->SetVectorParameterValue(ColorParam, Color);
	Part->SetMaterial(0, Mat);
	Part->RegisterComponent();
	return Part;
}

void ABastionMech::BuildBody()
{
	const FLinearColor Hull = Hex(TEXT("5E6650"));
	const FLinearColor Dark = Hex(TEXT("2F3330"));
	const FLinearColor Accent = Hex(TEXT("C7772A"));
	const FLinearColor Glass = Hex(TEXT("1A2630"));

	// Pelvis block (hull): moves with the legs, not the aim.
	AddPart(Pelvis, FVector(0.f, 0.f, 0.f), FVector(150.f, 200.f, 90.f), Dark);

	// Torso and cockpit ride the aim. Wide and squat so it reads as a machine, not a person.
	UpperBody.Add(AddPart(TorsoPivot, FVector(-10.f, 0.f, 95.f), FVector(230.f, 270.f, 150.f), Hull));
	UpperBody.Add(AddPart(TorsoPivot, FVector(95.f, 0.f, 120.f), FVector(70.f, 150.f, 70.f), Glass));   // canopy
	UpperBody.Add(AddPart(TorsoPivot, FVector(-25.f, 0.f, 185.f), FVector(150.f, 120.f, 30.f), Dark));  // spine plate
	UpperBody.Add(AddPart(TorsoPivot, FVector(20.f, 0.f, 210.f), FVector(40.f, 40.f, 30.f), Accent));   // sensor
	for (float Side : { -1.f, 1.f })
	{
		// Shoulder pods (empty hardpoints: no weapons this milestone) and armour skirts.
		UpperBody.Add(AddPart(TorsoPivot, FVector(10.f, Side * 160.f, 110.f), FVector(170.f, 60.f, 110.f), Hull * 0.85f));
		UpperBody.Add(AddPart(TorsoPivot, FVector(70.f, Side * 160.f, 110.f), FVector(60.f, 50.f, 50.f), Dark));
		UpperBody.Add(AddPart(TorsoPivot, FVector(-130.f, Side * 70.f, 70.f), FVector(50.f, 90.f, 110.f), Dark)); // jump-jet housing
		Thrusters.Add(AddPart(TorsoPivot, FVector(-160.f, Side * 70.f, 5.f), FVector(40.f, 60.f, 30.f), Accent * 0.3f, false));
	}

	// Legs: thigh, shin, foot per side, placed every tick by the rig.
	for (int32 i = 0; i < 2; ++i)
	{
		LegParts.Add(AddPart(Visual, FVector::ZeroVector, FVector(100.f), Hull * 0.9f));
		LegParts.Add(AddPart(Visual, FVector::ZeroVector, FVector(100.f), Dark));
		LegParts.Add(AddPart(Visual, FVector::ZeroVector, FVector(100.f), Hull * 0.75f));
	}

	// Cockpit frame: struts at the edges of the first-person view.
	const FLinearColor Frame = Hex(TEXT("1E201E"));
	auto FramePart = [&](const FVector& Loc, const FVector& Size, const FRotator& Rot)
	{
		UStaticMeshComponent* P = AddPart(CockpitCamera, Loc, Size, Frame, false);
		P->SetRelativeRotation(Rot);
		CockpitFrame.Add(P);
	};
	// At 30 cm with a 95 degree FOV the view is ~70 x 39 cm, so these sit just inside the edges.
	FramePart(FVector(30.f, -33.f, 0.f), FVector(2.f, 5.f, 60.f), FRotator(0.f, 0.f, -12.f));    // left pillar
	FramePart(FVector(30.f, 33.f, 0.f), FVector(2.f, 5.f, 60.f), FRotator(0.f, 0.f, 12.f));      // right pillar
	FramePart(FVector(30.f, 0.f, 18.8f), FVector(2.f, 70.f, 2.5f), FRotator::ZeroRotator);       // canopy bow
	FramePart(FVector(32.f, 0.f, -19.5f), FVector(8.f, 70.f, 4.f), FRotator(-15.f, 0.f, 0.f));   // dash lip
}

void ABastionMech::BeginPlay()
{
	Super::BeginPlay();
	BuildBody();

	const FVector Ground = GetGroundLocation();
	const FVector Right = GetActorRightVector();
	for (int32 i = 0; i < 2; ++i)
	{
		Feet[i].Side = i == 0 ? -1.f : 1.f;
		Feet[i].Plant = Ground + Right * Feet[i].Side * StanceHalfWidth;
		Feet[i].Pos = Feet[i].Plant;
	}
	SetCameraMode(CameraMode);
}

FVector ABastionMech::GetGroundLocation() const
{
	return GetActorLocation() - FVector(0.f, 0.f, CapsuleHalfHeight);
}

FTransform ABastionMech::GetTorsoTransform() const
{
	return TorsoPivot->GetComponentTransform();
}

void ABastionMech::AddEvent(const FString& Name, float Value)
{
	EventLog.Add({ Name, Value, GFrameCounter });
	UE_LOG(LogTemper, Verbose, TEXT("EVENT %s %.2f"), *Name, Value);
}

void ABastionMech::SetMoveInput(const FVector2D& Input)
{
	MoveInput = Input.GetClampedToMaxSize(1.f);
	if (!MoveInput.IsNearlyZero() && Controller)
	{
		// Camera-relative: forward is where you're looking, whatever the legs are doing.
		const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector Fwd = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
		const FVector Rt = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
		AddMovementInput(Fwd * MoveInput.Y + Rt * MoveInput.X);
	}
}

void ABastionMech::SetSprint(bool bOn)
{
	UBastionMovementComponent* M = Move();
	if (bOn && !M->bWantsSprint && !MoveInput.IsNearlyZero() && M->IsMovingOnGround())
	{
		// Throttle-up shove: the torso dips into the sprint the frame it's pressed.
		LeanFwd.X += 0.8f;
		LeanFwd.V += 45.f;
		Drop.X += 2.f;
		Drop.V += 60.f;
	}
	M->bWantsSprint = bOn;
}

void ABastionMech::ResetRig()
{
	Drop = FSpring();
	LeanFwd = FSpring();
	LeanSide = FSpring();
	HullTurnSpeed = 0.f;
	Trauma = 0.f;
	JetCooldownLeft = 0.f;
	JetBufferLeft = 0.f;
	MoveInput = PrevMoveInput = FVector2D::ZeroVector;
	PrevVelocity = FVector::ZeroVector;
	Move()->bWantsSprint = false;
	const FVector Ground = GetGroundLocation();
	for (FFoot& Foot : Feet)
	{
		Foot.bSwing = false;
		Foot.T = 1.f;
		Foot.Plant = Ground + GetActorRightVector() * Foot.Side * StanceHalfWidth;
		Foot.Pos = Foot.Plant;
	}
}

void ABastionMech::Jet()
{
	JetBufferLeft = JetBufferTime;
	if (Move()->IsMovingOnGround() && JetCooldownLeft <= 0.f)
	{
		DoJet();
	}
}

void ABastionMech::DoJet()
{
	JetBufferLeft = 0.f;
	JetCooldownLeft = JetCooldown;
	UBastionMovementComponent* M = Move();

	FVector H = Flat(GetVelocity());
	const FVector InputDir = Flat(M->GetCurrentAcceleration()).GetSafeNormal();
	H += InputDir * JetForwardBoost;
	H = H.GetClampedToMaxSize(JetMaxHorizontal);

	// Applied now (not via LaunchCharacter's next-tick pending launch) so the hop starts this frame.
	M->Velocity = FVector(H.X, H.Y, JetUpSpeed);
	M->SetMovementMode(MOVE_Falling);
	M->JetBurnLeft = JetBurnTime;
	ThrusterGlow = 1.f;

	// Ignition shove: torso snaps nose-up against the thrust this frame, pelvis squats after.
	// (No instant squat: it would cancel the hull's first-frame rise and hide the response.)
	LeanFwd.X -= 1.5f;
	LeanFwd.V -= 50.f;
	Drop.V += 260.f;
	AddTrauma(0.25f);
	AddEvent(TEXT("jet"), H.Size());
}

void ABastionMech::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	const float Impact = FMath::Max(0.f, -GetVelocity().Z);
	const float Severity = Move()->RegisterLanding(Impact);

	// Ground-shaking touchdown: the pelvis slams down and recovers, camera takes a hit.
	Drop.V += 180.f + 700.f * Severity + Impact * 0.15f;
	LeanFwd.V += 20.f + 60.f * Severity;
	AddTrauma(0.35f + 0.6f * Severity);

	for (FFoot& Foot : Feet)
	{
		Foot.bSwing = false;
		Foot.T = 1.f;
		Foot.Plant = FVector(Foot.Pos.X, Foot.Pos.Y, GroundZAt(Foot.Pos, GetGroundLocation().Z));
		Foot.Pos = Foot.Plant;
	}
	AddEvent(TEXT("land"), Impact);
	// A jet press buffered just before touchdown fires from Tick once the cooldown allows.
}

void ABastionMech::SetCameraMode(ETemperCameraMode Mode)
{
	CameraMode = Mode;
	const bool bFirst = Mode == ETemperCameraMode::FirstPerson;
	CockpitCamera->SetActive(bFirst);
	ChaseCamera->SetActive(Mode == ETemperCameraMode::ThirdPerson);
	for (UStaticMeshComponent* P : UpperBody)
	{
		// Keep the shadow so the cockpit still has a body on the ground.
		P->SetHiddenInGame(bFirst);
		P->bCastHiddenShadow = true;
	}
	for (UStaticMeshComponent* P : CockpitFrame)
	{
		P->SetHiddenInGame(!bFirst);
	}
}

void ABastionMech::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const float Dt = FMath::Min(DeltaTime, 0.05f);

	JetCooldownLeft = FMath::Max(0.f, JetCooldownLeft - Dt);
	JetBufferLeft = FMath::Max(0.f, JetBufferLeft - Dt);
	if (JetBufferLeft > 0.f && JetCooldownLeft <= 0.f && Move()->IsMovingOnGround())
	{
		DoJet();
	}
	Move()->Facing = GetActorForwardVector();

	UpdateHull(Dt);
	UpdateSprings(Dt);
	UpdateLegs(Dt);
	UpdateCameras(Dt);

	PrevMoveInput = MoveInput;
	PrevVelocity = GetVelocity();
	bWasFalling = Move()->IsFalling();
}

void ABastionMech::UpdateHull(float Dt)
{
	if (!Controller)
	{
		return;
	}
	// The legs turn toward the aim with a rate limit and spin-up: a heavy machine pivoting.
	const float Target = Controller->GetControlRotation().Yaw;
	const float Error = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Target);
	const float Desired = FMath::Clamp(Error * 6.f, -HullTurnRate, HullTurnRate);
	HullTurnSpeed = FMath::FInterpConstantTo(HullTurnSpeed, Desired, Dt, HullTurnAccel);
	float Step = HullTurnSpeed * Dt;
	if (FMath::Abs(Step) > FMath::Abs(Error))
	{
		Step = Error;
		HullTurnSpeed = 0.f;
	}
	SetActorRotation(FRotator(0.f, GetActorRotation().Yaw + Step, 0.f));

	// The torso follows the aim exactly (yaw relative to the hull, pitch from the view, a share of it).
	const FRotator Control = Controller->GetControlRotation();
	const float TorsoYaw = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Control.Yaw);
	const float ViewPitch = FRotator::NormalizeAxis(Control.Pitch);
	TorsoPivot->SetRelativeRotation(FRotator(-LeanFwd.X + ViewPitch * 0.25f, TorsoYaw, LeanSide.X));
}

void ABastionMech::UpdateSprings(float Dt)
{
	const FVector Vel = GetVelocity();
	const FVector Accel = Dt > 0.f ? Flat(Vel - PrevVelocity) / Dt : FVector::ZeroVector;
	const FTransform Hull = GetActorTransform();
	const FVector LocalAccel = Move()->IsFalling() ? FVector::ZeroVector : Hull.InverseTransformVectorNoScale(Accel);

	// Input kick: the instant the stick changes, the machine visibly commits (hips drop, the
	// torso rocks into the new direction) - this is the frame-one response to a keypress.
	if (Controller && !Move()->IsFalling())
	{
		const FVector2D Change = MoveInput - PrevMoveInput;
		if (!Change.IsNearlyZero(0.05f))
		{
			const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
			const FVector World = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * Change.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * Change.X;
			const FVector Local = Hull.InverseTransformVectorNoScale(World);
			// Part displacement (seen this frame), part velocity (the follow-through).
			LeanFwd.X += 1.2f * Local.X;
			LeanFwd.V += 60.f * Local.X;
			LeanSide.X += 0.9f * Local.Y;
			LeanSide.V += 45.f * Local.Y;
			Drop.X += 3.f * Change.Size();
			Drop.V += 80.f * Change.Size();
		}
	}

	// Inertia: braking pitches the nose down, accelerating rocks it back.
	const float LeanPerAccel = 0.006f; // deg per cm/s^2
	LeanFwd.Step(FMath::Clamp(-LocalAccel.X * LeanPerAccel, -6.f, 9.f), 9.f, 0.45f, Dt);
	LeanSide.Step(FMath::Clamp(LocalAccel.Y * LeanPerAccel, -6.f, 6.f), 9.f, 0.5f, Dt);

	// Pelvis: sits lower at speed (longer stride) and recovers from footfall/landing hits.
	const float Speed = Flat(Vel).Size();
	const float Crouch = Move()->IsFalling() ? -10.f : 18.f * FMath::Clamp(Speed / Move()->SprintSpeed, 0.f, 1.f);
	Drop.Step(Crouch, 11.f, 0.55f, Dt);
	Drop.X = FMath::Clamp(Drop.X, -40.f, 110.f);

	Pelvis->SetRelativeLocation(FVector(0.f, 0.f, HipHeight - Drop.X));

	// Thruster glow fades after the burn.
	ThrusterGlow = FMath::Max(0.f, ThrusterGlow - Dt * 2.5f);
	for (UStaticMeshComponent* T : Thrusters)
	{
		T->SetRelativeScale3D(FVector(0.4f, 0.6f, 0.3f + ThrusterGlow * 1.2f));
	}
}

float ABastionMech::GroundZAt(const FVector& P, float Fallback) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BastionFoot), false, this);
	const FVector Start(P.X, P.Y, GetGroundLocation().Z + 120.f);
	const FVector End(P.X, P.Y, GetGroundLocation().Z - 250.f);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return Hit.ImpactPoint.Z;
	}
	return Fallback;
}

FVector ABastionMech::FootIdeal(const FFoot& Foot, float Lead) const
{
	const FVector Ground = GetGroundLocation();
	const FVector P = Ground + GetActorRightVector() * Foot.Side * StanceHalfWidth + Flat(GetVelocity()) * Lead;
	return FVector(P.X, P.Y, GroundZAt(P, Ground.Z));
}

void ABastionMech::Footfall(float Strength)
{
	Drop.V += 40.f + 120.f * Strength;
	AddTrauma(0.05f + 0.1f * Strength);
	AddEvent(TEXT("footfall"), Strength);
}

void ABastionMech::UpdateLegs(float Dt)
{
	const FVector Vel = Flat(GetVelocity());
	const float Speed = Vel.Size();
	const float Speed01 = FMath::Clamp(Speed / Move()->SprintSpeed, 0.f, 1.f);
	const bool bAir = Move()->IsFalling();

	if (bAir)
	{
		// Legs hang under the hips, tucked slightly back.
		for (FFoot& Foot : Feet)
		{
			const FVector Hang = Pelvis->GetComponentLocation() + GetActorRightVector() * Foot.Side * StanceHalfWidth
				- GetActorForwardVector() * 40.f - FVector(0.f, 0.f, ThighLength + ShinLength - 45.f);
			Foot.Pos = FMath::VInterpTo(Foot.Pos, Hang, Dt, 12.f);
			Foot.bSwing = false;
			Foot.T = 1.f;
		}
	}
	else
	{
		const float StepTime = FMath::Lerp(0.36f, 0.24f, Speed01);
		const float Lead = StepTime * 0.5f;
		StillTime = Speed < 30.f ? StillTime + Dt : 0.f;

		// Advance swings.
		for (int32 i = 0; i < 2; ++i)
		{
			FFoot& Foot = Feet[i];
			if (!Foot.bSwing)
			{
				Foot.Pos = Foot.Plant;
				continue;
			}
			Foot.T += Dt / Foot.Duration;
			const FVector To = FootIdeal(Foot, Lead);
			if (Foot.T >= 1.f)
			{
				Foot.bSwing = false;
				Foot.Plant = To;
				Foot.Pos = To;
				Footfall(FMath::Max(0.25f, Speed01));
				continue;
			}
			const float S = SmoothStep01(Foot.T);
			const float Lift = FMath::Sin(PI * Foot.T) * (30.f + 35.f * Speed01);
			Foot.Pos = FMath::Lerp(Foot.From, To, S) + FVector(0.f, 0.f, Lift);
		}

		// Pick the next foot to step: the one furthest from where it should be, one at a time.
		const float Trigger = FMath::Max(45.f, Speed * StepTime * 0.9f);
		int32 Best = INDEX_NONE;
		float BestDist = 0.f;
		for (int32 i = 0; i < 2; ++i)
		{
			const FFoot& Foot = Feet[i];
			const FFoot& Other = Feet[1 - i];
			if (Foot.bSwing || (Other.bSwing && Other.T < 0.75f))
			{
				continue;
			}
			const float Dist = FVector::Dist2D(Foot.Plant, FootIdeal(Foot, Lead));
			const bool bSettle = StillTime > 0.25f && Dist > 18.f;
			if ((Dist > Trigger || bSettle) && Dist > BestDist)
			{
				Best = i;
				BestDist = Dist;
			}
		}
		// A fresh input kicks the first step immediately rather than waiting for the trigger distance.
		if (Best == INDEX_NONE && !MoveInput.IsNearlyZero() && PrevMoveInput.IsNearlyZero() && !Feet[0].bSwing && !Feet[1].bSwing)
		{
			const FVector Dir = Flat(Move()->GetCurrentAcceleration()).GetSafeNormal();
			Best = (Dir | GetActorRightVector()) > 0.f ? 1 : 0;
		}
		if (Best != INDEX_NONE)
		{
			FFoot& Foot = Feet[Best];
			Foot.bSwing = true;
			Foot.T = 0.f;
			Foot.Duration = StepTime;
			Foot.From = Foot.Plant;
		}
	}

	// Two-bone IK from hip to foot, knees forward.
	const FVector Fwd = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	for (int32 i = 0; i < 2; ++i)
	{
		const FFoot& Foot = Feet[i];
		const FVector Hip = Pelvis->GetComponentLocation() + Right * Foot.Side * HipHalfWidth;
		const FVector Ankle = Foot.Pos + FVector(0.f, 0.f, 30.f);
		FVector ToFoot = Ankle - Hip;
		const float Reach = ThighLength + ShinLength - 1.f;
		float D = ToFoot.Size();
		const FVector Dir = D > KINDA_SMALL_NUMBER ? ToFoot / D : -FVector::UpVector;
		D = FMath::Clamp(D, 20.f, Reach);
		const float A = (ThighLength * ThighLength - ShinLength * ShinLength + D * D) / (2.f * D);
		const float H = FMath::Sqrt(FMath::Max(0.f, ThighLength * ThighLength - A * A));
		FVector Bend = Fwd - (Fwd | Dir) * Dir;
		Bend = Bend.GetSafeNormal(KINDA_SMALL_NUMBER, Fwd);
		const FVector Knee = Hip + Dir * A + Bend * H;
		const FVector AnkleReached = Hip + Dir * D;

		PlaceSegment(LegParts[i * 3 + 0], Hip, Knee, 60.f, Right);
		PlaceSegment(LegParts[i * 3 + 1], Knee, AnkleReached, 50.f, Right);
		UStaticMeshComponent* FootPart = LegParts[i * 3 + 2];
		FootPart->SetWorldLocationAndRotation(AnkleReached - FVector(0.f, 0.f, 15.f) + Fwd * 15.f, GetActorRotation());
		FootPart->SetWorldScale3D(FVector(1.3f, 0.8f, 0.3f));
	}
}

void ABastionMech::PlaceSegment(UStaticMeshComponent* Seg, const FVector& A, const FVector& B, float Width, const FVector& SideAxis)
{
	const FVector D = B - A;
	const float Len = D.Size();
	if (Len < KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FRotator Rot = FRotationMatrix::MakeFromXY(D / Len, SideAxis).Rotator();
	Seg->SetWorldLocationAndRotation((A + B) * 0.5f, Rot);
	Seg->SetWorldScale3D(FVector((Len + Width * 0.5f) / 100.f, Width / 100.f, Width / 100.f));
}

void ABastionMech::UpdateCameras(float Dt)
{
	// Shake: trauma squared, smooth noise, decays ~1.3/s. Footfalls add a little, landings a lot.
	Trauma = FMath::Max(0.f, Trauma - Dt * 1.3f);
	ShakeTime += Dt;
	const float Amount = Trauma * Trauma;
	const float F = 22.f;
	const FRotator Shake(
		Amount * 4.f * FMath::PerlinNoise1D(ShakeTime * F + 1.7f),
		Amount * 2.5f * FMath::PerlinNoise1D(ShakeTime * F + 31.3f),
		Amount * 3.f * FMath::PerlinNoise1D(ShakeTime * F + 77.1f));
	const FVector ShakeOffset(0.f, 0.f, Amount * 18.f * FMath::PerlinNoise1D(ShakeTime * F + 5.5f));

	if (Controller)
	{
		// The cockpit view is the aim (world space), carried by the torso's bob and lean.
		const FRotator Control = Controller->GetControlRotation();
		const FRotator LeanRoll(-LeanFwd.X * 0.35f, 0.f, LeanSide.X * 0.5f);
		CockpitCamera->SetWorldRotation((FQuat(Control) * FQuat(LeanRoll) * FQuat(Shake)).Rotator());
		CockpitCamera->SetRelativeLocation(FVector(125.f, 0.f, 125.f) + ShakeOffset);
	}
	ChaseCamera->SetRelativeLocationAndRotation(ShakeOffset * 1.5f, Shake * 0.8f);
}
