#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "UObject/Interface.h"
#include "QuadFootInteractable.generated.h"

class UPrimitiveComponent;
class USceneComponent;

UENUM(BlueprintType)
enum class EQuadGripMode : uint8
{
	None,
	Anchored,
	Carrying
};

UENUM(BlueprintType)
enum class EQuadGripEndReason : uint8
{
	Released,
	TargetLost,
	Unreachable,
	Reset
};

USTRUCT(BlueprintType)
struct QUAD_API FQuadFootContact
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Foot Contact")
	TObjectPtr<AActor> Quad = nullptr;

	UPROPERTY(BlueprintReadOnly, Category="Foot Contact")
	int32 LegID = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category="Foot Contact")
	int32 ContactID = 0;

	UPROPERTY(BlueprintReadOnly, Category="Foot Contact")
	FHitResult Hit;
};

USTRUCT(BlueprintType)
struct QUAD_API FQuadGrip
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foot Grip")
	EQuadGripMode Mode = EQuadGripMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foot Grip")
	TObjectPtr<USceneComponent> Anchor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foot Grip")
	TObjectPtr<UPrimitiveComponent> CarryBody = nullptr;
};

UINTERFACE(BlueprintType)
class QUAD_API UQuadFootInteractable : public UInterface
{
	GENERATED_BODY()
};

class QUAD_API IQuadFootInteractable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Quad|Foot Contact")
	void FootLanded(const FQuadFootContact& Contact);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Quad|Foot Contact")
	void FootLeft(const FQuadFootContact& Contact);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Quad|Foot Grip")
	FQuadGrip TryBeginGrip(const FQuadFootContact& Contact, int32 GripID);

	// A zero vector leaves the input unconstrained. Axis and grab point are in world space.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Quad|Foot Grip")
	FVector GetGripMotionAxis(const FQuadFootContact& Contact, int32 GripID, FVector GrabPoint);

	// Return a new mode to transition, such as Carrying after a fixture breaks.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Quad|Foot Grip")
	EQuadGripMode DriveGrip(const FQuadFootContact& Contact, int32 GripID, FVector WorldVelocity, float DeltaSeconds);
	virtual EQuadGripMode DriveGrip_Implementation(const FQuadFootContact& Contact, int32 GripID,
		FVector WorldVelocity, float DeltaSeconds);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Quad|Foot Grip")
	void EndGrip(const FQuadFootContact& Contact, int32 GripID, EQuadGripEndReason Reason);
};
