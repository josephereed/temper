#include "TemperScenarioRunner.h"

#include "BastionMech.h"
#include "BastionMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HighResScreenshot.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Temper.h"
#include "TemperArena.h"
#include "TemperPlayerController.h"
#include "Widgets/SWindow.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	constexpr float SequenceLeg = 1000.f; // cm: "walk forward 10 m", "sprint 10 m"

	class FErrorCounter : public FOutputDevice
	{
	public:
		int32 Errors = 0;
		TArray<FString> First;
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Fatal)
			{
				++Errors;
				if (First.Num() < 10)
				{
					First.Add(FString::Printf(TEXT("[%s] %s"), *Category.ToString(), V));
				}
			}
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
	};

	FString F(float V) { return FString::Printf(TEXT("%.3f"), V); }

	float Spread(const TArray<float>& Values)
	{
		if (Values.Num() == 0)
		{
			return 0.f;
		}
		float Lo = Values[0], Hi = Values[0];
		for (float V : Values)
		{
			Lo = FMath::Min(Lo, V);
			Hi = FMath::Max(Hi, V);
		}
		return Hi - Lo;
	}

	float Mean(const TArray<float>& Values)
	{
		float S = 0.f;
		for (float V : Values)
		{
			S += V;
		}
		return Values.Num() ? S / Values.Num() : 0.f;
	}
}

ATemperScenarioRunner::ATemperScenarioRunner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

ABastionMech* ATemperScenarioRunner::Mech() const
{
	return PC() ? PC()->Mech() : nullptr;
}

ATemperPlayerController* ATemperScenarioRunner::PC() const
{
	return Cast<ATemperPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
}

void ATemperScenarioRunner::Start(const FString& Spec)
{
	ErrorCounter = MakeUnique<FErrorCounter>();
	GLog->AddOutputDevice(ErrorCounter.Get());

	// At the end of each game frame (after the viewport has queued its draw), queue a render
	// command: it runs once the render thread has finished submitting that frame's scene.
	EndFrameRTHandle = FCoreDelegates::OnEndFrame.AddLambda([this]()
	{
		const uint64 Enqueued = FPlatformTime::Cycles64();
		ENQUEUE_RENDER_COMMAND(TemperFrameEnd)([this, Enqueued](FRHICommandListImmediate&)
		{
			FScopeLock Lock(&RTLock);
			RTFrameEnds.Add(TPair<uint64, uint64>(Enqueued, FPlatformTime::Cycles64()));
		});
	});

	TArray<FString> Names;
	Spec.ParseIntoArray(Names, TEXT("+"));
	const bool bAll = Names.Contains(TEXT("all"));
	auto Want = [&](const TCHAR* N) { return bAll || Names.Contains(N); };

	// Let the world settle: arena registered, mech landed, first shaders compiled.
	Step(TEXT("warmup"), 30.f, [this](float, float T) { return T > 3.f && Mech() != nullptr; });

	if (Want(TEXT("latency"))) AddLatency();
	if (Want(TEXT("sequence")))
	{
		for (int32 Run = 1; Run <= 3; ++Run)
		{
			AddSequence(Run);
		}
		AddSequenceCompare();
	}
	if (Want(TEXT("collision"))) AddCollision();
	if (Want(TEXT("cameras"))) AddCameras();
	if (Want(TEXT("perf"))) AddPerf();

	bStarted = true;
	UE_LOG(LogTemper, Display, TEXT("SCENARIO start: %s (%d steps)"), *Spec, Steps.Num());
}

void ATemperScenarioRunner::EndPlay(const EEndPlayReason::Type Reason)
{
	if (EndFrameRTHandle.IsValid())
	{
		FCoreDelegates::OnEndFrame.Remove(EndFrameRTHandle);
		FlushRenderingCommands();
	}
	if (ErrorCounter)
	{
		GLog->RemoveOutputDevice(ErrorCounter.Get());
	}
	Super::EndPlay(Reason);
}

void ATemperScenarioRunner::Step(const FString& Name, float Timeout, TFunction<bool(float, float)> InTick)
{
	Steps.Add({ Name, Timeout, MoveTemp(InTick) });
}

void ATemperScenarioRunner::Check(const FString& Name, bool bPass, const FString& Detail)
{
	(bPass ? Passed : Failed)++;
	const FString Line = FString::Printf(TEXT("SCENARIO %s %s: %s"), *Name, bPass ? TEXT("PASS") : TEXT("FAIL"), *Detail);
	Summary.Add(Line);
	UE_LOG(LogTemper, Display, TEXT("%s"), *Line);
}

void ATemperScenarioRunner::Shot(const FString& Name)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("TemperRuns") / (Name + TEXT(".png"));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
}

void ATemperScenarioRunner::WriteFile(const FString& Name, const TArray<FString>& Lines)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("TemperRuns") / Name;
	FFileHelper::SaveStringArrayToFile(Lines, *Path);
	UE_LOG(LogTemper, Display, TEXT("SCENARIO wrote %s"), *Path);
}

void ATemperScenarioRunner::Press(const FKey& Key)
{
	if (PC())
	{
		PC()->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		Held.Add(Key);
	}
}

void ATemperScenarioRunner::Release(const FKey& Key)
{
	if (PC())
	{
		PC()->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
		Held.Remove(Key);
	}
}

void ATemperScenarioRunner::ReleaseAll()
{
	const TArray<FKey> Keys = Held.Array();
	for (const FKey& K : Keys)
	{
		Release(K);
	}
}

void ATemperScenarioRunner::Mouse(float DeltaX)
{
	if (PC())
	{
		PC()->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, DeltaX, 1));
		bMouseThisFrame = DeltaX != 0.f;
	}
}

void ATemperScenarioRunner::ResetMech(const FVector& Location, float Yaw)
{
	ReleaseAll();
	ABastionMech* M = Mech();
	M->SetActorLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	M->Move()->Velocity = FVector::ZeroVector;
	M->Move()->SetMovementMode(MOVE_Falling);
	M->ResetRig();
	PC()->SetControlRotation(FRotator(-8.f, Yaw, 0.f));
}

