#pragma once

#include "GameFramework/Actor.h"
#include "QuadFootInteractable.h"
#include "QuadFootExamples.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPhysicsConstraintComponent;
class UMaterialInstanceDynamic;
class ATextRenderActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQuadActivationChanged, bool, Active);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQuadTravelChanged, float, Travel);

UCLASS(Blueprintable)
class QUAD_API AQuadPressurePlate : public AActor, public IQuadFootInteractable
{
	GENERATED_BODY()

public:
	AQuadPressurePlate();
	void BeginPlay() override;
	void FootLanded_Implementation(const FQuadFootContact& Contact) override;
	void FootLeft_Implementation(const FQuadFootContact& Contact) override;

	UPROPERTY(BlueprintReadOnly, Category="Pressure Plate")
	bool Pressed = false;

	UPROPERTY(BlueprintAssignable, Category="Pressure Plate")
	FQuadActivationChanged OnPressedChanged;

private:
	UPROPERTY(EditInstanceOnly, Category="Debug")
	TObjectPtr<ATextRenderActor> DebugLabel;
	UPROPERTY(VisibleAnywhere, Category="Pressure Plate")
	TObjectPtr<UBoxComponent> ContactSurface;
	UPROPERTY(VisibleAnywhere, Category="Pressure Plate")
	TObjectPtr<UStaticMeshComponent> Plunger;

	TMap<TWeakObjectPtr<AActor>, TMap<int32, int32>> Occupants;
	void UpdatePressed();
	void UpdateDebugLabel() const;
};

UENUM(BlueprintType)
enum class EQuadMechanismKind : uint8
{
	Detachable,
	Hinge,
	Dial,
	Slider
};

UCLASS(Blueprintable)
class QUAD_API AQuadFootMechanism : public AActor, public IQuadFootInteractable
{
	GENERATED_BODY()

public:
	AQuadFootMechanism();
	void BeginPlay() override;
	void Tick(float DeltaSeconds) override;

	FQuadGrip TryBeginGrip_Implementation(const FQuadFootContact& Contact, int32 GripID) override;
	FVector GetGripMotionAxis_Implementation(const FQuadFootContact& Contact, int32 GripID, FVector GrabPoint) override;
	EQuadGripMode DriveGrip_Implementation(const FQuadFootContact& Contact, int32 GripID,
		FVector WorldVelocity, float DeltaSeconds) override;
	void EndGrip_Implementation(const FQuadFootContact& Contact, int32 GripID, EQuadGripEndReason Reason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foot Mechanism")
	EQuadMechanismKind Kind = EQuadMechanismKind::Slider;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foot Mechanism", meta=(ClampMin="2.1"))
	float TravelLimit = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foot Mechanism")
	FVector ConstraintPivotOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foot Mechanism")
	float MaxForce = 24000.0f;

	// Torque at the reference inertia. Larger rotors get proportionally more torque.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foot Mechanism")
	float MaxTorque = 120000.0f;

	UPROPERTY(BlueprintReadOnly, Category="Foot Mechanism")
	float Travel = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Foot Mechanism")
	bool Detached = false;

	UPROPERTY(BlueprintReadOnly, Category="Foot Mechanism")
	bool Activated = false;

	UPROPERTY(BlueprintAssignable, Category="Foot Mechanism")
	FQuadTravelChanged OnTravelChanged;

	UPROPERTY(BlueprintAssignable, Category="Foot Mechanism")
	FQuadActivationChanged OnActivatedChanged;

private:
	UPROPERTY(EditInstanceOnly, Category="Debug")
	TObjectPtr<ATextRenderActor> DebugLabel;
	UPROPERTY(VisibleAnywhere, Category="Foot Mechanism")
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="Foot Mechanism")
	TObjectPtr<UPhysicsConstraintComponent> Constraint;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayMaterial;

	TWeakObjectPtr<AActor> GrippingQuad;
	int32 GrippingLeg = INDEX_NONE;
	int32 CurrentGripID = 0;
	int32 PreviousExcursion = 0;
	int32 Reversals = 0;
	double LastExcursionTime = 0.0;
	FVector WorldAxis = FVector::ForwardVector;
	FVector MountedPosition = FVector::ZeroVector;
	FQuat MountedRotation = FQuat::Identity;
	FVector GripPointLocal = FVector::ZeroVector;
	bool bGripNearPivot = false;

	bool IsRotary() const;
	bool OwnsGrip(const FQuadFootContact& Contact, int32 ID) const;
	void UpdateDebugLabel() const;
};
