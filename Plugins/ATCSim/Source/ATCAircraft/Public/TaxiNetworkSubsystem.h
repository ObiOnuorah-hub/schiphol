#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "TaxiNetworkSubsystem.generated.h"

class AATCAircraft;
class ATaxiRoute;
class UAircraftTypeData;

/** What a moving aircraft asks the reservation system for. All distances in cm along its route. */
struct ATCAIRCRAFT_API FTaxiClaimRequest
{
	ATaxiRoute* Route = nullptr;

	/** Rearmost and frontmost point of the aircraft in the travel direction. A pushback travels tail first. */
	float RearCm = 0.f;
	float FrontCm = 0.f;

	/** How far ahead it wants to be allowed to go: braking distance plus a look-ahead. */
	float WantedCm = 0.f;

	/** Not past this point, for example a stop where it will wait for a clearance. A two-way stretch is still claimed whole. */
	float LimitCm = BIG_NUMBER;

	float HalfSpanCm = 0.f;
	float LengthCm = 0.f;

	/** Main gear to the nose tip; the wings sit around the main gear. */
	float NoseOffsetCm = 0.f;

	/** A pushback: the tail leads. */
	bool bTailFirst = false;

	/** Room before the next aircraft in the same direction: a crossing is only entered when the whole aircraft fits past it. */
	float RoomAheadCm = BIG_NUMBER;

	/** The aircraft ahead it keeps its distance to and now stands behind: that wait counts in the deadlock check too. */
	const AActor* HeldBy = nullptr;

	/** A landing: it cannot wait in the air, so in the queues it counts as having waited LandingHeadStartSeconds already. */
	bool bPriority = false;
};

/**
 * The taxi network of a map as one system, built from all TaxiRoutes when the first aircraft asks for it: every route
 * sampled every 5 m, with the routes that use the same piece of taxiway (in the same or the opposite direction) and the
 * places where routes cross.
 *
 * On top of it the reservation system. An aircraft only taxis where it holds a claim, and claims never conflict, so two
 * aircraft never use the same piece of taxiway:
 *  - a claim runs from just behind the tail to the aircraft's authority: braking distance plus a look-ahead. Where the
 *    wings will pass it is as wide as the wingspan; the part ahead of the wings only as wide as the fuselage, so an
 *    aircraft can wait at a crossing like at a real holding position;
 *  - a two-way stretch (another route close by the other way) is only entered when nobody who will pass the same places
 *    the other way is in it, or has waited for it longer: the aircraft locks it until its tail is out. Nobody meets
 *    head-on on a taxiway, and nobody waits longer than needed;
 *  - a crossing is claimed in one go until the tail is through it: nobody stops in the middle of a crossing;
 *  - claims never overlap, also not in the same direction: a follower reserves up to just behind the tail of the
 *    aircraft ahead (the taxi controller keeps the 60 m separation), so it can never take a crossing that one needs;
 *  - a blocked aircraft queues for that piece of taxiway: when it frees up, whoever waited longest goes first. A landing
 *    counts as having waited two minutes already: it cannot wait in the air, but it does not keep a departure waiting
 *    for good either;
 *  - aircraft that wait for each other in a circle for a minute (also one standing behind another) are a deadlock: one
 *    of them that holds taxiway ahead of its nose, or a place in the queue, lets go of it for 30 s. Is the circle still
 *    there after that, the next one does.
 * Lane choice: a route with Alternatives (a parallel taxiway) gets the lane with the least opposing traffic, otherwise
 * the right-hand one, so the two directions each keep to their own taxiway.
 * Collision monitor: four times a second every pair of aircraft on the ground is checked for overlap (log and counter).
 */