bool ATemperScenarioRunner::Penetrating() const
{
	const ABastionMech* M = Mech();
	const UCapsuleComponent* Cap = M->GetCapsuleComponent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TemperPenetration), false, M);
	return GetWorld()->OverlapBlockingTestByChannel(M->GetActorLocation(), M->GetActorQuat(), ECC_Pawn,
		FCollisionShape::MakeCapsule(Cap->GetScaledCapsuleRadius() - 3.f, Cap->GetScaledCapsuleHalfHeight() - 3.f), Params);
}

void ATemperScenarioRunner::LogFrame(const FString& Phase)
{
	const ABastionMech* M = Mech();
	const FVector P = M->GetActorLocation();
	const FVector V = M->GetVelocity();
	Cur.MaxDt = FMath::Max(Cur.MaxDt, GetWorld()->GetDeltaSeconds());
	const FString KeyList = FString::JoinBy(Held, TEXT("|"), [](const FKey& K) { return K.ToString(); });
	RunLines.Add(FString::Printf(TEXT("%.4f,%llu,%.4f,%s,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%d"),
		Clock, (unsigned long long)GFrameNumber, GetWorld()->GetDeltaSeconds(), *Phase, KeyList.IsEmpty() ? TEXT("-") : *KeyList,
		P.X, P.Y, P.Z, V.X, V.Y, V.Z, FVector(V.X, V.Y, 0.f).Size(),
		M->GetHullYaw(), PC()->GetControlRotation().Yaw, M->Move()->IsFalling() ? 1 : 0));
}

// ---------------------------------------------------------------------------------------------
// Latency: frames from the input event reaching the controller to the mech visibly responding.

void ATemperScenarioRunner::AddLatency()
{
	const TCHAR* Probes[] = { TEXT("move_W"), TEXT("jet_Space"), TEXT("look_Mouse"), TEXT("camera_V"), TEXT("os_W") };
	for (const TCHAR* P : Probes)
	{
		const int32 Reps = FString(P) == TEXT("os_W") ? 5 : 5;
		for (int32 i = 0; i < Reps; ++i)
		{
			AddLatencyProbe(P, i);
		}
	}

	Step(TEXT("latency_report"), 2.f, [this](float, float T)
	{
		if (T < 0.5f)
		{
			return false; // let the render thread report the last frames
		}
		FlushRenderingCommands();
		TArray<FString> Lines;
		Lines.Add(TEXT("probe,rep,inject_frame,sim_frames,torso_frames,view_frames,visible_frame,os_to_controller_frames,inject_to_rt_frame_end_ms,body_1cm_frames,body_1cm_ms"));
		TMap<FString, TArray<float>> Ms;
		TMap<FString, TArray<float>> BodyMs;
		TMap<FString, int32> WorstFrames;
		int32 Rep = 0;
		FString LastProbe;
		for (FLatencySample& S : Latency)
		{
			Rep = S.Probe == LastProbe ? Rep + 1 : 0;
			LastProbe = S.Probe;
			{
				// The inject frame's end is the first frame end queued after the input; the
				// frame that showed the response is Visible (plus OS delivery) frames after it.
				FScopeLock Lock(&RTLock);
				const int32 Visible0 = S.Probe == TEXT("look_Mouse") || S.Probe == TEXT("camera_V") ? S.ViewFrames : S.TorsoFrames;
				const int32 Offset = Visible0 + FMath::Max(0, S.OsToControllerFrames);
				for (int32 i = 0; Visible0 >= 0 && i < RTFrameEnds.Num(); ++i)
				{
					if (RTFrameEnds[i].Key > S.InjectCycles)
					{
						if (RTFrameEnds.IsValidIndex(i + Offset))
						{
							S.EndToEndMs = FPlatformTime::ToMilliseconds64(RTFrameEnds[i + Offset].Value - S.InjectCycles);
						}
						break;
					}
				}
			}
			const int32 Visible = S.Probe == TEXT("look_Mouse") || S.Probe == TEXT("camera_V") ? S.ViewFrames : S.TorsoFrames;
			if (Visible != -2) // -2: OS probe skipped (window not foreground)
			{
				int32& W = WorstFrames.FindOrAdd(S.Probe, 0);
				W = FMath::Max(W, Visible < 0 ? 999 : Visible);
			}
			if (S.EndToEndMs >= 0.0)
			{
				Ms.FindOrAdd(S.Probe).Add(S.EndToEndMs);
			}
			Lines.Add(FString::Printf(TEXT("%s,%d,%llu,%d,%d,%d,%llu,%d,%.2f,%d,%.1f"), *S.Probe, Rep, (unsigned long long)S.InjectFrame,
				S.SimFrames, S.TorsoFrames, S.ViewFrames, (unsigned long long)S.VisibleFrame, S.OsToControllerFrames, S.EndToEndMs, S.BodyFrames, S.BodyMs));
			if (S.BodyMs >= 0.f)
			{
				BodyMs.FindOrAdd(S.Probe).Add(S.BodyMs);
			}
		}
		WriteFile(TEXT("latency.csv"), Lines);

		for (const TPair<FString, int32>& W : WorstFrames)
		{
			const TArray<float>* Samples = Ms.Find(W.Key);
			const FString MsText = Samples && Samples->Num()
				? FString::Printf(TEXT("input->render-thread frame end mean %.2f ms, max %.2f ms (n=%d)"), Mean(*Samples), FMath::Max(*Samples), Samples->Num())
				: TEXT("no render timing");
			const TArray<float>* Body = BodyMs.Find(W.Key);
			const FString BodyText = Body && Body->Num() ? FString::Printf(TEXT("; capsule moved 1cm after mean %.0f ms"), Mean(*Body)) : FString();
			Check(FString::Printf(TEXT("latency_%s"), *W.Key), W.Value == 0,
				FString::Printf(TEXT("worst %d extra frames from controller receipt to visible response; %s%s"), W.Value, *MsText, *BodyText));
		}
		return true;
	});
}

