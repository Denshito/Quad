#include "QuadIKMathLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogQuadBody, Log, All);

namespace QuadIKMath
{
	constexpr float MinLength = KINDA_SMALL_NUMBER;

	float ReachAtBend(const float UpperLength, const float LowerLength, const float BendRadians)
	{
		const float SquaredDistance =
			FMath::Square(UpperLength) + FMath::Square(LowerLength)
			+ 2.0f * UpperLength * LowerLength * FMath::Cos(BendRadians);
		return FMath::Sqrt(FMath::Max(0.0f, SquaredDistance));
	}
}

FVector2D UQuadIKMathLibrary::ApplyRadialDeadZone(const FVector2D Input, const float DeadZone)
{
	const float ClampedDeadZone = FMath::Clamp(DeadZone, 0.0f, 0.99f);
	const float Length = Input.Length();
	if (Length <= ClampedDeadZone)
	{
		return FVector2D::ZeroVector;
	}

	const float RemappedLength = FMath::Clamp((Length - ClampedDeadZone) / (1.0f - ClampedDeadZone), 0.0f, 1.0f);
	return Input.GetSafeNormal() * RemappedLength;
}

bool UQuadIKMathLibrary::ComputePlanarReachRadii(
	const float UpperLegLength,
	const float LowerLegLength,
	const float KneeMinBendDegrees,
	const float KneeMaxBendDegrees,
	const float SurfaceDistance,
	const float SafetyMargin,
	float& InnerRadius,
	float& OuterRadius)
{
	InnerRadius = 0.0f;
	OuterRadius = 0.0f;
	if (UpperLegLength <= QuadIKMath::MinLength || LowerLegLength <= QuadIKMath::MinLength)
	{
		return false;
	}

	const float MinBend = FMath::DegreesToRadians(FMath::Min(KneeMinBendDegrees, KneeMaxBendDegrees));
	const float MaxBend = FMath::DegreesToRadians(FMath::Max(KneeMinBendDegrees, KneeMaxBendDegrees));
	const float MaxReach = FMath::Max(0.0f, QuadIKMath::ReachAtBend(UpperLegLength, LowerLegLength, MinBend) - FMath::Max(0.0f, SafetyMargin));
	const float MinReach = QuadIKMath::ReachAtBend(UpperLegLength, LowerLegLength, MaxBend);
	const float Height = FMath::Abs(SurfaceDistance);

	if (Height >= MaxReach)
	{
		return false;
	}

	OuterRadius = FMath::Sqrt(FMath::Max(0.0f, FMath::Square(MaxReach) - FMath::Square(Height)));
	InnerRadius = Height < MinReach
		? FMath::Sqrt(FMath::Max(0.0f, FMath::Square(MinReach) - FMath::Square(Height)))
		: 0.0f;
	return OuterRadius > QuadIKMath::MinLength;
}

bool UQuadIKMathLibrary::BuildSurfaceBasis(
	FVector SurfaceNormal,
	const FVector CameraRight,
	const FVector CameraUp,
	const FVector PawnRight,
	FVector& SurfaceU,
	FVector& SurfaceV)
{
	SurfaceU = FVector::ZeroVector;
	SurfaceV = FVector::ZeroVector;
	if (!SurfaceNormal.Normalize())
	{
		return false;
	}

	SurfaceU = FVector::VectorPlaneProject(CameraRight, SurfaceNormal).GetSafeNormal();
	if (SurfaceU.IsNearlyZero())
	{
		SurfaceU = FVector::VectorPlaneProject(PawnRight, SurfaceNormal).GetSafeNormal();
	}
	if (SurfaceU.IsNearlyZero())
	{
		return false;
	}

	SurfaceV = FVector::CrossProduct(SurfaceNormal, SurfaceU).GetSafeNormal();
	if (FVector::DotProduct(SurfaceV, CameraUp) < 0.0f)
	{
		SurfaceV *= -1.0f;
	}
	return !SurfaceV.IsNearlyZero();
}

FVector UQuadIKMathLibrary::MapStickToReachDisk(
	const FVector SurfaceCenter,
	const FVector SurfaceU,
	const FVector SurfaceV,
	const float OuterRadius,
	const FVector2D StickInput,
	const float DeadZone)
{
	const FVector2D Stick = ApplyRadialDeadZone(StickInput, DeadZone);
	return SurfaceCenter
		+ SurfaceU.GetSafeNormal() * Stick.X * FMath::Max(0.0f, OuterRadius)
		+ SurfaceV.GetSafeNormal() * Stick.Y * FMath::Max(0.0f, OuterRadius);
}

