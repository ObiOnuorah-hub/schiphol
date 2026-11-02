#include "TaxiNetworkSubsystem.h"
#include "AircraftCommands.h"
#include "AircraftMovementComponent.h"
#include "AircraftTypeData.h"
#include "ATCAircraft.h"
#include "ATCSimSettings.h"
#include "ATCUnits.h"
#include "TaxiRoute.h"
#include "TrafficSubsystem.h"
#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** Distance between samples along a route. */
	constexpr float StepCm = 500.f;

	/** Samples before the start and past the end of every route: noses and tails stick out that far (80 m). */
	constexpr int32 OutsideSamples = 16;

	/** Two routes this close and in line use the same piece of taxiway. */
	constexpr float CoincideCm = 400.f;

	/**
	 * Another route this close running the other way makes a two-way stretch: the same taxiway (within 25 degrees), or a
	 * parallel one (within 10 degrees) so close that two wide-bodies could not pass (wingspan 80 m plus clearance). Who
	 * actually conflicts depends on their size. Taxiways that only come together at a junction are crossings instead.
	 */
	constexpr float TwoWayRadiusCm = 9000.f;
	constexpr float SameLaneDot = -0.9f;
	constexpr float ParallelDot = -0.985f;

	/** In opposite directions on the same piece of taxiway claims stay this far apart: their lengths already hold the bodies. */
	constexpr float LaneGapCm = 1000.f;

	/**
	 * Another route this close at an angle makes a crossing or a junction, where nobody stops: the widest wings (an A380)
	 * plus the wingtip clearance, like the holding position of a code F taxiway.
	 */
	constexpr float CrossingRadiusCm = 5000.f;

	/** The claim reaches this far behind the tail. */
	constexpr float TailMarginCm = 1000.f;

	/**
	 * Before a two-way stretch an aircraft waits this far back (100 m): one that leaves the stretch on that side then
	 * still fits in between, even an A380. A stop that needs a clearance shortens it (the runway is the controller's).
	 */
	constexpr int32 ApproachSamples = 20;

	/** Short gaps in a two-way stretch or a crossing (a corner where the two directions do not quite line up) are bridged. */
	constexpr int32 TwoWayBridgeSamples = 10;
	constexpr int32 CrossingBridgeSamples = 2;


	constexpr float GridCm = 1000.f;
	constexpr float CoarseCm = 3000.f;
	constexpr float HeldCellCm = 2500.f;

	/** Lane choice in seconds of taxi time: per aircraft coming the other way, per aircraft ahead, for the right-hand lane. */
	constexpr float OpposingSeconds = 240.f;
	constexpr float AheadSeconds = 40.f;
	constexpr float KeepRightSeconds = 90.f;
	constexpr float LaneSpeedCms = 800.f;

	/** Waiting this long in a circle of aircraft that wait for each other is a deadlock. */
	constexpr float DeadlockSeconds = 60.f;
	constexpr float BackOffSeconds = 30.f;

	FIntPoint CellOf(const FVector& Location, float Size)
	{
		return FIntPoint(FMath::FloorToInt(Location.X / Size), FMath::FloorToInt(Location.Y / Size));
	}

	/** Direction bin of 22.5 degrees (0..15). */
	int32 DirectionBin(const FVector& Direction)
	{
		const float Angle = FMath::Fmod(FMath::Atan2(Direction.Y, Direction.X) + 2.f * PI, 2.f * PI);
		return FMath::Clamp(FMath::FloorToInt(Angle / (PI / 8.f)), 0, 15);
	}

	/** The bins of Bin + Offset and its two neighbours. */
	uint16 BinsAround(int32 Bin, int32 Offset)
	{
		return uint16((1 << ((Bin + Offset + 15) % 16)) | (1 << ((Bin + Offset) % 16)) | (1 << ((Bin + Offset + 1) % 16)));
	}
}

bool UTaxiNetworkSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UTaxiNetworkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTaxiNetworkSubsystem, STATGROUP_Tickables);
}

void UTaxiNetworkSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	MonitorTimer -= DeltaTime;
	if (MonitorTimer <= 0.f)
	{
		MonitorTimer = 0.25f;
		CheckOverlaps();
	}
	DeadlockTimer -= DeltaTime;
	if (DeadlockTimer <= 0.f)
	{
		DeadlockTimer = 2.f;
		CheckDeadlocks();
	}
	if (bDrawClaims)
	{
		DrawClaims();
	}
}

float UTaxiNetworkSubsystem::GetNow() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

int32 UTaxiNetworkSubsystem::FindTrack(const ATaxiRoute* Route) const
{
	const int32* Index = Route ? TrackOf.Find(Route) : nullptr;
	return Index ? *Index : INDEX_NONE;
}

int32 UTaxiNetworkSubsystem::LocalSample(const FTrack& Track, float Cm) const
{
	// Clamped as a float first: BIG_NUMBER (no limit) does not fit in an integer
	return FMath::Clamp(FMath::FloorToInt(FMath::Clamp(Cm / StepCm, -1.e6f, 1.e6f)) + OutsideSamples, 0, Track.Num - 1);
}

float UTaxiNetworkSubsystem::SampleCm(int32 Local) const
{
	return (Local - OutsideSamples) * StepCm;
}

int32 UTaxiNetworkSubsystem::Relation(int32 Sample, int32 Track) const
{
	for (int32 Entry = CoincidentStart[Sample]; Entry < CoincidentStart[Sample + 1]; ++Entry)
	{
		if (Coincident[Entry].Track == Track)
		{
			return Coincident[Entry].bOpposite ? 1 : 0;
		}
	}
	return -1;
}

FString UTaxiNetworkSubsystem::NameOf(const TObjectKey<AActor>& Key) const
{
	const AActor* Actor = Key.ResolveObjectPtr();
	if (!Actor)
	{
		return TEXT("(gone)");
	}
	return Actor->Implements<UAircraftInfo>() ? IAircraftInfo::Execute_GetCallsign(Actor).ToString() : Actor->GetName();
}

void UTaxiNetworkSubsystem::EnsureBuilt()
{
	if (bDirty)
	{
		Build();
	}
}