void ATemperScenarioRunner::AddLatencyProbe(const FString& ProbeName, int32 Index)
{
	Step(FString::Printf(TEXT("latency_%s_%d_reset"), *ProbeName, Index), 5.f, [this, ProbeName](float, float T)
	{
		if (bFirstTickOfStep)
		{
			ResetMech(ATemperArena::PlayerSpawn().GetLocation(), 0.f);
			PC()->SetMechCamera(ProbeName == TEXT("camera_V") ? ETemperCameraMode::FirstPerson : ETemperCameraMode::FirstPerson);
		}
		return T > 1.5f && Mech()->Move()->IsMovingOnGround() && Mech()->GetVelocity().Size() < 1.f;
	});

	Step(FString::Printf(TEXT("latency_%s_%d"), *ProbeName, Index), 3.f, [this, ProbeName](float, float T)
	{
		ABastionMech* M = Mech();
		const FMinimalViewInfo& View = PC()->PlayerCameraManager->GetCameraCacheView();
		if (bFirstTickOfStep)
		{
			// State at the end of last frame = what is on screen before the input.
			BaseTorso = M->GetTorsoTransform();
			BaseVel = M->GetVelocity();
			BaseLoc = M->GetActorLocation();
			BaseViewLoc = View.Location;
			BaseViewRot = View.Rotation;
			Probe = FLatencySample();
			Probe.Probe = ProbeName;
			bFlag = false;

			// Inject now: this runner ticks before the controller, like OS input pumped at frame start.
			Probe.InjectFrame = GFrameNumber;
			Probe.InjectCycles = FPlatformTime::Cycles64();
			if (ProbeName == TEXT("move_W")) Press(EKeys::W);
			else if (ProbeName == TEXT("jet_Space")) Press(EKeys::SpaceBar);
			else if (ProbeName == TEXT("look_Mouse")) Mouse(40.f);
			else if (ProbeName == TEXT("camera_V")) Press(EKeys::V);
			else if (ProbeName == TEXT("os_W"))
			{
#if PLATFORM_WINDOWS
				// Real OS input: a WM_KEYDOWN through the message pump, Slate and the viewport.
				TSharedPtr<SWindow> Window = GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
				HWND Hwnd = Window.IsValid() && Window->GetNativeWindow().IsValid() ? (HWND)Window->GetNativeWindow()->GetOSWindowHandle() : nullptr;
				// Never steal focus from whoever is using the desktop: if we aren't foreground, skip.
				if (Hwnd && ::GetForegroundWindow() == Hwnd)
				{
					PC()->LastKeyFrame = 0;
					PC()->AllowRealKey = EKeys::W;
					INPUT In = {};
					In.type = INPUT_KEYBOARD;
					In.ki.wVk = 'W';
					In.ki.wScan = (WORD)::MapVirtualKey('W', MAPVK_VK_TO_VSC);
					::SendInput(1, &In, sizeof(INPUT));
					bFlag = true;
				}
				else if (Hwnd)
				{
					// Not foreground (someone is using the desktop): post the key message straight
					// into the window's queue - same pump/Slate/viewport path, minus the hardware.
					PC()->LastKeyFrame = 0;
					PC()->AllowRealKey = EKeys::W;
					const UINT Scan = ::MapVirtualKey('W', MAPVK_VK_TO_VSC);
					::PostMessageW(Hwnd, WM_KEYDOWN, 'W', 1 | (Scan << 16));
					bFlag = true;
					bPosted = true;
					UE_LOG(LogTemper, Display, TEXT("SCENARIO latency os_W: window not foreground, using PostMessage(WM_KEYDOWN)"));
				}
				else
				{
					Probe.SimFrames = Probe.TorsoFrames = Probe.ViewFrames = -2;
					return true;
				}
#else
				return true;
#endif
			}
			return false;
		}

		if (ProbeName == TEXT("os_W") && Probe.OsToControllerFrames < 0 && PC()->LastKeyFrame != 0)
		{
			// Re-base the frame count on the frame the OS event reached the controller.
			Probe.OsToControllerFrames = int32(PC()->LastKeyFrame - Probe.InjectFrame);
			Probe.InjectFrame = PC()->LastKeyFrame;
		}

		// We run before this frame's input/sim, so what we see is the end of the previous frame.
		const int32 Frames = int32(GFrameNumber - 1 - Probe.InjectFrame);
		if (Frames < 0)
		{
			return false;
		}
		const FTransform Torso = M->GetTorsoTransform();
		const bool bSim = !M->GetVelocity().Equals(BaseVel, 0.5f);
		const bool bTorso = FVector::Dist(Torso.GetLocation(), BaseTorso.GetLocation()) >= 1.f
			|| Torso.GetRotation().AngularDistance(BaseTorso.GetRotation()) >= FMath::DegreesToRadians(0.25f);
		const bool bView = FVector::Dist(View.Location, BaseViewLoc) >= 1.f
			|| FQuat(View.Rotation).AngularDistance(FQuat(BaseViewRot)) >= FMath::DegreesToRadians(0.25f);
		if (bSim && Probe.SimFrames < 0) Probe.SimFrames = Frames;
		if (bTorso && Probe.TorsoFrames < 0) Probe.TorsoFrames = Frames;
		if (bView && Probe.ViewFrames < 0) Probe.ViewFrames = Frames;
		if (Probe.BodyFrames < 0 && FVector::Dist(M->GetActorLocation(), BaseLoc) >= 1.f)
		{
			Probe.BodyFrames = Frames;
			Probe.BodyMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Probe.InjectCycles);
		}

		const bool bViewProbe = ProbeName == TEXT("look_Mouse") || ProbeName == TEXT("camera_V");
		const int32 Visible = bViewProbe ? Probe.ViewFrames : Probe.TorsoFrames;
		if (Visible >= 0 && Probe.VisibleFrame == 0)
		{
			Probe.VisibleFrame = Probe.InjectFrame + Visible;
		}
		// Movement probes also wait for the capsule itself to move 1 cm (reported, not gated).
		const bool bDone = (Visible >= 0 && (bViewProbe || (Probe.SimFrames >= 0 && Probe.BodyFrames >= 0))) || T > 1.5f;
		if (bDone)
		{
			ReleaseAll();
#if PLATFORM_WINDOWS
			if (ProbeName == TEXT("os_W") && bFlag && bPosted)
			{
				TSharedPtr<SWindow> Window = GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
				if (Window.IsValid() && Window->GetNativeWindow().IsValid())
				{
					const UINT Scan = ::MapVirtualKey('W', MAPVK_VK_TO_VSC);
					::PostMessageW((HWND)Window->GetNativeWindow()->GetOSWindowHandle(), WM_KEYUP, 'W', 1 | (Scan << 16) | (1u << 30) | (1u << 31));
				}
				bPosted = false;
			}
			else if (ProbeName == TEXT("os_W") && bFlag)
			{
				INPUT In = {};
				In.type = INPUT_KEYBOARD;
				In.ki.wVk = 'W';
				In.ki.wScan = (WORD)::MapVirtualKey('W', MAPVK_VK_TO_VSC);
				In.ki.dwFlags = KEYEVENTF_KEYUP;
				::SendInput(1, &In, sizeof(INPUT));
			}
#endif
			if (ProbeName == TEXT("os_W"))
			{
				// Keep the real W allowed for the key-up, then lock out hardware input again.
				FTimerHandle Handle;
				GetWorldTimerManager().SetTimer(Handle, [this]() { if (PC()) PC()->AllowRealKey = FKey(); }, 0.3f, false);
			}
			if (ProbeName == TEXT("camera_V"))
			{
				PC()->SetMechCamera(ETemperCameraMode::FirstPerson);
			}
			UE_LOG(LogTemper, Display, TEXT("LATENCY %s frame=%llu sim=%d torso=%d view=%d os=%d"), *ProbeName,
				(unsigned long long)Probe.InjectFrame, Probe.SimFrames, Probe.TorsoFrames, Probe.ViewFrames, Probe.OsToControllerFrames);
			Latency.Add(Probe);
			return true;
		}
		return false;
	});
}

