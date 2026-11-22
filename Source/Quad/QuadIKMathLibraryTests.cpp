#if WITH_DEV_AUTOMATION_TESTS

#include "QuadIKMathLibrary.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuadIKMathLibraryTest,
	"Quad.IK.MathLibrary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuadIKMathLibraryTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Dead-zone center is zero"), UQuadIKMathLibrary::ApplyRadialDeadZone(FVector2D(0.05f, 0.0f), 0.1f).IsNearlyZero());
	TestTrue(TEXT("Full stick stays full"), UQuadIKMathLibrary::ApplyRadialDeadZone(FVector2D(1.0f, 0.0f), 0.1f).Equals(FVector2D(1.0f, 0.0f)));

	float InnerRadius = 0.0f;
	float OuterRadius = 0.0f;
	TestTrue(TEXT("Reach disk intersects the surface"), UQuadIKMathLibrary::ComputePlanarReachRadii(
		100.0f, 100.0f, 0.0f, 180.0f, 100.0f, 0.0f, InnerRadius, OuterRadius));
	TestEqual(TEXT("Folded equal links have no inner hole"), InnerRadius, 0.0f);
	TestTrue(TEXT("Outer radius follows the sphere-plane intersection"), FMath::IsNearlyEqual(OuterRadius, FMath::Sqrt(30000.0f), 0.01f));

	FVector SurfaceU;
	FVector SurfaceV;
	TestTrue(TEXT("Surface basis is valid"), UQuadIKMathLibrary::BuildSurfaceBasis(
		FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector::RightVector, SurfaceU, SurfaceV));
	TestTrue(TEXT("Surface U follows camera right"), SurfaceU.Equals(FVector::RightVector));
	TestTrue(TEXT("Surface V follows camera up"), SurfaceV.Equals(FVector::UpVector));

	float KneeBend = 0.0f;
	float HipYaw = 0.0f;
	float HipPitch = 0.0f;
	TestTrue(TEXT("A 90-degree two-bone target is valid"), UQuadIKMathLibrary::ValidateTwoBoneReach(
		FTransform::Identity,
		FVector(100.0f, 100.0f, 0.0f),
		100.0f,
		100.0f,
		0.0f,
		180.0f,
		-90.0f,
		90.0f,
		-90.0f,
		90.0f,
		KneeBend,
		HipYaw,
		HipPitch));
	TestTrue(TEXT("Knee bend is 90 degrees"), FMath::IsNearlyEqual(KneeBend, 90.0f, 0.01f));
	TestTrue(TEXT("Hip yaw is 45 degrees"), FMath::IsNearlyEqual(HipYaw, 45.0f, 0.01f));

	// ---- Support body solver (S_QuadLeg fields mapped to parallel arrays) ----
	auto BuildFootArrays = [](const TArray<FVector>& Points, const FVector& Normal, const TArray<float>& InWeights,
		TArray<FVector>& OutLocations, TArray<FVector>& OutNormals, TArray<float>& OutWeights)
	{
		OutLocations.Reset();
		OutNormals.Reset();
		OutWeights.Reset();
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			OutLocations.Add(Points[Index]);
			OutNormals.Add(Normal);
			OutWeights.Add(InWeights.IsValidIndex(Index) ? InWeights[Index] : 1.0f);
		}
	};

	{
		// An identity footprint must reproduce the identity body transform.
		TArray<FVector> NominalFeet;
		NominalFeet.Add(FVector(100.0, 100.0, 0.0));
		NominalFeet.Add(FVector(100.0, -100.0, 0.0));
		NominalFeet.Add(FVector(-100.0, -100.0, 0.0));
		NominalFeet.Add(FVector(-100.0, 100.0, 0.0));
		TArray<FVector> Locations;
		TArray<FVector> Normals;
		TArray<float> Weights;
		BuildFootArrays(NominalFeet, FVector::UpVector, TArray<float>(), Locations, Normals, Weights);

		const FQuadBodyPose Pose = UQuadIKMathLibrary::ComputeSupportBodyPose(
			Locations, Normals, Weights, NominalFeet);

		TestTrue(TEXT("Body solver accepts a planted footprint"), Pose.bValid);
		TestTrue(TEXT("Identity footprint yields identity body"), Pose.Transform.Equals(FTransform::Identity, 0.01f));
		TestTrue(TEXT("Identity footprint yields zero yaw"), FMath::IsNearlyZero(Pose.YawDegrees, 0.01f));
	}

	{
		// A common translation must move the body without rotating it.
		TArray<FVector> NominalFeet;
		NominalFeet.Add(FVector(100.0, 100.0, 0.0));
		NominalFeet.Add(FVector(100.0, -100.0, 0.0));
		NominalFeet.Add(FVector(-100.0, -100.0, 0.0));
		NominalFeet.Add(FVector(-100.0, 100.0, 0.0));
		const FVector Translation(100.0, 0.0, 0.0);
		TArray<FVector> Locations;
		TArray<FVector> Normals;
		TArray<float> Weights;
		BuildFootArrays(NominalFeet, FVector::UpVector, TArray<float>(), Locations, Normals, Weights);
		for (FVector& Location : Locations)
		{
			Location += Translation;
		}

		const FQuadBodyPose Pose = UQuadIKMathLibrary::ComputeSupportBodyPose(
			Locations, Normals, Weights, NominalFeet);

		TestTrue(TEXT("Translated footprint moves the body"), Pose.Transform.GetLocation().Equals(Translation, 0.01f));
		TestTrue(TEXT("Translated footprint keeps the body level"), Pose.Transform.GetRotation().Equals(FQuat::Identity, 0.001f));
	}

	{
		// Rotating the footprint around its centroid must rotate the body yaw.
		TArray<FVector> NominalFeet;
		NominalFeet.Add(FVector(100.0, 100.0, 0.0));
		NominalFeet.Add(FVector(100.0, -100.0, 0.0));
		NominalFeet.Add(FVector(-100.0, -100.0, 0.0));
		NominalFeet.Add(FVector(-100.0, 100.0, 0.0));
		const FQuat Yaw(FVector::UpVector, FMath::DegreesToRadians(30.0f));
		TArray<FVector> Locations;
		TArray<FVector> Normals;
		TArray<float> Weights;
		BuildFootArrays(NominalFeet, FVector::UpVector, TArray<float>(), Locations, Normals, Weights);
		for (FVector& Location : Locations)
		{
			Location = Yaw.RotateVector(Location);
		}

		const FQuadBodyPose Pose = UQuadIKMathLibrary::ComputeSupportBodyPose(
			Locations, Normals, Weights, NominalFeet);

		TestTrue(TEXT("Rotated footprint yields 30 degree body yaw"), FMath::IsNearlyEqual(Pose.YawDegrees, 30.0f, 0.5f));
	}

	{
		// A tilted support plane must tilt the body, not yaw it.
		TArray<FVector> NominalFeet;
		NominalFeet.Add(FVector(100.0, 100.0, 0.0));
		NominalFeet.Add(FVector(100.0, -100.0, 0.0));
		NominalFeet.Add(FVector(-100.0, -100.0, 0.0));
		NominalFeet.Add(FVector(-100.0, 100.0, 0.0));
		const FVector Normal = FVector::ForwardVector;
		const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
		TArray<FVector> Locations;
		TArray<FVector> Normals;
		TArray<float> Weights;
		BuildFootArrays(NominalFeet, Normal, TArray<float>(), Locations, Normals, Weights);
		for (FVector& Location : Locations)
		{
			Location = Tilt.RotateVector(Location);
		}

		const FQuadBodyPose Pose = UQuadIKMathLibrary::ComputeSupportBodyPose(
			Locations, Normals, Weights, NominalFeet);

		TestTrue(TEXT("Tilted plane yields a valid body"), Pose.bValid);
		TestTrue(TEXT("Body up follows the support normal"),
			Pose.Transform.GetRotation().RotateVector(FVector::UpVector).Equals(Normal, 0.01f));
	}

	{
		// Fewer than three planted feet cannot define a unique body pose.
		TArray<FVector> NominalFeet;
		NominalFeet.Add(FVector(100.0, 100.0, 0.0));
		NominalFeet.Add(FVector(100.0, -100.0, 0.0));
		NominalFeet.Add(FVector(-100.0, -100.0, 0.0));
		NominalFeet.Add(FVector(-100.0, 100.0, 0.0));
		TArray<FVector> Locations;
		TArray<FVector> Normals;
		TArray<float> Weights;
		BuildFootArrays(NominalFeet, FVector::UpVector, TArray<float>(), Locations, Normals, Weights);
		Weights[2] = 0.0f;
		Weights[3] = 0.0f;

		const FQuadBodyPose Pose = UQuadIKMathLibrary::ComputeSupportBodyPose(
			Locations, Normals, Weights, NominalFeet);

		TestFalse(TEXT("Two planted feet are rejected"), Pose.bValid);
	}

	return true;
}

#endif
