#include "ATCGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Cmd_Pushback, "Cmd.Pushback", "Start pushback. Parameter = pushback option (Pushback.*)");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Cmd_Taxi, "Cmd.Taxi", "Start taxiing. Parameter = route (Route.*), empty = assigned route");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Cmd_HoldPosition, "Cmd.HoldPosition", "Stop as soon as possible");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Cmd_Continue, "Cmd.Continue", "Continue taxiing (clearance)");

UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Pushback_State, "Event.Pushback.State", "Pushback state changed");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Pushback_Completed, "Event.Pushback.Completed", "Pushback finished, tug disconnected");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Taxi_State, "Event.Taxi.State", "Taxi state changed");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Taxi_StopReached, "Event.Taxi.StopReached", "Stopped at a stop position");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Taxi_Waypoint, "Event.Taxi.Waypoint", "Passed a waypoint");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Taxi_SpeedZone, "Event.Taxi.SpeedZone", "Entered a speed zone");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Taxi_Arrived, "Event.Taxi.Arrived", "End of the route reached");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Taxi_Traffic, "Event.Taxi.Traffic", "Held up by an aircraft ahead, or clear again");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Traffic_Spawned, "Event.Traffic.Spawned", "Aircraft spawned by the spawn manager");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Traffic_Queued, "Event.Traffic.Queued", "Spawn waits: another aircraft is in the way");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Traffic_Removed, "Event.Traffic.Removed", "Aircraft removed (departed or despawned)");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Traffic_Takeoff, "Event.Traffic.Takeoff", "Takeoff roll started");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_ATC_Event_Traffic_GateAssigned, "Event.Traffic.GateAssigned", "Arrival assigned to a gate");