// ---------------------------------------------------------------------------------------------
// The scripted movement sequence: walk 10 m, sprint 10 m, thruster burst, 180 degree turn.

void ATemperScenarioRunner::AddSequence(int32 Run)
{
	const FString R = FString::Printf(TEXT("run%d"), Run);

	Step(R + TEXT("_reset"), 6.f, [this](float, float T)
	{
		if (bFirstTickOfStep)
		{
			ResetMech(ATemperArena::PlayerSpawn().GetLocation(), 0.f);
			PC()->SetMechCamera(ETemperCameraMode::ThirdPerson);
			RunLines.Reset();
			RunLines.Add(TEXT("t,frame,dt,phase,keys,x,y,z,vx,vy,vz,speed,hull_yaw,control_yaw,falling"));
			Cur = FRunMetrics();
		}
		return T > 2.f && Mech()->Move()->IsMovingOnGround() && Mech()->GetVelocity().Size() < 1.f;
	});

	// Walk forward 10 m from a standstill, then release.
	Step(R + TEXT("_walk"), 8.f, [this](float, float T)
	{
		ABastionMech* M = Mech();
		if (bFirstTickOfStep)
		{
			Mark = M->GetActorLocation();
			Press(EKeys::W);
		}
		LogFrame(TEXT("walk"));
		const float Speed = M->GetVelocity().Size2D();
		Cur.WalkPeak = FMath::Max(Cur.WalkPeak, Speed);
		if (Cur.WalkTo95 < 0.f && Speed >= 0.95f * M->Move()->WalkSpeed)
		{
			Cur.WalkTo95 = T;
		}
		if (M->GetActorLocation().X - Mark.X >= SequenceLeg)
		{
			Cur.WalkTime = T;
			Mark2 = M->GetActorLocation();
			Release(EKeys::W);
			return true;
		}
		return false;
	});
	Step(R + TEXT("_walk_stop"), 5.f, [this](float, float T)
	{
		ABastionMech* M = Mech();
		if (bFirstTickOfStep)
		{
			MarkT = 0.f; // stop distance is measured from Mark2, the position where the key was released
		}
		LogFrame(TEXT("walk_stop"));
		if (M->GetVelocity().Size2D() < 1.f)
		{
			Cur.WalkStopDist = FVector::Dist2D(M->GetActorLocation(), Mark2);
			Cur.WalkStopTime = T;
			return true;
		}
		return false;
	});

	// Sprint 10 m from a standstill.
	Step(R + TEXT("_sprint"), 8.f, [this](float, float T)
	{
		ABastionMech* M = Mech();
		if (bFirstTickOfStep)
		{
			Mark = M->GetActorLocation();
			Press(EKeys::LeftShift);
			Press(EKeys::W);
		}
		LogFrame(TEXT("sprint"));
		Cur.SprintPeak = FMath::Max(Cur.SprintPeak, M->GetVelocity().Size2D());
		if (M->GetActorLocation().X - Mark.X >= SequenceLeg)
		{
			Cur.SprintTime = T;
			Mark2 = M->GetActorLocation();
			Release(EKeys::W);
			Release(EKeys::LeftShift);
			return true;
		}
		return false;
	});
	Step(R + TEXT("_sprint_stop"), 5.f, [this](float, float T)
	{
		ABastionMech* M = Mech();
		if (bFirstTickOfStep)
		{
			MarkT = 0.f;
		}
		LogFrame(TEXT("sprint_stop"));
		if (M->GetVelocity().Size2D() < 1.f)
		{
			Cur.SprintStopDist = FVector::Dist2D(M->GetActorLocation(), Mark2);
			Cur.SprintStopTime = T;
			return true;
		}
		return false;
	});

	// Thruster burst from a standstill with W held (a forward repositioning hop).
	Step(R + TEXT("_jet"), 6.f, [this](float, float T)
	{
		ABastionMech* M = Mech();
		if (bFirstTickOfStep)
		{
			Mark = M->GetActorLocation();
			MarkT = -1.f;
			bFlag = false;
			Counter = M->Events().Num();
			Press(EKeys::W);
			Press(EKeys::SpaceBar);
			return false;
		}
		if (Held.Contains(EKeys::SpaceBar))
		{
			Release(EKeys::SpaceBar);
		}
		LogFrame(TEXT("jet"));
		Cur.JetPeakHeight = FMath::Max(Cur.JetPeakHeight, M->GetActorLocation().Z - Mark.Z);
		if (M->Move()->IsFalling() && MarkT < 0.f)
		{
			MarkT = T;
		}
		for (int32 i = Counter; i < M->Events().Num(); ++i)
		{
			if (M->Events()[i].Name == TEXT("land") && MarkT >= 0.f)
			{
				Cur.JetAirTime = T - MarkT;
				Cur.JetImpact = M->Events()[i].Value;
				Cur.JetDistance = FVector::Dist2D(M->GetActorLocation(), Mark);
				Release(EKeys::W);
				return true;
			}
		}
		Counter = M->Events().Num();
		return false;
	});
	Step(R + TEXT("_jet_stop"), 5.f, [this](float, float T)
	{
		ABastionMech* M = Mech();
		if (bFirstTickOfStep)
		{
			Mark = M->GetActorLocation();
		}
		LogFrame(TEXT("jet_stop"));
		if (M->GetVelocity().Size2D() < 1.f && T > 0.1f)
		{
			Cur.JetStopDist = FVector::Dist2D(M->GetActorLocation(), Mark);
			return true;
		}
		return false;
	});

	// 180 degree turn: a mouse flick over 6 frames; the view turns 1:1, the hull follows.
	Step(R + TEXT("_turn"), 6.f, [this](float, float T)
	{
		ABastionMech* M = Mech();
		const float Yaw = PC()->GetControlRotation().Yaw;
		if (bFirstTickOfStep)
		{
			TargetYaw = FRotator::NormalizeAxis(Yaw + 180.f);
			Counter = 0;
			MarkT = -1.f;
			MarkF = 0.f;
			Mark.X = Yaw;
		}
		else if (Counter == 1 && MarkF > 0.f)
		{
			// Calibrate from what the first mouse frame actually did (deg per count, end to end).
			UE_LOG(LogTemper, Display, TEXT("SCENARIO mouse: %.5f deg/count measured"), FMath::Abs(FMath::FindDeltaAngleDegrees(Mark.X, Yaw)) / MarkF);
			Mark.Y = FMath::Abs(FMath::FindDeltaAngleDegrees(Mark.X, Yaw)) / MarkF;
		}
		// A fast flick: up to 30 degrees per frame of mouse travel until the view is around.
		const float Remaining = FMath::FindDeltaAngleDegrees(Yaw, TargetYaw);
		if (MarkT < 0.f && FMath::Abs(Remaining) >= 0.5f && Counter < 40)
		{
			const float DegPerCount = Counter == 0 ? 0.01f : FMath::Max(Mark.Y, 1e-4f);
			const float Deg = Counter == 0 ? 2.f : FMath::Min(30.f, FMath::Abs(Remaining));
			MarkF = Deg / DegPerCount;
			Mouse(FMath::Sign(Remaining == 0.f ? 1.f : Remaining) * MarkF);
			++Counter;
		}
		LogFrame(TEXT("turn"));
		if (MarkT < 0.f && FMath::Abs(FMath::FindDeltaAngleDegrees(PC()->GetControlRotation().Yaw, TargetYaw)) < 0.5f)
		{
			MarkT = T;
			Cur.TurnViewTime = T;
		}
		if (MarkT >= 0.f && FMath::Abs(FMath::FindDeltaAngleDegrees(M->GetHullYaw(), TargetYaw)) < 1.f)
		{
			Cur.TurnHullTime = T;
			return true;
		}
		return false;
	});
	Step(R + TEXT("_done"), 2.f, [this, Run](float, float T)
	{
		LogFrame(TEXT("settle"));
		if (T < 0.5f)
		{
			return false;
		}
		const FRunMetrics& M = Cur;
		RunLines.Insert(FString::Printf(TEXT("# run %d summary: walk10m %.3fs (95%% speed at %.3fs, peak %.1f cm/s) stop %.1fcm/%.3fs | sprint10m %.3fs (peak %.1f) stop %.1fcm/%.3fs | jet peak %.1fcm air %.3fs dist %.1fcm impact %.1fcm/s stop %.1fcm | turn180 view %.3fs hull %.3fs"),
			Run, M.WalkTime, M.WalkTo95, M.WalkPeak, M.WalkStopDist, M.WalkStopTime, M.SprintTime, M.SprintPeak, M.SprintStopDist, M.SprintStopTime,
			M.JetPeakHeight, M.JetAirTime, M.JetDistance, M.JetImpact, M.JetStopDist, M.TurnViewTime, M.TurnHullTime), 0);
		WriteFile(FString::Printf(TEXT("movement_run%d.csv"), Run), RunLines);
		UE_LOG(LogTemper, Display, TEXT("%s"), *RunLines[0]);
		Runs.Add(Cur);
		return true;
	});
}