void UTaxiNetworkSubsystem::Build()
{
	const double StartSeconds = FPlatformTime::Seconds();

	// Claims survive a rebuild as stretches of their route
	struct FKept
	{
		TObjectKey<AActor> Key;
		FClaim Claim;
		TWeakObjectPtr<ATaxiRoute> Route;
		float FromCm = 0.f;
		float ToCm = 0.f;
	};
	TArray<FKept> Kept;
	for (const TPair<TObjectKey<AActor>, FClaim>& Pair : Claims)
	{
		FKept& Entry = Kept.Add_GetRef({ Pair.Key, Pair.Value });
		if (Tracks.IsValidIndex(Pair.Value.Track) && Pair.Value.Last >= Pair.Value.First)
		{
			Entry.Route = Tracks[Pair.Value.Track].Route;
			Entry.FromCm = SampleCm(Pair.Value.First);
			Entry.ToCm = SampleCm(Pair.Value.Last);
		}
	}
	Samples.Reset();
	SampleTrack.Reset();
	SampleFlags.Reset();
	CoincidentStart.Reset();
	Coincident.Reset();
	Tracks.Reset();
	TwoWays.Reset();
	Crossings.Reset();
	TrackOf.Reset();
	SampleGrid.Reset();
	HeldGrid.Reset();
	Claims.Reset();
	SideCache.Reset();
	TwoWaySamples = 0;
	CrossingSamples = 0;
	ClearanceCm = GetDefault<UATCSimSettings>()->WingtipClearanceM * ATCUnits::MetersToCm;
	HeadStartSeconds = GetDefault<UATCSimSettings>()->LandingHeadStartSeconds;
	bDirty = false;

	// Every route sampled every 5 m, also a little before its start and past its end
	UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>();
	TArray<ATaxiRoute*> Routes = Traffic ? Traffic->GetAllRoutes() : TArray<ATaxiRoute*>();
	Routes.Sort([](const ATaxiRoute& A, const ATaxiRoute& B) { return A.GetName() < B.GetName(); });
	for (ATaxiRoute* Route : Routes)
	{
		const float Length = Route->GetLength();
		if (Length < StepCm)
		{
			continue;
		}
		const int32 TrackIndex = Tracks.Num();
		FTrack& Track = Tracks.AddDefaulted_GetRef();
		Track.Route = Route;
		Track.First = Samples.Num();
		Track.Num = 2 * OutsideSamples + FMath::FloorToInt(Length / StepCm) + 1;
		TrackOf.Add(Route, TrackIndex);
		for (int32 Local = 0; Local < Track.Num; ++Local)
		{
			const float Cm = SampleCm(Local);
			FSample& Sample = Samples.AddDefaulted_GetRef();
			Sample.Location = Route->GetLocationAt(Cm);
			// From two points: the spline tangent at the very first point of a route can be zero
			Sample.Direction = (Route->GetLocationAt(Cm + 100.f) - Route->GetLocationAt(Cm - 100.f)).GetSafeNormal2D();
			SampleTrack.Add(TrackIndex);
			SampleGrid.FindOrAdd(CellOf(Sample.Location, GridCm)).Add(Samples.Num() - 1);
		}
	}

	// Which other routes pass through each sample, and which way: one entry per route, its closest sample
	CoincidentStart.SetNumUninitialized(Samples.Num() + 1);
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		CoincidentStart[Index] = Coincident.Num();
		const FSample& Mine = Samples[Index];
		const int32 MyTrack = SampleTrack[Index];
		const int32 Begin = Coincident.Num();
		const FIntPoint Center = CellOf(Mine.Location, GridCm);
		for (int32 X = -1; X <= 1; ++X)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				const TArray<int32>* Cell = SampleGrid.Find(Center + FIntPoint(X, Y));
				for (int32 Other : Cell ? *Cell : TArray<int32>())
				{
					const int32 OtherTrack = SampleTrack[Other];
					const float Dot = Mine.Direction | Samples[Other].Direction;
					const float DistanceSquared = FVector::DistSquared2D(Mine.Location, Samples[Other].Location);
					if (OtherTrack == MyTrack || DistanceSquared > CoincideCm * CoincideCm || FMath::Abs(Dot) < 0.9f)
					{
						continue;
					}
					const int32 Local = Other - Tracks[OtherTrack].First;
					int32 Existing = INDEX_NONE;
					for (int32 Entry = Begin; Entry < Coincident.Num(); ++Entry)
					{
						Existing = Coincident[Entry].Track == OtherTrack ? Entry : Existing;
					}
					if (Existing == INDEX_NONE)
					{
						Coincident.Add({ OtherTrack, Local, Dot < 0.f });
					}
					else if (DistanceSquared < FVector::DistSquared2D(Mine.Location, Samples[Tracks[OtherTrack].First + Coincident[Existing].Local].Location))
					{
						Coincident[Existing].Local = Local;
						Coincident[Existing].bOpposite = Dot < 0.f;
					}
				}
			}
		}
	}
	CoincidentStart[Samples.Num()] = Coincident.Num();

	// Two-way: another route close by the other way. Crossing: another taxiway at an angle within 50 m. Per coarse cell
	// which directions pass, so stretches with only same-way traffic are skipped quickly
	TMap<FIntPoint, uint16> CellDirections;
	TMap<FIntPoint, TArray<int32>> CoarseGrid;
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const FIntPoint Cell = CellOf(Samples[Index].Location, CoarseCm);
		CellDirections.FindOrAdd(Cell) |= uint16(1 << DirectionBin(Samples[Index].Direction));
		CoarseGrid.FindOrAdd(Cell).Add(Index);
	}
	TArray<uint8> Flags;
	Flags.SetNumZeroed(Samples.Num());
	const float CrossCos = FMath::Cos(FMath::DegreesToRadians(10.f));
	auto Find = [&](int32 Index, float Radius, uint16 Wanted, TFunctionRef<bool(int32)> Accept)
	{
		const int32 Range = FMath::CeilToInt(Radius / CoarseCm);
		const FIntPoint Center = CellOf(Samples[Index].Location, CoarseCm);
		for (int32 X = -Range; X <= Range; ++X)
		{
			for (int32 Y = -Range; Y <= Range; ++Y)
			{
				const FIntPoint Key = Center + FIntPoint(X, Y);
				const uint16* Directions = CellDirections.Find(Key);
				if (!Directions || !(*Directions & Wanted))
				{
					continue;
				}
				for (int32 Other : CoarseGrid.FindChecked(Key))
				{
					if (SampleTrack[Other] != SampleTrack[Index] && FVector::DistSquared2D(Samples[Index].Location, Samples[Other].Location) < Radius * Radius
						&& Accept(Other))
					{
						return true;
					}
				}
			}
		}
		return false;
	};
	// Per two-way sample the nearest sample of every route that runs the other way close by
	TArray<TArray<FOppositePair>> PairsOf;
	PairsOf.SetNum(Samples.Num());
	const int32 TwoWayRange = FMath::CeilToInt(TwoWayRadiusCm / CoarseCm);
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const FSample& Mine = Samples[Index];
		const int32 MyTrack = SampleTrack[Index];
		const int32 Bin = DirectionBin(Mine.Direction);
		const FIntPoint Center = CellOf(Mine.Location, CoarseCm);
		TArray<FOppositePair>& Pairs = PairsOf[Index];
		TArray<float, TInlineAllocator<8>> PairDistance;
		for (int32 X = -TwoWayRange; X <= TwoWayRange; ++X)
		{
			for (int32 Y = -TwoWayRange; Y <= TwoWayRange; ++Y)
			{
				const FIntPoint Key = Center + FIntPoint(X, Y);
				const uint16* Directions = CellDirections.Find(Key);
				if (!Directions || !(*Directions & BinsAround(Bin, 8)))
				{
					continue;
				}
				for (int32 Other : CoarseGrid.FindChecked(Key))
				{
					const float DistanceSquared = FVector::DistSquared2D(Mine.Location, Samples[Other].Location);
					const float Dot = Mine.Direction | Samples[Other].Direction;
					if (SampleTrack[Other] == MyTrack || DistanceSquared > TwoWayRadiusCm * TwoWayRadiusCm
						|| Dot > (DistanceSquared < CoincideCm * CoincideCm ? SameLaneDot : ParallelDot))
					{
						continue;
					}
					const int32 Existing = Pairs.IndexOfByPredicate([&](const FOppositePair& Pair) { return Pair.OtherTrack == SampleTrack[Other]; });
					if (Existing == INDEX_NONE)
					{
						Pairs.Add({ Index - Tracks[MyTrack].First, SampleTrack[Other], Other - Tracks[SampleTrack[Other]].First });
						PairDistance.Add(DistanceSquared);
					}
					else if (DistanceSquared < PairDistance[Existing])
					{
						Pairs[Existing].OtherLocal = Other - Tracks[SampleTrack[Other]].First;
						PairDistance[Existing] = DistanceSquared;
					}
				}
			}
		}
		// Another taxiway at more than 10 degrees: a crossing, or a junction where two come together. A route that bends
		// away from this one where it still runs through it is a shared curve, not a crossing
		const bool bCrossing = Find(Index, CrossingRadiusCm, uint16(~((1 << Bin) | (1 << ((Bin + 8) % 16)))),
			[&](int32 Other) { return FMath::Abs(Mine.Direction | Samples[Other].Direction) < CrossCos && Relation(Other, MyTrack) < 0; });
		Flags[Index] = (Pairs.Num() > 0 ? 1 : 0) | (bCrossing ? 2 : 0);
		TwoWaySamples += Pairs.Num() > 0 ? 1 : 0;
		CrossingSamples += bCrossing ? 1 : 0;
	}
	SampleFlags = Flags;

	// Per track its two-way stretches and crossings, short gaps bridged
	for (int32 TrackIndex = 0; TrackIndex < Tracks.Num(); ++TrackIndex)
	{
		FTrack& Track = Tracks[TrackIndex];
		auto Collect = [&](uint8 Flag, int32 Bridge, TArray<int32>& Index, TArray<FStretch>& Into)
		{
			TArray<FStretch> Found;
			int32 Previous = INDEX_NONE;
			for (int32 Local = 0; Local < Track.Num; ++Local)
			{
				if (Flags[Track.First + Local] & Flag)
				{
					if (Found.Num() == 0 || Local - Previous > Bridge + 1)
					{
						Found.Add({ Local, Local });
					}
					Found.Last().Last = Local;
					Previous = Local;
				}
			}
			Index.Init(INDEX_NONE, Track.Num);
			for (const FStretch& Stretch : Found)
			{
				for (int32 Local = Stretch.First; Local <= Stretch.Last; ++Local)
				{
					Index[Local] = Into.Num();
				}
				Into.Add(Stretch);
			}
		};
		const int32 FirstTwoWay = TwoWays.Num();
		Collect(1, TwoWayBridgeSamples, Track.TwoWay, TwoWays);
		Collect(2, CrossingBridgeSamples, Track.Crossing, Crossings);
		for (int32 Index = FirstTwoWay; Index < TwoWays.Num(); ++Index)
		{
			for (int32 Local = TwoWays[Index].First; Local <= TwoWays[Index].Last; ++Local)
			{
				TwoWays[Index].Pairs.Append(PairsOf[Track.First + Local]);
			}
		}

		// The wait for a two-way stretch is 100 m before it, but not behind a stop that needs a clearance, nor in the stretch before
		TArray<int32> ClearanceStops;
		for (const FTaxiStop& Stop : Track.Route->GetAllStops())
		{
			if (Stop.bRequiresClearance)
			{
				ClearanceStops.Add(LocalSample(Track, Stop.Distance));
			}
		}
		for (int32 Index = FirstTwoWay; Index < TwoWays.Num(); ++Index)
		{
			FStretch& Stretch = TwoWays[Index];
			int32 Start = FMath::Max(Stretch.First - ApproachSamples, Index > FirstTwoWay ? TwoWays[Index - 1].Last + 1 : 0);
			for (int32 Stop : ClearanceStops)
			{
				Start = Stop < Stretch.First ? FMath::Max(Start, FMath::Min(Stop + 4, Stretch.First)) : Start;
			}
			for (int32 Local = Start; Local < Stretch.First; ++Local)
			{
				Track.TwoWay[Local] = Index;
			}
			Stretch.First = Start;
		}
	}

	// The claims back, on the new samples of their routes
	for (const FKept& Entry : Kept)
	{
		FClaim Claim = Entry.Claim;
		Claim.Track = FindTrack(Entry.Route.Get());
		Claim.First = Claim.Track != INDEX_NONE ? LocalSample(Tracks[Claim.Track], Entry.FromCm) : 0;
		Claim.Last = Claim.Track != INDEX_NONE ? LocalSample(Tracks[Claim.Track], Entry.ToCm) : -1;
		Claim.WaitingSince = -1.f;
		Claim.WantedTrack = INDEX_NONE;
		Claim.WantedTwoWay = INDEX_NONE;
		Claim.Locks.Reset();
		Claim.Reserved.Reset();
		FClaim& Restored = Claims.Add(Entry.Key, Claim);
		for (int32 Local = Restored.First; Restored.Track != INDEX_NONE && Local <= Restored.Last; ++Local)
		{
			Hold(Entry.Key, Tracks[Restored.Track].First + Local);
		}
		LockUnder(Restored, Restored.First, Restored.Last);
	}

	UE_LOG(LogATC, Display, TEXT("Taxi network: %d routes, %d samples, %.1f km two-way in %d stretches, %.1f km at %d crossings, built in %.0f ms"),
		Tracks.Num(), Samples.Num(), TwoWaySamples * StepCm / 100000.f, TwoWays.Num(), CrossingSamples * StepCm / 100000.f, Crossings.Num(),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

bool UTaxiNetworkSubsystem::Conflicts(int32 Mine, float MyRadius, int32 Theirs, float TheirRadius) const
{
	const int32 MyTrack = SampleTrack[Mine];
	const int32 TheirTrack = SampleTrack[Theirs];
	const float DistanceSquared = FVector::DistSquared2D(Samples[Mine].Location, Samples[Theirs].Location);
	// The same piece of taxiway (the same route, or one that runs through the other's place): the claims hold the
	// bodies along the line, so only the same place conflicts. In the same direction too: a follower stops claiming
	// just behind the one ahead and never takes a crossing ahead of it
	if (MyTrack == TheirTrack || (Relation(Mine, TheirTrack) >= 0 && Relation(Theirs, MyTrack) >= 0))
	{
		return DistanceSquared < LaneGapCm * LaneGapCm;
	}
	// Another taxiway: the wings (or nose or tail) must stay apart
	const float Reach = MyRadius + TheirRadius + ClearanceCm;
	return DistanceSquared < Reach * Reach;
}

float UTaxiNetworkSubsystem::QueueSince(const FClaim& Claim) const
{
	return (Claim.WaitingSince >= 0.f ? Claim.WaitingSince : GetNow()) - (Claim.bPriority ? HeadStartSeconds : 0.f);
}

float UTaxiNetworkSubsystem::RadiusAt(const FClaim& Claim, int32 Local) const
{
	if (Local > Claim.Last - Claim.FrontNarrow)
	{
		return Claim.FrontRadiusCm;
	}
	return Local < Claim.First + Claim.RearNarrow ? Claim.RearRadiusCm : Claim.HalfSpanCm;
}

bool UTaxiNetworkSubsystem::IsFree(const TObjectKey<AActor>& Me, const FClaim* MyClaim, int32 Sample, float Radius, TObjectKey<AActor>& OutBlocker) const
{
	const FSample& Mine = Samples[Sample];
	const float Reach = Radius + MaxHalfSpanCm + ClearanceCm;
	const int32 Range = FMath::CeilToInt(Reach / HeldCellCm);
	const FIntPoint Center = CellOf(Mine.Location, HeldCellCm);
	for (int32 X = -Range; X <= Range; ++X)
	{
		for (int32 Y = -Range; Y <= Range; ++Y)
		{
			const TArray<FHeld>* Held = HeldGrid.Find(Center + FIntPoint(X, Y));
			for (const FHeld& Entry : Held ? *Held : TArray<FHeld>())
			{
				const FClaim* Other = Entry.Owner != Me ? Claims.Find(Entry.Owner) : nullptr;
				if (Other && Conflicts(Sample, Radius, Entry.Sample, RadiusAt(*Other, Entry.Sample - Tracks[Other->Track].First)))
				{
					OutBlocker = Entry.Owner;
					return false;
				}
			}
		}
	}

	// The queue: whoever has waited longer for a piece of taxiway that conflicts with this one goes first (a landing
	// with its head start), unless it is (through one other) waiting for this aircraft itself. A wait for a two-way
	// stretch only holds up the other direction (IsTwoWayFree)
	const float MySince = MyClaim ? QueueSince(*MyClaim) : GetNow();
	for (const TPair<TObjectKey<AActor>, FClaim>& Pair : Claims)
	{
		const FClaim& Other = Pair.Value;
		if (Pair.Key == Me || Other.WaitingSince < 0.f || QueueSince(Other) >= MySince || !Tracks.IsValidIndex(Other.WantedTrack)
			|| Other.WantedTwoWay != INDEX_NONE || Other.Blocker == Me || Other.WantedBox.ComputeSquaredDistanceToPoint(Mine.Location) > Reach * Reach)
		{
			continue;
		}
		const FClaim* Behind = Claims.Find(Other.Blocker);
		if (Behind && Behind->WaitingSince >= 0.f && Behind->Blocker == Me)
		{
			continue;
		}
		const int32 Base = Tracks[Other.WantedTrack].First;
		for (int32 Local = Other.WantedFirst; Local <= Other.WantedLast; ++Local)
		{
			if (Conflicts(Sample, Radius, Base + Local, Other.HalfSpanCm))
			{
				OutBlocker = Pair.Key;
				return false;
			}
		}
	}
	return true;
}

bool UTaxiNetworkSubsystem::Meet(const FStretch& A, int32 ATrack, int32 FromA, float HalfSpanA, const FStretch& B, int32 BTrack, int32 FromB,
	float HalfSpanB) const
{
	const float Reach = HalfSpanA + HalfSpanB + ClearanceCm;
	for (const FOppositePair& Pair : A.Pairs)
	{
		if (Pair.OtherTrack == BTrack && Pair.Local >= FromA && Pair.OtherLocal >= FromB && Pair.OtherLocal <= B.Last
			&& FVector::DistSquared2D(Samples[Tracks[ATrack].First + Pair.Local].Location, Samples[Tracks[BTrack].First + Pair.OtherLocal].Location) < Reach * Reach)
		{
			return true;
		}
	}
	return false;
}

bool UTaxiNetworkSubsystem::IsTwoWayFree(const TObjectKey<AActor>& Me, const FClaim& MyClaim, int32 Track, int32 TwoWay, int32 From,
	TObjectKey<AActor>& OutBlocker) const
{
	const FStretch& Mine = TwoWays[TwoWay];
	const float MySince = QueueSince(MyClaim);
	for (const TPair<TObjectKey<AActor>, FClaim>& Pair : Claims)
	{
		const FClaim& Other = Pair.Value;
		if (Pair.Key == Me)
		{
			continue;
		}
		// In an opposite stretch, with a place close to this one still in front of its tail; or about to be (reserved)
		for (const int32 Locked : Other.Locks)
		{
			if (Meet(TwoWays[Locked], Other.Track, Other.First, Other.HalfSpanCm, Mine, Track, From, MyClaim.HalfSpanCm))
			{
				OutBlocker = Pair.Key;
				return false;
			}
		}
		for (const FClaim::FReserved& Ahead : Other.Reserved)
		{
			if (Meet(TwoWays[Ahead.TwoWay], Ahead.Track, Ahead.From, Other.HalfSpanCm, Mine, Track, From, MyClaim.HalfSpanCm))
			{
				OutBlocker = Pair.Key;
				return false;
			}
		}
		// Waiting longer for an opposite stretch (a landing with its head start): it goes first, unless it waits
		// (through one other) for this aircraft
		const FClaim* Behind = Claims.Find(Other.Blocker);
		if (Other.WantedTwoWay != INDEX_NONE && Other.WaitingSince >= 0.f && QueueSince(Other) < MySince && Other.Blocker != Me
			&& !(Behind && Behind->WaitingSince >= 0.f && Behind->Blocker == Me)
			&& Meet(TwoWays[Other.WantedTwoWay], Other.WantedTrack, Other.WantedFirst, Other.HalfSpanCm, Mine, Track, From, MyClaim.HalfSpanCm))
		{
			OutBlocker = Pair.Key;
			return false;
		}
	}
	return true;
}

void UTaxiNetworkSubsystem::LockUnder(FClaim& Claim, int32 First, int32 Last)
{
	for (int32 Local = FMath::Max(First, 0); Tracks.IsValidIndex(Claim.Track) && Local <= Last && Local < Tracks[Claim.Track].Num; ++Local)
	{
		if (Tracks[Claim.Track].TwoWay[Local] != INDEX_NONE)
		{
			Claim.Locks.AddUnique(Tracks[Claim.Track].TwoWay[Local]);
		}
	}
}

void UTaxiNetworkSubsystem::SetShape(FClaim& Claim, const FTaxiClaimRequest& Request)
{
	// Wing root about 10 % of the length ahead of the main gear, wingtips swept back about 12 % of the span behind it;
	// ahead of and behind the wings only the fuselage (nose) or the tailplane (tail)
	const float Span = Request.HalfSpanCm * 2.f;
	const float NoseOnly = FMath::Max(Request.NoseOffsetCm - 0.1f * Request.LengthCm, 0.f);
	const float TailOnly = FMath::Max(Request.LengthCm - Request.NoseOffsetCm - 0.12f * Span, 0.f);
	const float NoseRadius = FMath::Max(0.05f * Request.LengthCm, 200.f);
	const float TailRadius = FMath::Max(0.18f * Span, NoseRadius);
	Claim.HalfSpanCm = Request.HalfSpanCm;
	Claim.FrontNarrow = FMath::FloorToInt((Request.bTailFirst ? TailOnly : NoseOnly) / StepCm);
	Claim.FrontRadiusCm = Request.bTailFirst ? TailRadius : NoseRadius;
	Claim.RearNarrow = FMath::FloorToInt(((Request.bTailFirst ? NoseOnly : TailOnly) + TailMarginCm) / StepCm);
	Claim.RearRadiusCm = Request.bTailFirst ? NoseRadius : TailRadius;
	MaxHalfSpanCm = FMath::Max(MaxHalfSpanCm, Request.HalfSpanCm);
}

void UTaxiNetworkSubsystem::Hold(const TObjectKey<AActor>& Owner, int32 Sample)
{
	HeldGrid.FindOrAdd(CellOf(Samples[Sample].Location, HeldCellCm)).Add({ Owner, Sample });
}

void UTaxiNetworkSubsystem::Unhold(const TObjectKey<AActor>& Owner, int32 Sample)
{
	if (TArray<FHeld>* Held = HeldGrid.Find(CellOf(Samples[Sample].Location, HeldCellCm)))
	{
		const int32 Index = Held->IndexOfByPredicate([&Owner, Sample](const FHeld& Entry) { return Entry.Owner == Owner && Entry.Sample == Sample; });
		if (Index != INDEX_NONE)
		{
			Held->RemoveAtSwap(Index);
		}
	}
}

void UTaxiNetworkSubsystem::ClearHeld(const TObjectKey<AActor>& Owner, FClaim& Claim)
{
	for (int32 Local = Claim.First; Tracks.IsValidIndex(Claim.Track) && Local <= Claim.Last; ++Local)
	{
		Unhold(Owner, Tracks[Claim.Track].First + Local);
	}
	Claim.Track = INDEX_NONE;
	Claim.First = 0;
	Claim.Last = -1;
}

void UTaxiNetworkSubsystem::SetWaiting(FClaim& Claim, int32 Track, int32 First, int32 Last, int32 TwoWay, const TObjectKey<AActor>& Blocker)
{
	// The place in the queue is kept while it asks for the same piece; something else ahead is a new wait
	if (Claim.WaitingSince < 0.f || Claim.WantedTrack != Track || Claim.WantedFirst != First)
	{
		Claim.WaitingSince = GetNow();
	}
	Claim.WantedTrack = Track;
	Claim.WantedFirst = First;
	Claim.WantedLast = Last;
	Claim.WantedTwoWay = TwoWay;
	Claim.Blocker = Blocker;
	Claim.WantedBox = FBox(ForceInit);
	for (int32 Local = First; Local <= Last; ++Local)
	{
		Claim.WantedBox += Samples[Tracks[Track].First + Local].Location;
	}
}

float UTaxiNetworkSubsystem::UpdateClaim(const AActor* Aircraft, const FTaxiClaimRequest& Request, AActor*& OutBlocker)
{
	OutBlocker = nullptr;
	EnsureBuilt();
	const int32 TrackIndex = FindTrack(Request.Route);
	if (!Aircraft || TrackIndex == INDEX_NONE)
	{
		return BIG_NUMBER;   // a route outside the network: nothing to reserve
	}
	const TObjectKey<AActor> Key(Aircraft);
	FClaim& Claim = Claims.FindOrAdd(Key);
	Claim.Aircraft = Aircraft;
	SetShape(Claim, Request);
	Claim.bPriority = Request.bPriority;
	const FTrack& Track = Tracks[TrackIndex];

	// A new route (after a pushback, a landing or a lane change): the aircraft is already there, so its body is claimed as it is
	const int32 Rear = LocalSample(Track, Request.RearCm - TailMarginCm);
	const int32 Front = FMath::Min(LocalSample(Track, Request.FrontCm) + 1, Track.Num - 1);
	const bool bNewTrack = Claim.Track != TrackIndex;
	if (bNewTrack)
	{
		ClearHeld(Key, Claim);
		Claim.Locks.Reset();
		Claim.Track = TrackIndex;
		Claim.First = Rear;
		Claim.Last = Rear - 1;
	}
	// Behind the tail: free for the others again, also the two-way stretches it has left
	while (Claim.First < Rear && Claim.First <= Claim.Last)
	{
		Unhold(Key, Track.First + Claim.First++);
	}
	if (Claim.Last < Claim.First)
	{
		Claim.First = Rear;
		Claim.Last = Rear - 1;
	}
	Claim.Locks.RemoveAll([this, &Claim](int32 Locked) { return TwoWays[Locked].Last < Claim.First; });
	// Under the body: always, it is there
	while (Claim.Last < Front)
	{
		Hold(Key, Track.First + ++Claim.Last);
	}
	if (bNewTrack)
	{
		// Where it stands it has the right of way; what it reserved on this route before (after a pushback) is now its lock
		LockUnder(Claim, Claim.First, Claim.Last);
		for (const FClaim::FReserved& Ahead : Claim.Reserved)
		{
			if (Ahead.Track == TrackIndex)
			{
				Claim.Locks.AddUnique(Ahead.TwoWay);
			}
		}
		Claim.Reserved.Reset();
	}

	// Ahead, piece by piece, up to the sample at or just past the wanted point (and never past the limit)
	const int32 Body = FMath::CeilToInt((Request.LengthCm + TailMarginCm) / StepCm);
	const int32 LimitLocal = FMath::Min(LocalSample(Track, Request.LimitCm) + 1, Track.Num - 1);
	const int32 TargetLocal = FMath::Min(LocalSample(Track, Request.WantedCm) + 1, LimitLocal);
	TObjectKey<AActor> Blocker;
	bool bWaiting = false;
	for (int32 Next = Claim.Last + 1; Claim.BackOffUntil < GetNow() && Next <= TargetLocal;)
	{
		// Into a two-way stretch only when nobody who will come by the other way is in it or waited for it longer;
		// then it is locked: whoever wants in at the other end waits until this aircraft has passed
		const int32 TwoWay = Track.TwoWay[Next];
		if (TwoWay != INDEX_NONE && !Claim.Locks.Contains(TwoWay))
		{
			if (!IsTwoWayFree(Key, Claim, TrackIndex, TwoWay, Next, Blocker))
			{
				SetWaiting(Claim, TrackIndex, Next, TwoWays[TwoWay].Last, TwoWay, Blocker);
				bWaiting = true;
				break;
			}
			Claim.Locks.Add(TwoWay);
		}
		// A crossing in one go, until the tail is out of it, but not past a stop that waits for a clearance (the runway
		// behind it is the controller's), and only when the aircraft ahead in the same direction leaves room for all of it
		int32 End = Next;
		if (Track.Crossing[Next] != INDEX_NONE)
		{
			for (int32 Local = Next; Local <= End; ++Local)
			{
				if (Local > LimitLocal)
				{
					End = Local - 1;
					break;
				}
				if (Track.Crossing[Local] != INDEX_NONE)
				{
					End = FMath::Max(End, FMath::Min(Crossings[Track.Crossing[Local]].Last + Body, Track.Num - 1));
				}
			}
			if (End < Next || SampleCm(End) - Request.FrontCm > Request.RoomAheadCm)
			{
				break;
			}
		}
		// From where the wings will now come (the old nose part) to the new nose
		bool bFree = true;
		for (int32 Local = FMath::Max(Claim.First, Claim.Last - Claim.FrontNarrow + 1); Local <= End && bFree; ++Local)
		{
			const float Radius = Local > End - Claim.FrontNarrow ? Claim.FrontRadiusCm : Claim.HalfSpanCm;
			bFree = IsFree(Key, &Claim, Track.First + Local, Radius, Blocker);
		}
		if (!bFree)
		{
			SetWaiting(Claim, TrackIndex, Next, End, INDEX_NONE, Blocker);
			bWaiting = true;
			break;
		}
		for (int32 Local = Next; Local <= End; ++Local)
		{
			Hold(Key, Track.First + Local);
		}
		Claim.Last = End;
		Next = End + 1;
	}
	if (!bWaiting)
	{
		// Standing behind the aircraft ahead is a wait for the deadlock check, but not a place in the queue
		const TObjectKey<AActor> HeldBy(Request.HeldBy);
		Claim.WaitingSince = !Request.HeldBy ? -1.f
			: (Claim.WaitingSince < 0.f || Claim.WantedTrack != INDEX_NONE || Claim.Blocker != HeldBy ? GetNow() : Claim.WaitingSince);
		Claim.WantedTrack = INDEX_NONE;
		Claim.WantedTwoWay = INDEX_NONE;
		Claim.Blocker = HeldBy;
	}
	OutBlocker = bWaiting ? Blocker.ResolveObjectPtr() : nullptr;
	// Up to the stop or the end of the taxi: from there on the taxi controller's own braking for it is the limit
	return Claim.Last >= LimitLocal ? BIG_NUMBER : SampleCm(Claim.Last);
}

bool UTaxiNetworkSubsystem::ClaimStretch(const AActor* Aircraft, const FTaxiClaimRequest& Request, AActor*& OutBlocker, bool bCheckOnly)
{
	OutBlocker = nullptr;
	EnsureBuilt();
	const int32 TrackIndex = FindTrack(Request.Route);
	if ((!Aircraft && !bCheckOnly) || TrackIndex == INDEX_NONE)
	{
		return true;
	}
	const FTrack& Track = Tracks[TrackIndex];
	const TObjectKey<AActor> Key(Aircraft);
	// A check works on a copy: nothing changes
	const FClaim* Known = Aircraft ? Claims.Find(Key) : nullptr;
	FClaim Copy = Known ? *Known : FClaim();
	FClaim& Claim = bCheckOnly ? Copy : Claims.FindOrAdd(Key);
	Claim.Aircraft = Aircraft;
	SetShape(Claim, Request);
	Claim.bPriority = Request.bPriority;
	const int32 First = LocalSample(Track, Request.RearCm);
	const int32 Last = FMath::Min(LocalSample(Track, Request.FrontCm) + 1, Track.Num - 1);
	TObjectKey<AActor> Blocker;
	int32 Checked = INDEX_NONE;
	for (int32 Local = First; Local <= Last; ++Local)
	{
		// Each two-way stretch on the way like in UpdateClaim, then every place itself
		const int32 TwoWay = Track.TwoWay[Local];
		const bool bTwoWayFree = TwoWay == INDEX_NONE || TwoWay == Checked || (Claim.Track == TrackIndex && Claim.Locks.Contains(TwoWay))
			|| IsTwoWayFree(Key, Claim, TrackIndex, TwoWay, Local, Blocker);
		Checked = TwoWay;
		if (!bTwoWayFree || !IsFree(Key, &Claim, Track.First + Local, Request.HalfSpanCm, Blocker))
		{
			if (!bCheckOnly)
			{
				SetWaiting(Claim, TrackIndex, First, Last, bTwoWayFree ? INDEX_NONE : TwoWay, Blocker);
			}
			OutBlocker = Blocker.ResolveObjectPtr();
			return false;
		}
	}
	if (bCheckOnly)
	{
		return true;
	}
	ClearHeld(Key, Claim);
	Claim.Locks.Reset();
	Claim.Track = TrackIndex;
	Claim.First = First;
	Claim.Last = Last;
	for (int32 Local = First; Local <= Last; ++Local)
	{
		Hold(Key, Track.First + Local);
	}
	LockUnder(Claim, First, Last);
	Claim.WaitingSince = -1.f;
	Claim.WantedTrack = INDEX_NONE;
	Claim.WantedTwoWay = INDEX_NONE;
	Claim.Blocker = TObjectKey<AActor>();
	return true;
}

bool UTaxiNetworkSubsystem::ReserveTwoWay(const AActor* Aircraft, const FTaxiClaimRequest& Request, AActor*& OutBlocker, bool bCheckOnly)
{
	OutBlocker = nullptr;
	EnsureBuilt();
	const int32 TrackIndex = FindTrack(Request.Route);
	if ((!Aircraft && !bCheckOnly) || TrackIndex == INDEX_NONE)
	{
		return true;
	}
	const FTrack& Track = Tracks[TrackIndex];
	const TObjectKey<AActor> Key(Aircraft);
	const FClaim* Known = Aircraft ? Claims.Find(Key) : nullptr;
	FClaim Copy = Known ? *Known : FClaim();
	FClaim& Claim = bCheckOnly ? Copy : Claims.FindOrAdd(Key);
	Claim.Aircraft = Aircraft;
	SetShape(Claim, Request);
	TArray<FClaim::FReserved, TInlineAllocator<2>> Found;
	for (int32 Local = LocalSample(Track, Request.RearCm); Local <= FMath::Min(LocalSample(Track, Request.FrontCm) + 1, Track.Num - 1); ++Local)
	{
		const int32 TwoWay = Track.TwoWay[Local];
		if (TwoWay == INDEX_NONE || Found.ContainsByPredicate([TwoWay](const FClaim::FReserved& Entry) { return Entry.TwoWay == TwoWay; }))
		{
			continue;
		}
		TObjectKey<AActor> Blocker;
		if (!IsTwoWayFree(Key, Claim, TrackIndex, TwoWay, Local, Blocker))
		{
			if (!bCheckOnly)
			{
				SetWaiting(Claim, TrackIndex, Local, TwoWays[TwoWay].Last, TwoWay, Blocker);
			}
			OutBlocker = Blocker.ResolveObjectPtr();
			return false;
		}
		Found.Add({ TrackIndex, TwoWay, Local });
	}
	for (const FClaim::FReserved& Entry : Found)
	{
		if (!bCheckOnly && !Claim.Reserved.ContainsByPredicate([&Entry](const FClaim::FReserved& Held) { return Held.TwoWay == Entry.TwoWay; }))
		{
			Claim.Reserved.Add(Entry);
		}
	}
	return true;
}

void UTaxiNetworkSubsystem::CancelReservations(const AActor* Aircraft)
{
	if (FClaim* Claim = Claims.Find(TObjectKey<AActor>(Aircraft)))
	{
		Claim->Reserved.Reset();
	}
}

bool UTaxiNetworkSubsystem::HasClaim(const AActor* Aircraft) const
{
	const FClaim* Claim = Claims.Find(TObjectKey<AActor>(Aircraft));
	return Claim && Tracks.IsValidIndex(Claim->Track) && Claim->Last >= Claim->First;
}

void UTaxiNetworkSubsystem::ClaimBody(const AActor* Aircraft, const FTaxiClaimRequest& Request)
{
	EnsureBuilt();
	const int32 TrackIndex = FindTrack(Request.Route);
	if (!Aircraft || TrackIndex == INDEX_NONE)
	{
		return;
	}
	const TObjectKey<AActor> Key(Aircraft);
	FClaim& Claim = Claims.FindOrAdd(Key);
	ClearHeld(Key, Claim);
	Claim.Aircraft = Aircraft;
	SetShape(Claim, Request);
	Claim.Track = TrackIndex;
	Claim.First = LocalSample(Tracks[TrackIndex], Request.RearCm);
	Claim.Last = FMath::Min(LocalSample(Tracks[TrackIndex], Request.FrontCm) + 1, Tracks[TrackIndex].Num - 1);
	for (int32 Local = Claim.First; Local <= Claim.Last; ++Local)
	{
		Hold(Key, Tracks[TrackIndex].First + Local);
	}
	Claim.Locks.Reset();
	LockUnder(Claim, Claim.First, Claim.Last);
}

bool UTaxiNetworkSubsystem::IsStretchFree(ATaxiRoute* Route, float FromCm, float ToCm, float HalfSpanCm, AActor*& OutBlocker)
{
	OutBlocker = nullptr;
	EnsureBuilt();
	const int32 TrackIndex = FindTrack(Route);
	if (TrackIndex == INDEX_NONE)
	{
		return true;
	}
	const float KnownMax = MaxHalfSpanCm;
	MaxHalfSpanCm = FMath::Max(MaxHalfSpanCm, HalfSpanCm);
	const FTrack& Track = Tracks[TrackIndex];
	const int32 Last = FMath::Min(LocalSample(Track, ToCm) + 1, Track.Num - 1);
	TObjectKey<AActor> Blocker;
	bool bFree = true;
	for (int32 Local = LocalSample(Track, FromCm); Local <= Last && bFree; ++Local)
	{
		bFree = IsFree(TObjectKey<AActor>(), nullptr, Track.First + Local, HalfSpanCm, Blocker);
	}
	MaxHalfSpanCm = KnownMax;
	OutBlocker = bFree ? nullptr : Blocker.ResolveObjectPtr();
	return bFree;
}

void UTaxiNetworkSubsystem::ReleaseClaim(const AActor* Aircraft)
{
	const TObjectKey<AActor> Key(Aircraft);
	if (FClaim* Claim = Claims.Find(Key))
	{
		ClearHeld(Key, *Claim);
		Claims.Remove(Key);
	}
}

bool UTaxiNetworkSubsystem::GetClaim(const AActor* Aircraft, ATaxiRoute*& OutRoute, float& OutFromCm, float& OutToCm) const
{
	const FClaim* Claim = Claims.Find(TObjectKey<AActor>(Aircraft));
	if (!Claim || !Tracks.IsValidIndex(Claim->Track) || Claim->Last < Claim->First)
	{
		return false;
	}
	OutRoute = Tracks[Claim->Track].Route.Get();
	OutFromCm = SampleCm(Claim->First);
	OutToCm = SampleCm(Claim->Last);
	return OutRoute != nullptr;
}

int32 UTaxiNetworkSubsystem::SideOf(ATaxiRoute* Lane, ATaxiRoute* Other)
{
	const TPair<TObjectKey<ATaxiRoute>, TObjectKey<ATaxiRoute>> Key(Lane, Other);
	if (const int32* Known = SideCache.Find(Key))
	{
		return *Known;
	}
	const int32 LaneTrack = FindTrack(Lane);
	const int32 OtherTrack = FindTrack(Other);
	float Sum = 0.f;
	for (int32 Local = OutsideSamples; LaneTrack != INDEX_NONE && OtherTrack != INDEX_NONE && Local < Tracks[LaneTrack].Num - OutsideSamples; Local += 4)
	{
		// Only where the two lanes are apart, and not where the other one makes a big detour
		const int32 Sample = Tracks[LaneTrack].First + Local;
		if (Relation(Sample, OtherTrack) >= 0)
		{
			continue;
		}
		const FVector Offset = Other->Spline->FindLocationClosestToWorldLocation(Samples[Sample].Location, ESplineCoordinateSpace::World) - Samples[Sample].Location;
		if (Offset.SizeSquared2D() < FMath::Square(30000.f))
		{
			// Positive: the other lane lies right of the travel direction
			Sum += FMath::Sign(FVector::CrossProduct(Samples[Sample].Direction, Offset).Z);
		}
	}
	const int32 Side = Sum > 0.f ? 1 : (Sum < 0.f ? -1 : 0);
	SideCache.Add(Key, Side);
	return Side;
}

ATaxiRoute* UTaxiNetworkSubsystem::ChooseLane(const AActor* Aircraft, ATaxiRoute* Planned, const FVector& Start, FString* OutReason)
{
	if (!Planned || Planned->Alternatives.Num() == 0)
	{
		return Planned;
	}
	EnsureBuilt();
	TArray<ATaxiRoute*> Lanes = { Planned };
	for (ATaxiRoute* Alternative : Planned->Alternatives)
	{
		if (Alternative && FindTrack(Alternative) != INDEX_NONE)
		{
			Lanes.AddUnique(Alternative);
		}
	}
	if (Lanes.Num() < 2 || FindTrack(Planned) == INDEX_NONE)
	{
		return Planned;
	}

	// Where the other aircraft on the network are: per route the rear of their claim
	const TObjectKey<AActor> Me(Aircraft);
	TMap<int32, TArray<TPair<int32, TObjectKey<AActor>>>> OnTrack;
	for (const TPair<TObjectKey<AActor>, FClaim>& Pair : Claims)
	{
		if (Pair.Key != Me && Tracks.IsValidIndex(Pair.Value.Track) && Pair.Value.Last >= Pair.Value.First)
		{
			OnTrack.FindOrAdd(Pair.Value.Track).Add({ Pair.Value.First, Pair.Key });
		}
	}

	const bool bKeepRight = GetDefault<UATCSimSettings>()->bKeepRightOnParallelTaxiways;
	ATaxiRoute* Best = Planned;
	float BestCost = BIG_NUMBER;
	FString BestReason;
	for (ATaxiRoute* Lane : Lanes)
	{
		const int32 TrackIndex = FindTrack(Lane);
		const FTrack& Track = Tracks[TrackIndex];
		const float StartCm = UAircraftMovementComponent::GetDistanceClosestTo(Lane->Spline, Start);
		if (FVector::Dist2D(Lane->GetLocationAt(StartCm), Start) > 1500.f)
		{
			continue;   // this lane does not pass where the aircraft is
		}
		// Who will come the other way on this lane, and who is on it ahead in the same direction
		TSet<TObjectKey<AActor>> Opposing;
		TSet<TObjectKey<AActor>> Ahead;
		const int32 From = LocalSample(Track, StartCm);
		if (const TArray<TPair<int32, TObjectKey<AActor>>>* Here = OnTrack.Find(TrackIndex))
		{
			for (const TPair<int32, TObjectKey<AActor>>& Other : *Here)
			{
				if (Other.Key >= From)
				{
					Ahead.Add(Other.Value);
				}
			}
		}
		for (int32 Local = From; Local < Track.Num - OutsideSamples; ++Local)
		{
			const int32 Sample = Track.First + Local;
			for (int32 Entry = CoincidentStart[Sample]; Entry < CoincidentStart[Sample + 1]; ++Entry)
			{
				const FCoincident& Through = Coincident[Entry];
				const TArray<TPair<int32, TObjectKey<AActor>>>* There = OnTrack.Find(Through.Track);
				for (const TPair<int32, TObjectKey<AActor>>& Other : There ? *There : TArray<TPair<int32, TObjectKey<AActor>>>())
				{
					// Still before this place on its own route, so it will come by here. The same way it is only ahead
					// when it is closer to the place than this aircraft is
					const int32 ToGo = Through.Local - Other.Key;
					if (ToGo >= 0 && (Through.bOpposite || ToGo <= Local - From))
					{
						(Through.bOpposite ? Opposing : Ahead).Add(Other.Value);
					}
				}
			}
		}
		// The right-hand lane: every other lane lies to its left where they part
		bool bRightHand = bKeepRight;
		for (ATaxiRoute* Other : Lanes)
		{
			bRightHand &= Other == Lane || SideOf(Lane, Other) < 0;
		}
		const float Cost = (Lane->GetLength() - StartCm) / LaneSpeedCms + Opposing.Num() * OpposingSeconds + Ahead.Num() * AheadSeconds
			- (bRightHand ? KeepRightSeconds : 0.f);
		if (Cost < BestCost)
		{
			BestCost = Cost;
			Best = Lane;
			BestReason = FString::Printf(TEXT("%s (%d coming the other way, %d ahead%s)"), *Lane->RouteTag.ToString(), Opposing.Num(), Ahead.Num(),
				bRightHand ? TEXT(", right-hand lane") : TEXT(""));
		}
	}
	if (OutReason)
	{
		*OutReason = BestReason;
	}
	return Best;
}

TArray<FTaxiClaimRequest> UTaxiNetworkSubsystem::RunwayExitRequests(ATaxiRoute* Route, float StartCm, const UAircraftTypeData* Type)
{
	TArray<FTaxiClaimRequest> Requests;
	if (!Route || !Type)
	{
		return Requests;
	}
	const FVector Start = Route->GetLocationAt(StartCm);
	TArray<ATaxiRoute*> Lanes = { Route };
	for (ATaxiRoute* Alternative : Route->Alternatives)
	{
		if (Alternative && FVector::Dist2D(Alternative->GetLocationAt(UAircraftMovementComponent::GetDistanceClosestTo(Alternative->Spline, Start)), Start) < 1500.f)
		{
			Lanes.AddUnique(Alternative);
		}
	}
	for (ATaxiRoute* Lane : Lanes)
	{
		const float LaneStartCm = Lane == Route ? StartCm : UAircraftMovementComponent::GetDistanceClosestTo(Lane->Spline, Start);
		FTaxiClaimRequest& Exit = Requests.AddDefaulted_GetRef();
		Exit.Route = Lane;
		Exit.HalfSpanCm = Type->GetHalfSpanCm();
		Exit.LengthCm = Type->GetLengthCm();
		Exit.NoseOffsetCm = Type->GetNoseOffsetCm();
		Exit.RearCm = LaneStartCm - Exit.LengthCm - 1000.f;
		Exit.FrontCm = FMath::Max(Lane->GetRunwayVacatedCm(), LaneStartCm) + Exit.LengthCm + Exit.NoseOffsetCm;
		Exit.bPriority = true;
	}
	return Requests;
}

namespace
{
	struct FBodyPart
	{
		FVector From;
		FVector To;
		float Radius;
	};

	/** Flat outline: fuselage, swept wings from the root just ahead of the main gear, tailplane near the tail. */
	void BodyOutline(const UAircraftTypeData* Type, const FTransform& Pose, TArray<FBodyPart, TInlineAllocator<4>>& Out)
	{
		const float Length = (Type ? Type->LengthM : 39.5f) * ATCUnits::MetersToCm;
		const float Span = (Type ? Type->WingspanM : 35.8f) * ATCUnits::MetersToCm;
		const FVector Forward = Pose.GetRotation().GetForwardVector().GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector Gear(Pose.GetLocation().X, Pose.GetLocation().Y, 0.f);
		const FVector Nose = Gear + Forward * (Type ? Type->GetNoseOffsetCm() : 1560.f + 0.12f * 3950.f);
		const FVector Tail = Nose - Forward * Length;
		const FVector Root = Gear + Forward * (0.1f * Length);
		const FVector Tips = Gear - Forward * (0.12f * Span);
		const FVector Stabilizer = Tail + Forward * (0.08f * Length);
		Out.Add({ Tail, Nose, 0.05f * Length });
		Out.Add({ Root, Tips + Right * (Span * 0.5f), 0.04f * Span });
		Out.Add({ Root, Tips - Right * (Span * 0.5f), 0.04f * Span });
		Out.Add({ Stabilizer - Right * (Span * 0.18f), Stabilizer + Right * (Span * 0.18f), 0.03f * Span });
	}
}

bool UTaxiNetworkSubsystem::Overlap(const AATCAircraft* A, const AATCAircraft* B)
{
	return A && Overlap(A->AircraftType, A->GetActorTransform(), B);
}

bool UTaxiNetworkSubsystem::Overlap(const UAircraftTypeData* Type, const FTransform& Pose, const AATCAircraft* Other, float MarginCm)
{
	if (!Other || FVector::Dist2D(Pose.GetLocation(), Other->GetActorLocation()) > 20000.f)
	{
		return false;
	}
	TArray<FBodyPart, TInlineAllocator<4>> PartsA;
	TArray<FBodyPart, TInlineAllocator<4>> PartsB;
	BodyOutline(Type, Pose, PartsA);
	BodyOutline(Other->AircraftType, Other->GetActorTransform(), PartsB);
	for (const FBodyPart& PartA : PartsA)
	{
		for (const FBodyPart& PartB : PartsB)
		{
			FVector OnA, OnB;
			FMath::SegmentDistToSegmentSafe(PartA.From, PartA.To, PartB.From, PartB.To, OnA, OnB);
			if (FVector::Dist(OnA, OnB) < PartA.Radius + PartB.Radius + MarginCm)
			{
				return true;
			}
		}
	}
	return false;
}

void UTaxiNetworkSubsystem::CheckOverlaps()
{
	const UTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UTrafficSubsystem>() : nullptr;
	if (!Traffic)
	{
		return;
	}
	TArray<const AATCAircraft*> OnGround;
	for (AActor* Actor : Traffic->GetAllAircraft())
	{
		const AATCAircraft* Aircraft = Cast<AATCAircraft>(Actor);
		if (Aircraft && !Aircraft->IsTakingOff() && !Aircraft->IsLanding())
		{
			OnGround.Add(Aircraft);
		}
	}
	TSet<TPair<TObjectKey<AActor>, TObjectKey<AActor>>> Now;
	for (int32 I = 0; I < OnGround.Num(); ++I)
	{
		for (int32 J = I + 1; J < OnGround.Num(); ++J)
		{
			if (!Overlap(OnGround[I], OnGround[J]))
			{
				continue;
			}
			const TObjectKey<AActor> KeyI(OnGround[I]);
			const TObjectKey<AActor> KeyJ(OnGround[J]);
			const TPair<TObjectKey<AActor>, TObjectKey<AActor>> Pair = KeyI < KeyJ ? MakeTuple(KeyI, KeyJ) : MakeTuple(KeyJ, KeyI);
			Now.Add(Pair);
			if (!Overlapping.Contains(Pair))
			{
				++OverlapCount;
				const FVector AtI = OnGround[I]->GetActorLocation();
				const FVector AtJ = OnGround[J]->GetActorLocation();
				UE_LOG(LogATC, Warning, TEXT("Collision monitor: %s (%s) and %s (%s) overlap (X=%.1f Y=%.1f yaw %.1f and X=%.1f Y=%.1f yaw %.1f)"), *NameOf(KeyI),
					*IAircraftInfo::Execute_GetStatusText(OnGround[I]).ToString(), *NameOf(KeyJ), *IAircraftInfo::Execute_GetStatusText(OnGround[J]).ToString(),
					AtI.X / 100.0, AtI.Y / 100.0, OnGround[I]->GetActorRotation().Yaw, AtJ.X / 100.0, AtJ.Y / 100.0, OnGround[J]->GetActorRotation().Yaw);
			}
		}
	}
	Overlapping = MoveTemp(Now);
}

void UTaxiNetworkSubsystem::CheckDeadlocks()
{
	const float Now = GetNow();
	TArray<TArray<TObjectKey<AActor>>> Circles;
	for (const TPair<TObjectKey<AActor>, FClaim>& Pair : Claims)
	{
		if (Pair.Value.WaitingSince < 0.f || Now - Pair.Value.WaitingSince < DeadlockSeconds)
		{
			continue;
		}
		// Follow who waits for whom; coming back here is a circle that nobody leaves on their own
		TArray<TObjectKey<AActor>> Circle = { Pair.Key };
		TObjectKey<AActor> Next = Pair.Value.Blocker;
		for (const FClaim* Claim = Claims.Find(Next); Claim && Claim->WaitingSince >= 0.f && !Circle.Contains(Next) && Circle.Num() < 12; Claim = Claims.Find(Next))
		{
			Circle.Add(Next);
			Next = Claim->Blocker;
		}
		if (Next == Pair.Key && Circle.Num() >= 2)
		{
			Circle.Sort();
			Circles.AddUnique(Circle);
		}
	}

	// Front of the aircraft as a sample of its claim: what lies beyond it is taxiway ahead of its nose
	auto FrontOf = [this](const TObjectKey<AActor>& Key, const FClaim& Claim)
	{
		const AATCAircraft* Aircraft = Cast<AATCAircraft>(Key.ResolveObjectPtr());
		const ATaxiRoute* Route = Tracks.IsValidIndex(Claim.Track) ? Tracks[Claim.Track].Route.Get() : nullptr;
		if (!Aircraft || !Route || Aircraft->Movement->Path != Route->Spline)
		{
			return Claim.Last;
		}
		const float Length = (Aircraft->AircraftType ? Aircraft->AircraftType->LengthM : 39.5f) * ATCUnits::MetersToCm;
		const float FrontCm = Aircraft->Movement->Distance + (Aircraft->Movement->bNoseAlongTravel ? Aircraft->GetNoseOffsetCm() : Length - Aircraft->GetNoseOffsetCm());
		return FMath::Min(LocalSample(Tracks[Claim.Track], FrontCm) + 1, Tracks[Claim.Track].Num - 1);
	};

	// One of the circle lets go of the taxiway ahead of its nose and of its place in the queue for a while, so the
	// others can move. Only one that holds either can free something (one that stands behind another cannot; a landing
	// in the air is not asked). Of those the one that did so longest ago, so the next time it is another; then one that
	// holds taxiway ahead (more likely in the way than a place in the queue); then the last one to join the queue
	for (const TArray<TObjectKey<AActor>>& Circle : Circles)
	{
		if (Circle.ContainsByPredicate([this](const TObjectKey<AActor>& Member) { return Claims[Member].WaitingSince < 0.f; }))
		{
			continue;   // one of them already let go in another circle
		}
		TArray<FString> Names;
		const TObjectKey<AActor>* Chosen = nullptr;
		bool bChosenAhead = false;
		for (const TObjectKey<AActor>& Member : Circle)
		{
			Names.Add(NameOf(Member));
			const FClaim& Claim = Claims[Member];
			const bool bAhead = Claim.Last > FrontOf(Member, Claim);
			if (Claim.bPriority || (!bAhead && Claim.WantedTrack == INDEX_NONE))
			{
				continue;
			}
			const FClaim* Best = Chosen ? &Claims[*Chosen] : nullptr;
			if (!Best || Claim.BackOffUntil < Best->BackOffUntil || (Claim.BackOffUntil == Best->BackOffUntil
				&& (bAhead != bChosenAhead ? bAhead : Claim.WaitingSince > Best->WaitingSince)))
			{
				Chosen = &Member;
				bChosenAhead = bAhead;
			}
		}
		Names.Sort();
		const FString Key = FString::Join(Names, TEXT(" "));
		if (!ReportedDeadlocks.Contains(Key))
		{
			ReportedDeadlocks.Add(Key);
			++DeadlockCount;
			if (!Chosen)
			{
				UE_LOG(LogATC, Warning, TEXT("Taxi deadlock: %s wait for each other; they stand where the other has to go"), *Key);
			}
		}
		if (!Chosen)
		{
			continue;
		}
		UE_LOG(LogATC, Warning, TEXT("Taxi deadlock: %s wait for each other; %s lets the others go first for %.0f s"), *Key, *NameOf(*Chosen), BackOffSeconds);
		FClaim& Claim = Claims[*Chosen];
		const int32 Front = FrontOf(*Chosen, Claim);
		while (Claim.Last > Front && Claim.Last >= Claim.First)
		{
			Unhold(*Chosen, Tracks[Claim.Track].First + Claim.Last--);
		}
		// Two-way stretches it has not entered yet are open to the other direction again
		Claim.Locks.RemoveAll([this, Front](int32 Locked) { return TwoWays[Locked].First > Front; });
		Claim.BackOffUntil = Now + BackOffSeconds;
		Claim.WaitingSince = -1.f;
		Claim.WantedTrack = INDEX_NONE;
		Claim.WantedTwoWay = INDEX_NONE;
		Claim.Blocker = TObjectKey<AActor>();
	}
}

void UTaxiNetworkSubsystem::DrawClaims() const
{
#if ENABLE_DRAW_DEBUG
	const FVector Lift(0.f, 0.f, 300.f);
	for (const TPair<TObjectKey<AActor>, FClaim>& Pair : Claims)
	{
		const FClaim& Claim = Pair.Value;
		const FColor Color = FLinearColor::MakeFromHSV8(uint8(GetTypeHash(Pair.Key) * 47), 190, 255).ToFColor(true);
		for (int32 Local = Claim.First; Tracks.IsValidIndex(Claim.Track) && Local < Claim.Last; ++Local)
		{
			const int32 Sample = Tracks[Claim.Track].First + Local;
			DrawDebugLine(GetWorld(), Samples[Sample].Location + Lift, Samples[Sample + 1].Location + Lift, Color, false, -1.f, 0, 120.f);
		}
		for (int32 Local = Claim.WantedFirst; Tracks.IsValidIndex(Claim.WantedTrack) && Local < Claim.WantedLast; ++Local)
		{
			const int32 Sample = Tracks[Claim.WantedTrack].First + Local;
			DrawDebugLine(GetWorld(), Samples[Sample].Location + Lift * 1.5f, Samples[Sample + 1].Location + Lift * 1.5f, FColor::Red, false, -1.f, 0, 50.f);
		}
	}
#endif
}

FString UTaxiNetworkSubsystem::Describe()
{
	EnsureBuilt();
	FString Text = FString::Printf(TEXT("Taxi network: %d routes, %.1f km two-way in %d stretches, %.1f km at %d crossings\n"), Tracks.Num(),
		TwoWaySamples * StepCm / 100000.f, TwoWays.Num(), CrossingSamples * StepCm / 100000.f, Crossings.Num());
	const float Now = GetNow();
	for (const TPair<TObjectKey<AActor>, FClaim>& Pair : Claims)
	{
		const FClaim& Claim = Pair.Value;
		const ATaxiRoute* Route = Tracks.IsValidIndex(Claim.Track) ? Tracks[Claim.Track].Route.Get() : nullptr;
		Text += FString::Printf(TEXT("  %-8s %-36s %6.0f - %6.0f m"), *NameOf(Pair.Key), Route ? *Route->RouteTag.ToString() : TEXT("(none)"),
			Route ? SampleCm(Claim.First) / 100.f : 0.f, Route ? SampleCm(Claim.Last) / 100.f : 0.f);
		if (Claim.WaitingSince >= 0.f)
		{
			Text += FString::Printf(TEXT("  waits %.0f s %s %s"), Now - Claim.WaitingSince, Claim.WantedTrack != INDEX_NONE ? TEXT("for") : TEXT("behind"),
				*NameOf(Claim.Blocker));
		}
		Text += TEXT("\n");
	}
	Text += FString::Printf(TEXT("Collision monitor: %d overlaps, %d deadlocks since Play"), OverlapCount, DeadlockCount);
	return Text;
}

void UTaxiNetworkSubsystem::Plot(const FString& Name, const FVector& Center, float HalfSizeCm)
{
	EnsureBuilt();
	const int32 Size = 1600;
	TArray<FColor> Pixels;
	Pixels.Init(FColor(18, 20, 26), Size * Size);
	const float Scale = Size / (2.f * FMath::Max(HalfSizeCm, 1000.f));
	auto Paint = [&](const FVector& Location, const FColor& Color, int32 Radius)
	{
		const int32 CenterX = FMath::RoundToInt((Location.X - Center.X) * Scale + Size * 0.5f);
		const int32 CenterY = FMath::RoundToInt((Location.Y - Center.Y) * Scale + Size * 0.5f);
		for (int32 Y = CenterY - Radius; Y <= CenterY + Radius; ++Y)
		{
			for (int32 X = CenterX - Radius; X <= CenterX + Radius; ++X)
			{
				if (X >= 0 && Y >= 0 && X < Size && Y < Size)
				{
					Pixels[Y * Size + X] = Color;
				}
			}
		}
	};
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const uint8 Flags = SampleFlags.IsValidIndex(Index) ? SampleFlags[Index] : 0;
		Paint(Samples[Index].Location, (Flags & 2) ? FColor(150, 70, 170) : (Flags & 1) ? FColor(150, 130, 40) : FColor(85, 85, 85), 0);
	}
	for (const TPair<TObjectKey<AActor>, FClaim>& Pair : Claims)
	{
		const FClaim& Claim = Pair.Value;
		const FColor Color = FLinearColor::MakeFromHSV8(uint8(GetTypeHash(Pair.Key) * 47), 190, 255).ToFColor(true);
		for (int32 Local = Claim.First; Tracks.IsValidIndex(Claim.Track) && Local <= Claim.Last; ++Local)
		{
			Paint(Samples[Tracks[Claim.Track].First + Local].Location, Color, 2);
		}
		for (int32 Local = Claim.WantedFirst; Tracks.IsValidIndex(Claim.WantedTrack) && Local <= Claim.WantedLast; ++Local)
		{
			Paint(Samples[Tracks[Claim.WantedTrack].First + Local].Location, FColor::Red, 1);
		}
	}
	// Every aircraft: main gear, and a dot 20 m ahead of it for the heading
	if (const UTrafficSubsystem* Traffic = GetWorld()->GetSubsystem<UTrafficSubsystem>())
	{
		for (const AActor* Aircraft : Traffic->GetAllAircraft())
		{
			Paint(Aircraft->GetActorLocation(), FColor::White, 4);
			Paint(Aircraft->GetActorLocation() + Aircraft->GetActorForwardVector() * 2000.f, FColor::White, 1);
		}
	}
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Size, Size, Pixels, Png);
	const FString File = FPaths::ProjectSavedDir() / TEXT("ATC") / Name + TEXT(".png");
	FFileHelper::SaveArrayToFile(Png, *File);
	UE_LOG(LogATC, Display, TEXT("Taxi network picture: %s"), *File);
}