UCLASS()
class ATCAIRCRAFT_API UTaxiNetworkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** A route came or went: the network is rebuilt at the next request. Claims are kept. */
	void MarkDirty() { bDirty = true; }

	/**
	 * Keeps the aircraft's claim on its route up to date: lets go of what lies behind it and extends it towards WantedCm.
	 * Returns how far the front of the aircraft may go. OutBlocker = whoever holds, or queued earlier for, the next piece.
	 */
	float UpdateClaim(const AActor* Aircraft, const FTaxiClaimRequest& Request, AActor*& OutBlocker);

	/**
	 * Claims Request.RearCm..FrontCm at once, for example a pushback path or the runway exit of a landing. False, and
	 * nothing claimed, when part of it is taken. bCheckOnly: only whether it could (Aircraft may be null: not there yet).
	 */
	bool ClaimStretch(const AActor* Aircraft, const FTaxiClaimRequest& Request, AActor*& OutBlocker, bool bCheckOnly = false);

	/**
	 * Reserves the two-way stretches of Request.Route between RearCm and FrontCm for the aircraft, on a route it will
	 * drive next (the taxi route after a pushback): nobody may enter them the other way before it has passed.
	 */
	bool ReserveTwoWay(const AActor* Aircraft, const FTaxiClaimRequest& Request, AActor*& OutBlocker, bool bCheckOnly = false);

	/** Gives back what ReserveTwoWay reserved (the aircraft does not go yet after all). */
	void CancelReservations(const AActor* Aircraft);

	/** Does the aircraft hold a claim (it is on the taxi network)? */
	bool HasClaim(const AActor* Aircraft) const;

	/** Picture of the network around Center (north up): routes grey, two-way stretches yellow, crossings purple, claims in colour, waits red. */
	void Plot(const FString& Name, const FVector& Center, float HalfSizeCm);

	/** Claims Request.RearCm..FrontCm without asking: the aircraft is already there (just spawned on a route). */
	void ClaimBody(const AActor* Aircraft, const FTaxiClaimRequest& Request);

	/** Nobody's claim, and nobody in the queue, in the way of an aircraft of this size on that stretch (before spawning one there). */
	bool IsStretchFree(ATaxiRoute* Route, float FromCm, float ToCm, float HalfSpanCm, AActor*& OutBlocker);

	/** The aircraft leaves the taxi network: parked on its stand, in the air or removed. */
	void ReleaseClaim(const AActor* Aircraft);

	/** Claimed stretch of an aircraft, cm along its route. False when it holds none. */
	bool GetClaim(const AActor* Aircraft, ATaxiRoute*& OutRoute, float& OutFromCm, float& OutToCm) const;

	/**
	 * Planned or one of its Alternatives, for an aircraft starting at Start: the lane with the least opposing traffic and
	 * the shortest queue; with equal traffic the right-hand one. OutReason explains the choice for the log.
	 */
	ATaxiRoute* ChooseLane(const AActor* Aircraft, ATaxiRoute* Planned, const FVector& Start, FString* OutReason = nullptr);

	/** Do the bodies of two aircraft (fuselage, wings, tailplane) overlap? */
	static bool Overlap(const AATCAircraft* A, const AATCAircraft* B);

	/**
	 * Would an aircraft of Type with its main gear and heading at Pose overlap the body of Other where Other is now?
	 * MarginCm: closer than this counts too.
	 */
	static bool Overlap(const UAircraftTypeData* Type, const FTransform& Pose, const AATCAircraft* Other, float MarginCm = 0.f);

	/**
	 * What a landing reserves: the runway behind the start of its arrival route and the exit up to where its tail is
	 * clear of the runway (90 m), on Route (first) and on each of its Alternatives that leaves the runway at the same place.
	 */
	static TArray<FTaxiClaimRequest> RunwayExitRequests(ATaxiRoute* Route, float StartCm, const UAircraftTypeData* Type);

	/** Overlaps the collision monitor has seen since Play started. Should stay 0. */
	int32 GetOverlapCount() const { return OverlapCount; }

	/** Circles of aircraft waiting for each other since Play started (each circle counted once). */
	int32 GetDeadlockCount() const { return DeadlockCount; }

	/** Network, claims and queue, for ATC.Network. */
	FString Describe();

	/** Draws every claim over the taxiways (ATC.Claims 1): one colour per aircraft, red where it waits. */
	bool bDrawClaims = false;