void ATemperScenarioRunner::AddSequenceCompare()
{
	Step(TEXT("sequence_compare"), 1.f, [this](float, float)
	{
		if (Runs.Num() != 3)
		{
			Check(TEXT("sequence_3x"), false, FString::Printf(TEXT("only %d of 3 runs completed"), Runs.Num()));
			return true;
		}
		bool bComplete = true;
		TArray<float> Walk, Sprint, WalkStop, SprintStop, JetH, JetD, Hull;
		for (const FRunMetrics& M : Runs)
		{
			bComplete &= M.WalkTime > 0.f && M.WalkStopDist >= 0.f && M.SprintTime > 0.f && M.SprintStopDist >= 0.f
				&& M.JetAirTime > 0.f && M.TurnHullTime > 0.f;
			Walk.Add(M.WalkTime); Sprint.Add(M.SprintTime); WalkStop.Add(M.WalkStopDist);
			SprintStop.Add(M.SprintStopDist); JetH.Add(M.JetPeakHeight); JetD.Add(M.JetDistance); Hull.Add(M.TurnHullTime);
		}
		Check(TEXT("sequence_complete_3x"), bComplete, TEXT("all four legs finished in each of 3 runs"));
		// Variable frame rate means runs are not bit-identical: events land on frame boundaries.
		// Allow two of the worst frame seen in time, and one worst frame of travel in distance.
		float Q = 0.f;
		for (const FRunMetrics& M : Runs)
		{
			Q = FMath::Max(Q, M.MaxDt);
		}
		const float TimeTol = FMath::Max(0.05f, 2.f * Q);
		const float DistTol = FMath::Max(10.f, Runs[0].SprintPeak * Q);
		const bool bConsistent = Spread(Walk) <= TimeTol && Spread(Sprint) <= TimeTol && Spread(WalkStop) <= DistTol
			&& Spread(SprintStop) <= DistTol && Spread(JetH) <= DistTol && Spread(JetD) <= 2.f * DistTol && Spread(Hull) <= TimeTol;
		Check(TEXT("sequence_consistent_3x"), bConsistent, FString::Printf(
			TEXT("spread walk %.3fs sprint %.3fs walkstop %.1fcm sprintstop %.1fcm jetH %.1fcm jetD %.1fcm hull %.3fs (worst frame %.1f ms -> tol %.3fs / %.1fcm)"),
			Spread(Walk), Spread(Sprint), Spread(WalkStop), Spread(SprintStop), Spread(JetH), Spread(JetD), Spread(Hull), Q * 1000.f, TimeTol, DistTol));

		// Mass: ramps that take real time and distance, but not sluggish ones.
		const FRunMetrics& A = Runs[0];
		Check(TEXT("mass_accel_ramp"), A.WalkTo95 > 1.0f && A.WalkTo95 < 2.0f,
			FString::Printf(TEXT("0->95%% walk speed in %.3fs (target ~1.5s)"), A.WalkTo95));
		Check(TEXT("mass_decel"), A.WalkStopDist > 80.f && A.WalkStopDist < 300.f && A.SprintStopDist > A.WalkStopDist,
			FString::Printf(TEXT("walk stop %.0fcm in %.2fs, sprint stop %.0fcm in %.2fs"), A.WalkStopDist, A.WalkStopTime, A.SprintStopDist, A.SprintStopTime));
		Check(TEXT("jet_hop_not_flight"), A.JetPeakHeight > 120.f && A.JetPeakHeight < 400.f && A.JetAirTime < 1.6f,
			FString::Printf(TEXT("peak %.0fcm, air %.2fs, distance %.0fcm, landing impact %.0fcm/s"), A.JetPeakHeight, A.JetAirTime, A.JetDistance, A.JetImpact));
		return true;
	});
}

