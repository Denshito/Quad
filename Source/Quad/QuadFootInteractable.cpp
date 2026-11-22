#include "QuadFootInteractable.h"

EQuadGripMode IQuadFootInteractable::DriveGrip_Implementation(const FQuadFootContact& Contact, int32 GripID,
	FVector WorldVelocity, float DeltaSeconds)
{
	return EQuadGripMode::Anchored;
}
