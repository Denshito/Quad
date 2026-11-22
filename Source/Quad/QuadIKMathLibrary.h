#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "QuadIKMathLibrary.generated.h"

/** Result of the support body solver. */
USTRUCT(BlueprintType)
struct QUAD_API FQuadBodyPose
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quad|Body")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly, Category = "Quad|Body")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Quad|Body")
	FVector SupportCentroid = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Quad|Body")
	FVector SupportNormal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Quad|Body")
	float YawDegrees = 0.0f;

	/** Human readable reason for the last solve result (especially when bValid is false). */
	UPROPERTY(BlueprintReadOnly, Category = "Quad|Body")
	FString DebugMessage;
};

/** Persistent state for the critically damped body follow. */
USTRUCT(BlueprintType)
struct QUAD_API FQuadBodyFollowState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quad|Body")
	FTransform Current = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly, Category = "Quad|Body")
	FVector LinearVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Quad|Body")
	bool bInitialized = false;
};

UCLASS()
class QUAD_API UQuadIKMathLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Quad|IK")
	static FVector2D ApplyRadialDeadZone(FVector2D Input, float DeadZone = 0.1f);

	UFUNCTION(BlueprintPure, Category = "Quad|IK")
	static bool ComputePlanarReachRadii(
		float UpperLegLength,
		float LowerLegLength,
		float KneeMinBendDegrees,
		float KneeMaxBendDegrees,
		float SurfaceDistance,
		float SafetyMargin,
		float& InnerRadius,
		float& OuterRadius);

	UFUNCTION(BlueprintPure, Category = "Quad|IK")
	static bool BuildSurfaceBasis(
		FVector SurfaceNormal,
		FVector CameraRight,
		FVector CameraUp,
		FVector PawnRight,
		FVector& SurfaceU,
		FVector& SurfaceV);

	UFUNCTION(BlueprintPure, Category = "Quad|IK")
	static FVector MapStickToReachDisk(
		FVector SurfaceCenter,
		FVector SurfaceU,
		FVector SurfaceV,
		float OuterRadius,
		FVector2D StickInput,
		float DeadZone = 0.1f);

	UFUNCTION(BlueprintPure, Category = "Quad|IK")
	static bool ValidateTwoBoneReach(
		FTransform HipWorldTransform,
		FVector CandidateWorld,
		float UpperLegLength,
		float LowerLegLength,
		float KneeMinBendDegrees,
		float KneeMaxBendDegrees,
		float HipYawMinDegrees,
		float HipYawMaxDegrees,
		float HipPitchMinDegrees,
		float HipPitchMaxDegrees,
		float& KneeBendDegrees,
		float& HipYawDegrees,
		float& HipPitchDegrees);

	/**
	 * Computes the body transform that follows the planted feet.
	 * - Pitch / roll come from the weighted average contact normal.
	 * - Yaw comes from a planar rigid fit of the nominal footprint to the actual footprint,
	 *   so the body turns because the feet translated into a rotated arrangement.
	 * - Position is the weighted contact centroid mapped through the rigid fit.
	 *
	 * Feed the three arrays from your existing S_QuadLeg array:
	 *   FootWorldLocations[i] = Legs[i].CurrentFootIKTarget.GetLocation()
	 *   FootNormals[i]        = Legs[i].CurrentFootIKNormal
	 *   FootWeights[i]        = Legs[i].Weight
	 * so no extra struct is required.
	 *
	 * The body yaw is fully derived from the foot pattern; there is no forward/input direction.
	 * If fewer than three feet are planted, or the footprint is collinear, the result is invalid
	 * and the caller should keep its previous body transform.
	 *
	 * @param FootWorldLocations World-space contact points, in a stable order.
	 * @param FootNormals        World-space contact normals; may be empty to use world up.
	 * @param FootWeights        Support weights (0 swing .. 1 planted); may be empty to use 1.
	 * @param NominalFeetLocal   Rest-pose foot positions in body-local space (Z up), same order.
	 * @param LastUp             Previous support normal (optional), used to prevent sign flips.
	 * @param HeightOffset       Offset along the support normal (positive = lift the body).
	 * @param MinWeight          Feet below this weight are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "Quad|Body")
	static FQuadBodyPose ComputeSupportBodyPose(
		const TArray<FVector>& FootWorldLocations,
		const TArray<FVector>& FootNormals,
		const TArray<float>& FootWeights,
		const TArray<FVector>& NominalFeetLocal,
		FVector LastUp = FVector::ZeroVector,
		float HeightOffset = 0.0f,
		float MinWeight = 0.1f);

	/**
	 * Critically damped follow of the body towards a target pose.
	 * Holds the previous pose while the target is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quad|Body")
	static FTransform UpdateBodyFollow(
		UPARAM(ref) FQuadBodyFollowState& State,
		const FQuadBodyPose& Target,
		float DeltaSeconds,
		float PositionSpeed = 8.0f,
		float RotationSpeed = 6.0f);
};