// ---------------------------------------------------------------------------------------------
// Collision: walls, buildings, the perimeter, steps and drops. Every frame: no penetration,
// never outside the arena.

void ATemperScenarioRunner::AddCollision()
{
	auto Guard = [this]()
	{
		if (Penetrating())
		{
			++Penetrations;
		}
		if (!ATemperArena::IsInside(Mech()->GetActorLocation(), 0.f))
		{
			++Escapes;
		}
	};

	// 1. Sprint straight into the west perimeter wall, then grind along it into the corner with jets.
	Step(TEXT("coll_wall_reset"), 5.f, [this](float, float T)
	{
		if (bFirstTickOfStep)
		{
			ResetMech(FVector(-3000.f, 0.f, 240.f), 180.f);
			PC()->SetMechCamera(ETemperCameraMode::ThirdPerson);
			Penetrations = Escapes = 0;
		}
		return T > 1.5f && Mech()->Move()->IsMovingOnGround();
	});
	Step(TEXT("coll_wall_ram"), 12.f, [this, Guard](float, float T)
	{
		if (bFirstTickOfStep)
		{
			Press(EKeys::LeftShift);
			Press(EKeys::W);
			MarkF = 0.f;
		}
		Guard();
		if (T > 4.5f && !Held.Contains(EKeys::A))
		{
			Press(EKeys::A); // slide along the wall toward the south-west corner
		}
		// Jet spam into the wall and corner.
		if (T > 4.5f && T - MarkF > 0.35f)
		{
			MarkF = T;
			Press(EKeys::SpaceBar);
		}
		else if (Held.Contains(EKeys::SpaceBar))
		{
			Release(EKeys::SpaceBar);
		}
		if (T > 11.f)
		{
			const FVector P = Mech()->GetActorLocation();
			ReleaseAll();
			Check(TEXT("collision_perimeter"), Penetrations == 0 && Escapes == 0,
				FString::Printf(TEXT("sprint+jet into west wall/corner 11s: penetrating frames %d, outside-arena frames %d, final (%.0f, %.0f, %.0f), wall face x=%.0f"),
					Penetrations, Escapes, P.X, P.Y, P.Z, -ATemperArena::HalfSize));
			Shot(TEXT("collision_wall"));
			return true;
		}
		return false;
	});

	// 2. Ram a building at sprint and try to hop up its face.
	Step(TEXT("coll_bldg_reset"), 5.f, [this](float, float T)
	{
		if (bFirstTickOfStep)
		{
			ResetMech(FVector(2300.f, 1200.f, 240.f), 0.f);
			Penetrations = Escapes = 0;
		}
		return T > 1.5f && Mech()->Move()->IsMovingOnGround();
	});
	Step(TEXT("coll_bldg_ram"), 8.f, [this, Guard](float, float T)
	{
		if (bFirstTickOfStep)
		{
			Press(EKeys::LeftShift);
			Press(EKeys::W);
			MarkF = 0.f;
		}
		Guard();
		if (T > 3.f && T - MarkF > 0.4f)
		{
			MarkF = T;
			Press(EKeys::SpaceBar);
		}
		else if (Held.Contains(EKeys::SpaceBar))
		{
			Release(EKeys::SpaceBar);
		}
		if (T > 6.f)
		{
			const FVector P = Mech()->GetActorLocation();
			ReleaseAll();
			// East building west face at x = 3400; capsule radius 165.
			Check(TEXT("collision_building"), Penetrations == 0 && P.X <= 3400.f - 160.f,
				FString::Printf(TEXT("sprint+jet into east building 6s: penetrating frames %d, final x %.0f (face 3400, capsule r165)"), Penetrations, P.X));
			return true;
		}
		return false;
	});

	// 3. Hop up the 1.5 m step onto the 3 m loading dock.
	Step(TEXT("coll_dock_reset"), 5.f, [this](float, float T)
	{
		if (bFirstTickOfStep)
		{
			ResetMech(FVector(-700.f, -900.f, 240.f), 0.f);
			Penetrations = Escapes = 0;
		}
		return T > 1.5f && Mech()->Move()->IsMovingOnGround();
	});
	Step(TEXT("coll_dock_climb"), 10.f, [this, Guard](float, float T)
	{
		ABastionMech* M = Mech();
		if (bFirstTickOfStep)
		{
			Press(EKeys::W);
			Counter = 0;
		}
		Guard();
		// Hop whenever blocked by a ledge (speed collapsed while pushing forward).
		const bool bBlocked = T > 0.6f && M->Move()->IsMovingOnGround() && M->GetVelocity().Size2D() < 150.f && M->GetJetCooldownLeft() <= 0.f;
		if (bBlocked && !Held.Contains(EKeys::SpaceBar))
		{
			Press(EKeys::SpaceBar);
			++Counter;
		}
		else if (Held.Contains(EKeys::SpaceBar))
		{
			Release(EKeys::SpaceBar);
		}
		const float GroundZ = M->GetGroundLocation().Z;
		if ((GroundZ > 290.f && M->Move()->IsMovingOnGround() && M->GetActorLocation().X > 900.f) || T > 9.f)
		{
			ReleaseAll();
			Check(TEXT("collision_dock_climb"), GroundZ > 290.f && Penetrations == 0,
				FString::Printf(TEXT("ground z %.0f after %d hops in %.1fs (dock top 300), penetrating frames %d"), GroundZ, Counter, T, Penetrations));
			Shot(TEXT("collision_dock"));
			return true;
		}
		return false;
	});

	// 4. Walk off the dock's east edge: a 3 m drop should land hard.
	Step(TEXT("coll_drop"), 8.f, [this, Guard](float, float T)
	{
		ABastionMech* M = Mech();
		if (bFirstTickOfStep)
		{
			Counter = M->Events().Num();
			Press(EKeys::W);
		}
		Guard();
		for (int32 i = Counter; i < M->Events().Num(); ++i)
		{
			if (M->Events()[i].Name == TEXT("land") && T > 0.3f)
			{
				ReleaseAll();
				const float Impact = M->Events()[i].Value;
				Check(TEXT("landing_impact"), Impact > M->Move()->LandSoftSpeed && Penetrations == 0,
					FString::Printf(TEXT("3m drop landed at %.0f cm/s (hard above %.0f): pelvis slam + camera shake + speed penalty"), Impact, M->Move()->LandSoftSpeed));
				return true;
			}
		}
		Counter = M->Events().Num();
		if (T > 7.5f)
		{
			ReleaseAll();
			Check(TEXT("landing_impact"), false, TEXT("never dropped off the dock"));
			return true;
		}
		return false;
	});
}

