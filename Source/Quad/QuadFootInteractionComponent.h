#pragma once

#include "Components/ActorComponent.h"
#include "QuadFootInteractable.h"
#include "QuadFootInteractionComponent.generated.h"

class UQuadFootStepComponent;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQuadFootContactEvent, const FQuadFootContact&, Contact);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FQuadFootGripEvent, int32, LegID, int32, GripID, EQuadGripMode, Mode);

UCLASS(ClassGroup=(Quad), meta=(BlueprintSpawnableComponent))
class QUAD_API UQuadFootInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Quad|Foot Interaction")
	static void ToggleGripFor(AActor* Quad);

	UFUNCTION(BlueprintPure, Category="Quad|Foot Interaction")
	EQuadGripMode GetGripMode(int32 LegID) const;

	UFUNCTION(BlueprintPure, Category="Quad|Foot Interaction")
	bool GetFootContact(int32 LegID, FQuadFootContact& Contact) const;

	UPROPERTY(BlueprintAssignable, Category="Quad|Foot Interaction")
	FQuadFootContactEvent OnFootLanded;

	UPROPERTY(BlueprintAssignable, Category="Quad|Foot Interaction")
	FQuadFootContactEvent OnFootLeft;

	UPROPERTY(BlueprintAssignable, Category="Quad|Foot Interaction")
	FQuadFootGripEvent OnGripChanged;

protected:
	void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	friend class UQuadFootStepComponent;

	struct FFoot
	{
		FQuadFootContact Contact;
		TWeakObjectPtr<UObject> ContactReceiver;
		bool bContact = false;

		FQuadFootContact GripContact;
		TWeakObjectPtr<UObject> GripReceiver;
		TWeakObjectPtr<USceneComponent> Anchor;
		TWeakObjectPtr<UPrimitiveComponent> CarryBody;
		FTransform FootInAnchor;
		FTransform CarryInFoot;
		FTransform GripTarget;
		int32 GripID = 0;
		EQuadGripMode GripMode = EQuadGripMode::None;
		bool bNeedsLanding = false;
	};

	TArray<FFoot> Feet;
	int32 NextContactID = 0;
	int32 NextGripID = 0;
	bool bDispatching = false;

	void ResetFeet(int32 Count);
	void CommitContact(int32 LegID, const FHitResult& Hit);
	void LeaveContact(int32 LegID);
	void BeginGrip(int32 LegID, const FTransform& FootPose, USkeletalMeshComponent& Mesh);
	void ReleaseGrip(int32 LegID, EQuadGripEndReason Reason);
	bool AttachCarry(int32 LegID, USkeletalMeshComponent& Mesh);
	void DriveGrip(int32 LegID, const FVector& Velocity, float DeltaSeconds, USkeletalMeshComponent& Mesh);
};
