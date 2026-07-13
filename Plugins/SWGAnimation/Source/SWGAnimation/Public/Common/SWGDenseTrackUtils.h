#pragma once

#include "CoreMinimal.h"

/**
 * Converts a bone's sparse (frame -> value) keyframes into a dense,
 * one-sample-per-frame array via interpolation between the surrounding
 * known keys. Shared by FSWGAnimationImporter (building UAnimSequence
 * tracks) and FSWGRuntimeAnimationPlayer (direct runtime sampling) — both
 * need the same resampling, just feeding it to different consumers.
 */
inline TArray<FQuat> SWGBuildDenseRotationTrack(const TMap<int32, FQuat>& Sparse, int32 FrameCount, const FQuat& Fallback)
{
	TArray<FQuat> Dense;
	Dense.SetNum(FrameCount);

	if (Sparse.Num() == 0)
	{
		for (int32 i = 0; i < FrameCount; ++i)
		{
			Dense[i] = Fallback;
		}
		return Dense;
	}

	TArray<int32> KnownFrames;
	Sparse.GetKeys(KnownFrames);
	KnownFrames.Sort();

	for (int32 Frame = 0; Frame < FrameCount; ++Frame)
	{
		if (const FQuat* Exact = Sparse.Find(Frame))
		{
			Dense[Frame] = *Exact;
			continue;
		}

		// Find the known keys bracketing this frame.
		int32 PrevFrame = INDEX_NONE, NextFrame = INDEX_NONE;
		for (int32 KnownFrame : KnownFrames)
		{
			if (KnownFrame < Frame) PrevFrame = KnownFrame;
			if (KnownFrame > Frame && NextFrame == INDEX_NONE) NextFrame = KnownFrame;
		}

		if (PrevFrame == INDEX_NONE)
		{
			Dense[Frame] = Sparse[NextFrame]; // before the first key — hold it
		}
		else if (NextFrame == INDEX_NONE)
		{
			Dense[Frame] = Sparse[PrevFrame]; // after the last key — hold it
		}
		else
		{
			const float Alpha = (float)(Frame - PrevFrame) / (float)(NextFrame - PrevFrame);
			Dense[Frame] = FQuat::Slerp(Sparse[PrevFrame], Sparse[NextFrame], Alpha);
		}
	}

	// An isolated bad sample jumps away from its neighbors and back within one
	// frame, so skipping it (i-1 to i+1 directly) gives a much smaller total
	// rotation than going through it. Detect that and replace with the
	// neighbor midpoint; leaves genuine large-but-monotonic motion untouched.
	if (FrameCount >= 3)
	{
		auto ShortestAngleDeg = [](const FQuat& A, const FQuat& B) -> float
		{
			FQuat Delta = A.Inverse() * B;
			if (Delta.W < 0.0f) Delta = Delta * -1.0f; // hemisphere-correct GetAngle()
			return FMath::RadiansToDegrees(Delta.GetAngle());
		};

		TArray<FQuat> Smoothed = Dense;
		for (int32 i = 0; i < FrameCount; ++i)
		{
			const int32 Prev = (i - 1 + FrameCount) % FrameCount;
			const int32 Next = (i + 1) % FrameCount;
			const float Ang1 = ShortestAngleDeg(Dense[Prev], Dense[i]);
			const float Ang2 = ShortestAngleDeg(Dense[i], Dense[Next]);
			const float Ang3 = ShortestAngleDeg(Dense[Prev], Dense[Next]);
			if (Ang1 > 25.0f && Ang2 > 25.0f && Ang3 < (Ang1 + Ang2) * 0.4f)
			{
				Smoothed[i] = FQuat::Slerp(Dense[Prev], Dense[Next], 0.5f);
			}
		}
		Dense = MoveTemp(Smoothed);
	}

	return Dense;
}

// Same sparse->dense conversion as rotation, but LERP instead of SLERP
// (translation deltas, not rotations) — used for the root bone's
// translation track (FSWGAnimationData::RootTranslationDeltas).
inline TArray<FVector> SWGBuildDenseTranslationTrack(const TMap<int32, FVector>& Sparse, int32 FrameCount, const FVector& BindPose)
{
	TArray<FVector> Dense;
	Dense.SetNum(FrameCount);

	if (Sparse.Num() == 0)
	{
		for (int32 i = 0; i < FrameCount; ++i)
		{
			Dense[i] = BindPose;
		}
		return Dense;
	}

	TArray<int32> KnownFrames;
	Sparse.GetKeys(KnownFrames);
	KnownFrames.Sort();

	for (int32 Frame = 0; Frame < FrameCount; ++Frame)
	{
		FVector Delta;
		if (const FVector* Exact = Sparse.Find(Frame))
		{
			Delta = *Exact;
		}
		else
		{
			int32 PrevFrame = INDEX_NONE, NextFrame = INDEX_NONE;
			for (int32 KnownFrame : KnownFrames)
			{
				if (KnownFrame < Frame) PrevFrame = KnownFrame;
				if (KnownFrame > Frame && NextFrame == INDEX_NONE) NextFrame = KnownFrame;
			}

			if (PrevFrame == INDEX_NONE)
			{
				Delta = Sparse[NextFrame];
			}
			else if (NextFrame == INDEX_NONE)
			{
				Delta = Sparse[PrevFrame];
			}
			else
			{
				const float Alpha = (float)(Frame - PrevFrame) / (float)(NextFrame - PrevFrame);
				Delta = FMath::Lerp(Sparse[PrevFrame], Sparse[NextFrame], Alpha);
			}
		}
		Dense[Frame] = BindPose + Delta;
	}
	return Dense;
}