bool UQuadIKMathLibrary::ValidateTwoBoneReach(
	const FTransform HipWorldTransform,
	const FVector CandidateWorld,
	const float UpperLegLength,
	const float LowerLegLength,
	const float KneeMinBendDegrees,
	const float KneeMaxBendDegrees,
	const float HipYawMinDegrees,
	const float HipYawMaxDegrees,
	const float HipPitchMinDegrees,
	const float HipPitchMaxDegrees,
	float& KneeBendDegrees,
	float& HipYawDegrees,
	float& HipPitchDegrees)
{
	KneeBendDegrees = 0.0f;
	HipYawDegrees = 0.0f;
	HipPitchDegrees = 0.0f;
	if (UpperLegLength <= QuadIKMath::MinLength || LowerLegLength <= QuadIKMath::MinLength)
	{
		return false;
	}

	const FVector HipToCandidate = CandidateWorld - HipWorldTransform.GetLocation();
	const float Distance = HipToCandidate.Length();
	if (Distance <= QuadIKMath::MinLength
		|| Distance < FMath::Abs(UpperLegLength - LowerLegLength) - KINDA_SMALL_NUMBER
		|| Distance > UpperLegLength + LowerLegLength + KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float CosBend = FMath::Clamp(
		(FMath::Square(Distance) - FMath::Square(UpperLegLength) - FMath::Square(LowerLegLength))
		/ (2.0f * UpperLegLength * LowerLegLength),
		-1.0f,
		1.0f);
	KneeBendDegrees = FMath::RadiansToDegrees(FMath::Acos(CosBend));

	const FVector LocalDirection = HipWorldTransform.InverseTransformVectorNoScale(HipToCandidate).GetSafeNormal();
	HipYawDegrees = FMath::RadiansToDegrees(FMath::Atan2(LocalDirection.Y, LocalDirection.X));
	HipPitchDegrees = FMath::RadiansToDegrees(FMath::Atan2(
		LocalDirection.Z,
		FMath::Sqrt(FMath::Square(LocalDirection.X) + FMath::Square(LocalDirection.Y))));

	const float MinKnee = FMath::Min(KneeMinBendDegrees, KneeMaxBendDegrees);
	const float MaxKnee = FMath::Max(KneeMinBendDegrees, KneeMaxBendDegrees);
	return KneeBendDegrees >= MinKnee && KneeBendDegrees <= MaxKnee
		&& HipYawDegrees >= FMath::Min(HipYawMinDegrees, HipYawMaxDegrees)
		&& HipYawDegrees <= FMath::Max(HipYawMinDegrees, HipYawMaxDegrees)
		&& HipPitchDegrees >= FMath::Min(HipPitchMinDegrees, HipPitchMaxDegrees)
		&& HipPitchDegrees <= FMath::Max(HipPitchMinDegrees, HipPitchMaxDegrees);
}

namespace QuadIKBodyMath
{
	FQuat RotationBetweenWithFallback(const FVector& From, const FVector& To)
	{
		const FVector FromNormalized = From.GetSafeNormal();
		const FVector ToNormalized = To.GetSafeNormal();
		if (FromNormalized.IsNearlyZero() || ToNormalized.IsNearlyZero())
		{
			return FQuat::Identity;
		}

		const float Dot = FVector::DotProduct(FromNormalized, ToNormalized);
		if (Dot > 1.0f - KINDA_SMALL_NUMBER)
		{
			return FQuat::Identity;
		}
		if (Dot < -1.0f + KINDA_SMALL_NUMBER)
		{
			FVector Axis = FVector::CrossProduct(FromNormalized, FVector::ForwardVector);
			if (Axis.IsNearlyZero())
			{
				Axis = FVector::CrossProduct(FromNormalized, FVector::RightVector);
			}
			return FQuat(Axis.GetSafeNormal(), PI);
		}
		return FQuat::FindBetweenNormals(FromNormalized, ToNormalized);
	}
}

FQuadBodyPose UQuadIKMathLibrary::ComputeSupportBodyPose(
	const TArray<FVector>& FootWorldLocations,
	const TArray<FVector>& FootNormals,
	const TArray<float>& FootWeights,
	const TArray<FVector>& NominalFeetLocal,
	const FVector LastUp,
	const float HeightOffset,
	const float MinWeight)
{
	FQuadBodyPose Result;
	const int32 FootCount = FootWorldLocations.Num();
	if (FootCount == 0 || NominalFeetLocal.Num() != FootCount)
	{
		Result.DebugMessage = FString::Printf(
			TEXT("Invalid: FootCount=%d, NominalFeetLocal.Num()=%d (两个数组必须等长且 >0)"),
			FootCount, NominalFeetLocal.Num());
		UE_LOG(LogQuadBody, Warning, TEXT("%s"), *Result.DebugMessage);
		return Result;
	}

	auto GetNormal = [&FootNormals](const int32 Index) -> FVector
	{
		return FootNormals.IsValidIndex(Index) ? FootNormals[Index] : FVector::UpVector;
	};
	auto GetWeight = [&FootWeights](const int32 Index) -> float
	{
		return FootWeights.IsValidIndex(Index) ? FMath::Clamp(FootWeights[Index], 0.0f, 1.0f) : 1.0f;
	};

	float TotalWeight = 0.0f;
	FVector Centroid = FVector::ZeroVector;
	FVector NormalSum = FVector::ZeroVector;
	int32 WeightedCount = 0;

	for (int32 Index = 0; Index < FootCount; ++Index)
	{
		const float Weight = GetWeight(Index);
		if (Weight < MinWeight)
		{
			continue;
		}

		++WeightedCount;
		TotalWeight += Weight;
		Centroid += FootWorldLocations[Index] * Weight;
		NormalSum += GetNormal(Index).GetSafeNormal() * Weight;
	}

	// A unique 3D body pose needs at least three planted feet.
	if (WeightedCount < 3 || TotalWeight < KINDA_SMALL_NUMBER)
	{
		FString FootSummary;
		for (int32 Index = 0; Index < FootCount; ++Index)
		{
			FootSummary += FString::Printf(TEXT("[%d] w=%.2f loc=(%.1f,%.1f,%.1f) "),
				Index, GetWeight(Index),
				FootWorldLocations[Index].X, FootWorldLocations[Index].Y, FootWorldLocations[Index].Z);
		}
		Result.DebugMessage = FString::Printf(
			TEXT("Invalid: WeightedCount=%d (need >=3), TotalWeight=%.4f (need >0), MinWeight=%.3f | %s"),
			WeightedCount, TotalWeight, MinWeight, *FootSummary);
		UE_LOG(LogQuadBody, Warning, TEXT("%s"), *Result.DebugMessage);
		return Result;
	}

	Centroid /= TotalWeight;
	Result.SupportCentroid = Centroid;

	FVector Up = NormalSum.GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		Up = LastUp.IsNearlyZero() ? FVector::UpVector : LastUp.GetSafeNormal();
	}
	if (!LastUp.IsNearlyZero() && FVector::DotProduct(Up, LastUp.GetSafeNormal()) < 0.0f)
	{
		Up = -Up;
	}
	Result.SupportNormal = Up;

	// Pitch / roll: align the body-local Z axis with the support normal.
	const FQuat Tilt = QuadIKBodyMath::RotationBetweenWithFallback(FVector::UpVector, Up);

	// Weighted centroid of the nominal footprint in body-local space.
	FVector NominalCentroid = FVector::ZeroVector;
	for (int32 Index = 0; Index < FootCount; ++Index)
	{
		const float Weight = GetWeight(Index);
		if (Weight < MinWeight)
		{
			continue;
		}
		NominalCentroid += NominalFeetLocal[Index] * Weight;
	}
	NominalCentroid /= TotalWeight;

	// Yaw: 2D rigid fit of the tilted nominal footprint onto the actual footprint.
	double A = 0.0;
	double B = 0.0;
	for (int32 Index = 0; Index < FootCount; ++Index)
	{
		const float Weight = GetWeight(Index);
		if (Weight < MinWeight)
		{
			continue;
		}

		const FVector NominalOffset = Tilt.RotateVector(NominalFeetLocal[Index] - NominalCentroid);
		const FVector P = NominalOffset - Up * FVector::DotProduct(NominalOffset, Up);

		const FVector ActualOffset = FootWorldLocations[Index] - Centroid;
		const FVector Q = ActualOffset - Up * FVector::DotProduct(ActualOffset, Up);

		A += Weight * FVector::DotProduct(P, Q);
		B += Weight * FVector::DotProduct(FVector::CrossProduct(P, Q), Up);
	}

	// The yaw is fully determined by the foot pattern. If the planted footprint is
	// collinear (for example only two diagonal feet on the ground), the yaw is
	// undefined, so report the pose as invalid and let the caller hold the last one.
	if ((FMath::Abs(A) + FMath::Abs(B)) <= KINDA_SMALL_NUMBER)
	{
		FString FootSummary;
		for (int32 Index = 0; Index < FootCount; ++Index)
		{
			FootSummary += FString::Printf(TEXT("[%d] w=%.2f loc=(%.1f,%.1f,%.1f) "),
				Index, GetWeight(Index),
				FootWorldLocations[Index].X, FootWorldLocations[Index].Y, FootWorldLocations[Index].Z);
		}
		Result.DebugMessage = FString::Printf(
			TEXT("Invalid: degenerate yaw, A=%.6f B=%.6f (四个点共线或重合?), Up=(%.2f,%.2f,%.2f) | %s"),
			A, B, Up.X, Up.Y, Up.Z, *FootSummary);
		UE_LOG(LogQuadBody, Warning, TEXT("%s"), *Result.DebugMessage);
		return Result;
	}
	const float Yaw = static_cast<float>(FMath::Atan2(B, A));

	Result.YawDegrees = FMath::RadiansToDegrees(Yaw);

	const FQuat YawRotation(Up, Yaw);
	const FQuat BodyRotation = (YawRotation * Tilt).GetNormalized();

	// Position: map the nominal foot centroid onto the actual contact centroid.
	FVector BodyLocation = Centroid - BodyRotation.RotateVector(NominalCentroid);
	BodyLocation += Up * HeightOffset;

	Result.Transform = FTransform(BodyRotation, BodyLocation, FVector::OneVector);
	Result.bValid = true;
	Result.DebugMessage = FString::Printf(
		TEXT("Valid: feet=%d weighted=%d weight=%.2f yaw=%.2f up=(%.2f,%.2f,%.2f)"),
		FootCount, WeightedCount, TotalWeight, Result.YawDegrees, Up.X, Up.Y, Up.Z);
	UE_LOG(LogQuadBody, Verbose, TEXT("%s"), *Result.DebugMessage);
	return Result;
}