// ---------------------------------------------------------------------------------------------
// Cameras: cycle V through all modes several times; check the active view each time.

void ATemperScenarioRunner::AddCameras()
{
	Step(TEXT("cam_reset"), 5.f, [this](float, float T)
	{
		if (bFirstTickOfStep)
		{
			ResetMech(ATemperArena::PlayerSpawn().GetLocation() + FVector(800.f, 0.f, 0.f), 20.f);
			PC()->SetMechCamera(ETemperCameraMode::FirstPerson);
			CameraModesSeen.Reset();
			CameraFailures = 0;
			ErrorBaseline = static_cast<FErrorCounter*>(ErrorCounter.Get())->Errors;
			LastSlotPressed = 0;
		}
		return T > 1.5f;
	});
	Step(TEXT("cam_cycle"), 20.f, [this](float, float T)
	{
		ATemperPlayerController* P = PC();
		const int32 Index = FMath::FloorToInt(T / 0.6f);
		if (Index > 9)
		{
			ReleaseAll();
			const int32 NewErrors = static_cast<FErrorCounter*>(ErrorCounter.Get())->Errors - ErrorBaseline;
			FString Seq;
			for (uint8 Mode : CameraModesSeen)
			{
				Seq += Mode == 0 ? TEXT("1P ") : Mode == 1 ? TEXT("3P ") : TEXT("OBS ");
			}
			Check(TEXT("camera_modes"), CameraFailures == 0 && NewErrors == 0 && Seq == TEXT("3P OBS 1P 3P OBS 1P 3P OBS 1P "),
				FString::Printf(TEXT("9 toggles via V: %s; wrong-view checks %d; log errors %d"), *Seq, CameraFailures, NewErrors));
			return true;
		}
		// Press V at the start of each 0.6 s slot, check and screenshot mid-slot.
		const float Slot = T - Index * 0.6f;
		if (Index >= 1 && LastSlotPressed != Index)
		{
			Press(EKeys::V);
			LastSlotPressed = Index;
		}
		else if (Held.Contains(EKeys::V))
		{
			Release(EKeys::V);
		}
		if (Index >= 1 && Slot > 0.3f && CameraModesSeen.Num() < Index)
		{
			const ETemperCameraMode Mode = P->GetCameraMode();
			CameraModesSeen.Add(static_cast<uint8>(Mode));
			AActor* Target = P->GetViewTarget();
			const bool bOk = Mode == ETemperCameraMode::Observe ? Target == P->GetObserveCamera() : Target == Mech();
			if (!bOk)
			{
				++CameraFailures;
			}
			if (Index <= 3)
			{
				Shot(FString::Printf(TEXT("camera_%s"), Mode == ETemperCameraMode::FirstPerson ? TEXT("first_person") : Mode == ETemperCameraMode::ThirdPerson ? TEXT("third_person") : TEXT("observe")));
			}
		}
		return false;
	});
	Step(TEXT("cam_observe_fly"), 5.f, [this](float, float T)
	{
		ATemperPlayerController* P = PC();
		if (bFirstTickOfStep)
		{
			P->SetMechCamera(ETemperCameraMode::Observe);
			Mark = P->GetObserveCamera()->GetActorLocation();
			Mark2 = Mech()->GetActorLocation();
			Press(EKeys::W);
			Press(EKeys::E);
		}
		if (T > 1.5f)
		{
			ReleaseAll();
			const float CamMoved = FVector::Dist(P->GetObserveCamera()->GetActorLocation(), Mark);
			const float MechMoved = FVector::Dist2D(Mech()->GetActorLocation(), Mark2);
			Check(TEXT("camera_observe_freefly"), CamMoved > 1000.f && MechMoved < 5.f,
				FString::Printf(TEXT("observe cam flew %.0fcm in 1.5s while the mech moved %.1fcm"), CamMoved, MechMoved));
			Shot(TEXT("camera_observe_flown"));
			P->SetMechCamera(ETemperCameraMode::ThirdPerson);
			return true;
		}
		return false;
	});
}

// ---------------------------------------------------------------------------------------------
// Perf: 30 s of sprinting, turning and hopping around the yard.