private:
	struct FSample
	{
		FVector Location = FVector::ZeroVector;
		/** Travel direction, flat. */
		FVector Direction = FVector::ForwardVector;
	};

	/** Another route through the same place as a sample, the same way or the opposite way. */
	struct FCoincident
	{
		int32 Track = 0;
		int32 Local = 0;
		bool bOpposite = false;
	};

	/** Where another route runs the other way close to a two-way stretch: a sample of this track and the nearest one of the other. */
	struct FOppositePair
	{
		int32 Local = 0;
		int32 OtherTrack = 0;
		int32 OtherLocal = 0;
	};

	/** A stretch of one track: a two-way stretch (with the 100 m before it where one waits) or a crossing. */
	struct FStretch
	{
		int32 First = 0;
		int32 Last = 0;
		/** Two-way stretches: every place where another route runs the other way close by. */
		TArray<FOppositePair> Pairs;
	};

	/** One route: samples from 80 m before its start to 80 m past its end, where the bodies of aircraft stick out. */
	struct FTrack
	{
		TWeakObjectPtr<ATaxiRoute> Route;
		int32 First = 0;
		int32 Num = 0;
		/** Per sample the two-way stretch (index in TwoWays) and the crossing (index in Crossings) it lies in, or INDEX_NONE. */
		TArray<int32> TwoWay;
		TArray<int32> Crossing;
	};

	struct FClaim
	{
		TWeakObjectPtr<const AActor> Aircraft;
		int32 Track = INDEX_NONE;
		/** Claimed samples First..Last of Track (local, inclusive); empty when Last < First. */
		int32 First = 0;
		int32 Last = -1;
		/** Width of the claim: the wings in the middle, the narrow nose or tail in the first and last few samples. */
		float HalfSpanCm = 0.f;
		int32 FrontNarrow = 0;
		float FrontRadiusCm = 0.f;
		int32 RearNarrow = 0;
		float RearRadiusCm = 0.f;
		/** Two-way stretches (of Track) it has entered: locked for the other direction until its tail is out. */
		TArray<int32, TInlineAllocator<2>> Locks;
		/** Two-way stretches of the route it drives next, from a sample on: they become locks when it gets there. */
		struct FReserved
		{
			int32 Track = 0;
			int32 TwoWay = 0;
			int32 From = 0;
		};
		TArray<FReserved, TInlineAllocator<2>> Reserved;
		/**
		 * In the queue since (game seconds; -1 = not waiting), for WantedFirst..WantedLast of WantedTrack, held up by Blocker.
		 * Standing behind the aircraft ahead is a wait too, without a wanted piece (WantedTrack none): not a place in the queue.
		 */
		float WaitingSince = -1.f;
		int32 WantedTrack = INDEX_NONE;
		int32 WantedFirst = 0;
		int32 WantedLast = -1;
		/** Waits to enter this two-way stretch: only the other direction has to let it go first. */
		int32 WantedTwoWay = INDEX_NONE;
		FBox WantedBox = FBox(ForceInit);
		TObjectKey<AActor> Blocker;
		/** After a deadlock this one lets the others go first until then. */
		float BackOffUntil = -1.f;
		/** A landing: in the queues it counts as having waited HeadStartSeconds longer. */
		bool bPriority = false;
	};

	struct FHeld
	{
		TObjectKey<AActor> Owner;
		int32 Sample = 0;
	};

	void EnsureBuilt();
	void Build();
	int32 FindTrack(const ATaxiRoute* Route) const;
	int32 LocalSample(const FTrack& Track, float Cm) const;
	float SampleCm(int32 Local) const;
	/** -1: Track does not pass through Sample; 0: it does, the same way; 1: it does, the opposite way. */
	int32 Relation(int32 Sample, int32 Track) const;
	/**
	 * Could aircraft at these two samples, this wide (half widths), touch? On one piece of taxiway the claims themselves
	 * hold the bodies along the line, so there only the same place counts, either way.
	 */
	bool Conflicts(int32 Mine, float MyRadius, int32 Theirs, float TheirRadius) const;
	/** Half width of a claim at one of its samples: wings in the middle, nose and tail at the ends. */
	float RadiusAt(const FClaim& Claim, int32 Local) const;
	/** Its place in the queues: when it started waiting (or now), a landing the head start earlier. */
	float QueueSince(const FClaim& Claim) const;
	/** No other claim, and nobody who has waited longer for it, in the way of Sample at this half width. */
	bool IsFree(const TObjectKey<AActor>& Me, const FClaim* MyClaim, int32 Sample, float Radius, TObjectKey<AActor>& OutBlocker) const;
	/**
	 * Can the aircraft enter two-way stretch TwoWay of Track at From? Not while somebody who has locked an opposite
	 * stretch, or waited longer for one, still has to pass a place close to where this one still has to go.
	 */
	bool IsTwoWayFree(const TObjectKey<AActor>& Me, const FClaim& MyClaim, int32 Track, int32 TwoWay, int32 From, TObjectKey<AActor>& OutBlocker) const;
	/** Will aircraft in stretch A (from FromA on) and stretch B of BTrack (from FromB on) pass close by each other the other way? */
	bool Meet(const FStretch& A, int32 ATrack, int32 FromA, float HalfSpanA, const FStretch& B, int32 BTrack, int32 FromB, float HalfSpanB) const;
	/** Locks the two-way stretches under Local First..Last of the claim (it is there already). */
	void LockUnder(FClaim& Claim, int32 First, int32 Last);
	void SetShape(FClaim& Claim, const FTaxiClaimRequest& Request);
	void Hold(const TObjectKey<AActor>& Owner, int32 Sample);
	void Unhold(const TObjectKey<AActor>& Owner, int32 Sample);
	void ClearHeld(const TObjectKey<AActor>& Owner, FClaim& Claim);
	void SetWaiting(FClaim& Claim, int32 Track, int32 First, int32 Last, int32 TwoWay, const TObjectKey<AActor>& Blocker);
	/** +1: Other lies to the right of Lane where they part, -1: to the left, 0: they never part. */
	int32 SideOf(ATaxiRoute* Lane, ATaxiRoute* Other);
	void CheckOverlaps();
	void CheckDeadlocks();
	void DrawClaims() const;
	FString NameOf(const TObjectKey<AActor>& Key) const;
	float GetNow() const;

	TArray<FSample> Samples;
	TArray<int32> SampleTrack;
	/** Per sample: 1 = two-way, 2 = crossing. */
	TArray<uint8> SampleFlags;
	/** Coincident[CoincidentStart[S] .. CoincidentStart[S + 1]) are the other routes through sample S. */
	TArray<int32> CoincidentStart;
	TArray<FCoincident> Coincident;
	TArray<FTrack> Tracks;
	TArray<FStretch> TwoWays;
	TArray<FStretch> Crossings;
	TMap<TObjectKey<ATaxiRoute>, int32> TrackOf;
	TMap<FIntPoint, TArray<int32>> SampleGrid;
	TMap<FIntPoint, TArray<FHeld>> HeldGrid;
	TMap<TObjectKey<AActor>, FClaim> Claims;
	TMap<TPair<TObjectKey<ATaxiRoute>, TObjectKey<ATaxiRoute>>, int32> SideCache;
	TSet<TPair<TObjectKey<AActor>, TObjectKey<AActor>>> Overlapping;
	TSet<FString> ReportedDeadlocks;
	float MaxHalfSpanCm = 0.f;
	/** Wingtip clearance and the head start of landings in the queues, from the project settings, read at every build. */
	float ClearanceCm = 750.f;
	float HeadStartSeconds = 120.f;
	float MonitorTimer = 0.f;
	float DeadlockTimer = 0.f;
	int32 OverlapCount = 0;
	int32 DeadlockCount = 0;
	int32 TwoWaySamples = 0;
	int32 CrossingSamples = 0;
	bool bDirty = true;
};