FTransform UQuadIKMathLibrary::UpdateBodyFollow(
	FQuadBodyFollowState& State,
	const FQuadBodyPose& Target,
	const float DeltaSeconds,
	const float PositionSpeed,
	const float RotationSpeed)
{
	if (!Target.bValid)
	{
		return State.Current;
	}

	if (!State.bInitialized)
	{
		State.Current = Target.Transform;
		State.LinearVelocity = FVector::ZeroVector;
		State.bInitialized = true;
		return State.Current;
	}

	const float DeltaTime = FMath::Max(DeltaSeconds, 0.0f);
	if (DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return State.Current;
	}

	// Analytically integrated critically damped spring (no overshoot, stable for any dt).
	const float Omega = FMath::Max(PositionSpeed, 0.01f);
	const FVector TargetLocation = Target.Transform.GetLocation();
	const FVector Error = State.Current.GetLocation() - TargetLocation;
	const FVector VelocityTerm = State.LinearVelocity + Error * Omega;
	const float Decay = FMath::Exp(-Omega * DeltaTime);

	const FVector NextError = (Error + VelocityTerm * DeltaTime) * Decay;
	State.LinearVelocity = (VelocityTerm - (Error + VelocityTerm * DeltaTime) * Omega) * Decay;
	State.Current.SetLocation(TargetLocation + NextError);

	// Exponential slerp for orientation.
	const float RotationAlpha = 1.0f - FMath::Exp(-FMath::Max(RotationSpeed, 0.01f) * DeltaTime);
	State.Current.SetRotation(FQuat::Slerp(
		State.Current.GetRotation(),
		Target.Transform.GetRotation(),
		RotationAlpha).GetNormalized());

	return State.Current;
}