void ATemperScenarioRunner::AddPerf()
{
	Step(TEXT("perf_reset"), 5.f, [this](float, float T)
	{
		if (bFirstTickOfStep)
		{
			ResetMech(ATemperArena::PlayerSpawn().GetLocation(), 0.f);
			PC()->SetMechCamera(ETemperCameraMode::ThirdPerson);
			FrameTimes.Reset();
			GameMs.Reset();
			RenderMs.Reset();
			GpuMs.Reset();
		}
		return T > 1.5f;
	});
	Step(TEXT("perf_drive"), 40.f, [this](float Dt, float T)
	{
		if (bFirstTickOfStep)
		{
			Press(EKeys::LeftShift);
			Press(EKeys::W);
			MarkF = 0.f;
			Counter = 0;
			return false;
		}
		FrameTimes.Add(FApp::GetDeltaTime() * 1000.f);
		GameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		RenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		GpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
		if (Held.Contains(EKeys::SpaceBar))
		{
			Release(EKeys::SpaceBar);
		}
		// Every 1.2 s swing the view; every 2.5 s hop.
		if (FMath::FloorToInt(T / 1.2f) != Counter)
		{
			Counter = FMath::FloorToInt(T / 1.2f);
			MarkF = 6.f;
		}
		if (MarkF > 0.f)
		{
			Mouse((Counter % 3 == 2 ? -25.f : 25.f) / PC()->MouseSensitivity);
			MarkF -= 1.f;
		}
		if (FMath::Fmod(T, 2.5f) < Dt)
		{
			Press(EKeys::SpaceBar);
		}
		if (FMath::Fmod(T, 10.f) < Dt)
		{
			PC()->CycleCamera();
			if (PC()->GetCameraMode() == ETemperCameraMode::Observe)
			{
				PC()->CycleCamera();
			}
		}
		if (T > 30.f)
		{
			ReleaseAll();
			TArray<FString> Lines;
			Lines.Add(TEXT("frame_index,ms"));
			for (int32 i = 0; i < FrameTimes.Num(); ++i)
			{
				Lines.Add(FString::Printf(TEXT("%d,%.3f"), i, FrameTimes[i]));
				if (FrameTimes[i] > 25.f)
				{
					UE_LOG(LogTemper, Display, TEXT("PERF spike frame %d (t~%.2fs): %.2f ms"), i, Mean(FrameTimes) * i / 1000.f, FrameTimes[i]);
				}
			}
			WriteFile(TEXT("perf_frames.csv"), Lines);
			TArray<float> Sorted = FrameTimes;
			Sorted.Sort();
			const float Avg = Mean(FrameTimes);
			const float P99 = Sorted[FMath::Clamp(int32(Sorted.Num() * 0.99f), 0, Sorted.Num() - 1)];
			const float Worst = Sorted.Last();
			int32 Over = 0;
			for (float Ms : FrameTimes)
			{
				Over += Ms > 1000.f / 60.f ? 1 : 0;
			}
			auto Pct = [](TArray<float> V, float P) { V.Sort(); return V.Num() ? V[FMath::Clamp(int32(V.Num() * P), 0, V.Num() - 1)] : 0.f; };
			Check(TEXT("perf_stable"), Avg < 1000.f / 60.f && P99 < 25.f,
				FString::Printf(TEXT("%d frames: avg %.2f ms (%.0f fps), p99 %.2f ms, worst %.2f ms, frames over 16.7 ms: %d (GPU: %s)"),
					FrameTimes.Num(), Avg, 1000.f / Avg, P99, Worst, Over, *GRHIAdapterName));
			// Our own cost per frame, separate from wall-clock stalls caused by other processes.
			Check(TEXT("perf_engine_cost"), Pct(GameMs, 0.99f) < 16.7f && Pct(GpuMs, 0.99f) < 16.7f,
				FString::Printf(TEXT("game thread p50 %.2f / p99 %.2f ms, render thread p50 %.2f / p99 %.2f ms, GPU p50 %.2f / p99 %.2f ms"),
					Pct(GameMs, 0.5f), Pct(GameMs, 0.99f), Pct(RenderMs, 0.5f), Pct(RenderMs, 0.99f), Pct(GpuMs, 0.5f), Pct(GpuMs, 0.99f)));
			return true;
		}
		return false;
	});
}

// ---------------------------------------------------------------------------------------------

void ATemperScenarioRunner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bStarted)
	{
		return;
	}
	// Make the controller (and through it the mech) tick after us, so injected input is
	// consumed this frame exactly like OS input pumped before the world ticks.
	if (ATemperPlayerController* P = PC())
	{
		P->PrimaryActorTick.AddPrerequisite(this, PrimaryActorTick);
		P->bScenarioInputOnly = true;
	}
	Clock += DeltaTime;
	if (StepIndex >= Steps.Num())
	{
		Finish();
		return;
	}
	FStep& S = Steps[StepIndex];
	const bool bMouseLastFrame = bMouseThisFrame;
	bMouseThisFrame = false;
	const bool bDone = S.Tick(DeltaTime, StepTime);
	if (bMouseLastFrame && !bMouseThisFrame)
	{
		// A simulated axis value persists until replaced; a real mouse reports zero when still.
		Mouse(0.f);
	}
	bFirstTickOfStep = false;
	StepTime += DeltaTime;
	if (!bDone && StepTime > S.Timeout)
	{
		ReleaseAll();
		Check(S.Name, false, FString::Printf(TEXT("timed out after %.1fs"), S.Timeout));
	}
	if (bDone || StepTime > S.Timeout)
	{
		++StepIndex;
		StepTime = 0.f;
		bFirstTickOfStep = true;
	}
}

void ATemperScenarioRunner::Finish()
{
	bStarted = false;
	const FErrorCounter* Errors = static_cast<FErrorCounter*>(ErrorCounter.Get());
	Summary.Add(FString::Printf(TEXT("log errors during run: %d"), Errors->Errors));
	for (const FString& E : Errors->First)
	{
		Summary.Add(TEXT("  ") + E);
	}
	Summary.Add(FString::Printf(TEXT("SCENARIO total: %d passed, %d failed"), Passed, Failed));
	WriteFile(TEXT("summary.txt"), Summary);
	UE_LOG(LogTemper, Display, TEXT("SCENARIO total: %d passed, %d failed"), Passed, Failed);
	FPlatformMisc::RequestExit(false);
}
