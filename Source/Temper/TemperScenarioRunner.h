#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TemperScenarioRunner.generated.h"

class ABastionMech;
class ATemperPlayerController;

/**
 * Scripted acceptance checks. Launched with -TemperScenario=all (or a '+' list of:
 * latency, sequence, collision, cameras, perf). Drives the mech through the real input
 * path (simulated key/mouse events into PlayerController::InputKey, injected at the start
 * of the frame like OS input; plus one probe with real Windows SendInput), logs
 * "SCENARIO ... PASS/FAIL" lines, writes per-run logs to Saved/TemperRuns/, and quits.
 */
UCLASS()
class TEMPER_API ATemperScenarioRunner : public AActor
{
	GENERATED_BODY()

public:
	ATemperScenarioRunner();
	void Start(const FString& Spec);
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FStep
	{
		FString Name;
		float Timeout = 10.f;
		TFunction<bool(float /*Dt*/, float /*T*/)> Tick;
	};

	/** Metrics of one scripted movement run. */
	struct FRunMetrics
	{
		float WalkTime = -1.f, WalkTo95 = -1.f, WalkPeak = 0.f, WalkStopDist = -1.f, WalkStopTime = -1.f;
		float SprintTime = -1.f, SprintPeak = 0.f, SprintStopDist = -1.f, SprintStopTime = -1.f;
		float JetPeakHeight = 0.f, JetAirTime = -1.f, JetDistance = -1.f, JetImpact = -1.f, JetStopDist = -1.f;
		float TurnViewTime = -1.f, TurnHullTime = -1.f;
		bool bAllSteps = true;
		float MaxDt = 0.f; // worst frame during the run (s)
	};

	/** One latency probe sample: frames from input reaching the controller to each visible response. */
	struct FLatencySample
	{
		FString Probe;
		uint64 InjectFrame = 0;
		uint64 InjectCycles = 0;
		int32 SimFrames = -1;
		int32 TorsoFrames = -1;
		int32 ViewFrames = -1;
		int32 BodyFrames = -1; // capsule displaced >= 1 cm (the mass itself, not the rig)
		float BodyMs = -1.f;
		uint64 VisibleFrame = 0;
		double EndToEndMs = -1.0;
		int32 OsToControllerFrames = -1;
	};

	void Step(const FString& Name, float Timeout, TFunction<bool(float, float)> InTick);
	void Check(const FString& Name, bool bPass, const FString& Detail);
	void Shot(const FString& Name);
	void Finish();

	void Press(const FKey& Key);
	void Release(const FKey& Key);
	void ReleaseAll();
	void Mouse(float DeltaX);
	void ResetMech(const FVector& Location, float Yaw);
	void LogFrame(const FString& Phase);
	void WriteFile(const FString& Name, const TArray<FString>& Lines);
	bool Penetrating() const;

	void AddLatency();
	void AddLatencyProbe(const FString& Probe, int32 Index);
	void AddSequence(int32 Run);
	void AddSequenceCompare();
	void AddCollision();
	void AddCameras();
	void AddPerf();

	ABastionMech* Mech() const;
	ATemperPlayerController* PC() const;

	TArray<FStep> Steps;
	int32 StepIndex = 0;
	float StepTime = 0.f;
	float Clock = 0.f;
	bool bStarted = false;
	bool bFirstTickOfStep = true;
	int32 Passed = 0;
	int32 Failed = 0;
	TArray<FString> Summary;
	TSet<FKey> Held;
	bool bMouseThisFrame = false;
	bool bPosted = false;
	int32 ErrorBaseline = 0;
	int32 LastSlotPressed = -1;

	// Scratch shared by the steps of the running scenario.
	FVector Mark = FVector::ZeroVector;
	FVector Mark2 = FVector::ZeroVector;
	float MarkT = 0.f;
	float MarkF = 0.f;
	bool bFlag = false;
	int32 Counter = 0;
	float TargetYaw = 0.f;
	TArray<FString> RunLines;
	TArray<FRunMetrics> Runs;
	FRunMetrics Cur;
	TArray<FLatencySample> Latency;
	FLatencySample Probe;
	FVector BaseLoc;
	FTransform BaseTorso;
	FVector BaseVel;
	FVector BaseViewLoc;
	FRotator BaseViewRot;
	int32 Penetrations = 0;
	int32 Escapes = 0;
	TArray<float> FrameTimes;
	TArray<float> GameMs, RenderMs, GpuMs;
	TArray<uint8> CameraModesSeen;
	int32 CameraFailures = 0;

	// Render-thread end-of-frame timestamps, in frame order (written on the render thread).
	FCriticalSection RTLock;
	TArray<TPair<uint64, uint64>> RTFrameEnds; // (queued on game thread, ran on render thread)
	FDelegateHandle EndFrameRTHandle;

	// Error lines seen in the log while scenarios run.
	TUniquePtr<FOutputDevice> ErrorCounter;
};
