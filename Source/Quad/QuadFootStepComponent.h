#pragma once

#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "QuadFootStepComponent.generated.h"

class USkeletalMeshComponent;
class UQuadFootInteractionComponent;

UENUM(BlueprintType)
enum class EQuadFootPhase : uint8
{
	Planted,
	Raising,
	Aiming,
	Lowering,
	PreparingLift,
	Returning
};

UCLASS(ClassGroup=(Quad), meta=(BlueprintSpawnableComponent))
class QUAD_API UQuadFootStepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Quad|Foot Step")
	bool InitializeFootSteps(USkeletalMeshComponent* SkeletalMesh);

	UFUNCTION(BlueprintCallable, Category="Quad|Foot Step")
	void RequestFootSelection(int32 RequestedID);

	UFUNCTION(BlueprintCallable, Category="Quad|Foot Step")
	void SetFootAimInput(FVector2D Input);

	UFUNCTION(BlueprintCallable, Category="Quad|Foot Step")
	void ConfirmFootPlacement();

	UFUNCTION(BlueprintCallable, Category="Quad|Foot Step")
	void AdvanceFootStep(float DeltaSeconds, float LiftClearance, float MoveSpeed, float MinKneeBend, float MaxKneeBend);

	UFUNCTION(BlueprintPure, Category="Quad|Foot Step")
	void GetLegPose(int32 LegID, FTransform& Target, FVector& Contact, FVector& Normal, float& Weight, bool& IsControlling) const;

	UFUNCTION(BlueprintPure, Category="Quad|Foot Step")
	FTransform ResolveFootBodyPose(const FTransform& SupportPose) const;

	UFUNCTION(BlueprintPure, Category="Quad|Foot Step")
	int32 GetActiveLegID() const;

	void ToggleFocusedGrip();

	UFUNCTION(BlueprintPure, Category="Quad|Foot Step")
	TArray<FVector> GetNominalContacts() const;

	UPROPERTY(BlueprintReadOnly, Category="Quad|Foot Step")
	EQuadFootPhase Phase = EQuadFootPhase::Planted;

	UPROPERTY(BlueprintReadOnly, Category="Quad|Foot Step")
	bool CandidateValid = false;

	UPROPERTY(BlueprintReadOnly, Category="Quad|Foot Step")
	FString LiftDiagnostic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quad|Foot Step", meta=(ClampMin="1"))
	float ProbeDistance = 60.0f;

private:
	struct FLeg
	{
		FVector HipLocal;
		FVector RadialLocal;
		FVector NominalContact;
		FVector LocalSole;
		float UpperLength;
		float LowerLength;
		FQuat FootToContact;
		FTransform Planted;
		FVector Contact;
		FVector Normal;
		FHitResult LastConfirmedHit;
	};

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UQuadFootInteractionComponent> Interaction;

	TArray<FLeg> Legs;
	int32 ActiveLegID = INDEX_NONE;
	int32 FocusedLegID = INDEX_NONE;
	int32 LastRequestedID = INDEX_NONE;
	FVector2D AimInput = FVector2D::ZeroVector;
	FVector LiftNormal = FVector::UpVector;
	FVector Heading = FVector::ForwardVector;
	FTransform SwingPose;
	FVector AimStartSole = FVector::ZeroVector;
	double AimStartSurfaceHeight = 0.0;
	FVector BodyPreparationOffset = FVector::ZeroVector;
	FTransform PreparedBodyPose;
	FVector BodyPreparationTarget = FVector::ZeroVector;
	TArray<FVector> LiftRoute;
	int32 RouteIndex = 1;
	bool bFootAirborne = false;
	bool bStepUpRoute = false;
	bool bTriedStepUpCorrection = false;

	bool ConfirmRequested = false;
	bool LandingApproach = false;
	bool AimBlocked = false;
	FHitResult LandingSurface;
	FTransform LandingPose;
	FVector LandingLocalPoint = FVector::ZeroVector;
	FVector LandingLocalNormal = FVector::UpVector;

	FVector GetAimVelocity(const FVector& Normal, float MoveSpeed) const;
	void AdvanceGrips(float DeltaSeconds, float MoveSpeed, float MinBend, float MaxBend);
	void BeginGripRecovery(int32 LegID);
	void BeginLowering();
	bool RefreshLandingSurface(const FLeg& Leg);
	void AdvanceSubstep(float DeltaSeconds, float Clearance, float MoveSpeed, float MinKneeBend, float MaxKneeBend);
	bool PlanInitialLift(const FLeg& Leg, float Clearance, float MinBend, float MaxBend);
	bool FindLiftRoute(const FLeg& Leg, const FTransform& BodyPose, float Clearance, float MinBend, float MaxBend,
		const FVector* StepUpGoal = nullptr);
	bool CanTraverseSole(const FLeg& Leg, const FTransform& BodyPose, const FVector& From, const FVector& To,
		float MinBend, float MaxBend, FString* Reason = nullptr) const;
	bool CanPrepareBody(const FTransform& From, const FTransform& To, float MinBend, float MaxBend) const;
	bool UpdateLandingCandidate(const FLeg& Leg, float MinBend, float MaxBend);
	void FinishLift(float Clearance);
	bool ProbeSurface(const FVector& PlanarPoint, const FVector& Normal, FHitResult& Hit) const;
	bool IsReachable(const FLeg& Leg, const FTransform& BodyPose, const FVector& Location, float MinBend, float MaxBend) const;
	bool CanMoveSole(const FVector& From, const FVector& To, const FVector& Normal,
		const AActor* IgnoredActor = nullptr) const;
	FTransform MakeContactPose(const FLeg& Leg, const FVector& Contact, const FVector& Normal) const;
};
