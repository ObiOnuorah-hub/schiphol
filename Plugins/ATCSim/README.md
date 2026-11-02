# ATC Sim Bouwblokken (Unreal Engine 5.6)

Herbruikbare bouwblokken voor VR-simulatie van luchtverkeer: taxiën, pushback, GMI-camera, weer, dag/nacht en luchthavens.
Het ontwerp staat in `Docs/ATC_VR_Bouwblokken.html` in de projectmap.

## Luchthavens en scenario's
| Luchthaven | Map | Scenario (`ATC.Scenario <naam>`) | Torens |
|---|---|---|---|
| Schiphol | `/Game/EHAM/EHAM` (de echte map) | `EHAM_Dag`: drukke dag, aankomsten, turnarounds en vertrekken, klaringen door een collega<br>`EHAM_DagZelf`: hetzelfde, alle klaringen geef je zelf<br>`EHAM_Verkeer`: rijen voor 36L en 24 via de spawnpunten<br>`EHAM_Ochtendpiek`: KLM1023 (787-9) G7 → 36L, EZY45LK (A320) G4 → 24 | `Tower.EHAM.Centrum`, `.West`, `.Nood` |
| Rotterdam | `/Game/Airports/EHRD/Maps/Map_EHRD_Schematic` | `EHRD_Vertrek`: A1 → 24 | `Tower.EHRD` |
| Lelystad | `/Game/Airports/EHLE/Maps/Map_EHLE_Schematic` | `EHLE_Vertrek`: L6 → 23 | `Tower.EHLE` |
| Groningen | `/Game/Airports/EHGG/Maps/Map_EHGG_Schematic` | `EHGG_Vertrek`: platform A → 23 | `Tower.EHGG` |
| Maastricht | `/Game/Airports/EHBK/Maps/Map_EHBK_Schematic` | `EHBK_Vertrek`: A2 → 21 | `Tower.EHBK` |
| Showcase | `/Game/Airports/Showcase/Maps/Map_Showcase_ATC` | `Showcase` (start vanzelf) | `Tower.Showcase` |

- **Schiphol**: open EHAM en druk op Play. Je staat in Toren Centrum (`PlayTower` van `DA_Airport_EHAM`) en het verkeer loopt meteen:
  landingen, pushbacks, taxiën en starts (`TrafficDirector_EHAM` met `bStartOnBeginPlay`). De klok loopt op de echte tijd en het weer is
  het echte weer van Schiphol (zie *Echte tijd en echt weer*); zet `bLiveEnvironmentOnPlay` uit en de map houdt zijn eigen licht tot een
  scenario start. Weer rustig: `ATC.TrafficOnPlay 0` (editor). Een scenario met eigen verkeer, zoals
  `ATC.Scenario EHAM_Ochtendpiek`, maakt het vliegveld eerst leeg. Klaring geef je zelf: `ATC.Cmd KLM1023 Continue`.
- **De andere vier** zijn *schematische* maps, gemaakt uit OpenStreetMap-data (banen, taxibanen, platforms, gebouwen, stands).
  Ze staan als "SCHEMATISCHE KAART" bij de toren. Groningen en Maastricht hebben wel een map in het project (`Content/EHGG`,
  `Content/EHBK`), maar daarin zijn de vliegveldobjecten op 1:100 geïmporteerd (de toren is kleiner dan een halve meter).
- Kaartdata © OpenStreetMap-bijdragers, ODbL 1.0. De bestanden staan in `Plugins/ATCSim/Data/OSM`.

### Wat geschat is
- Noodtoren Schiphol: de oude toren op Schiphol-Oost (OSM "Eerste verkeerstoren"); de bronnen zeggen niet duidelijk welk gebouw nu de officiële noodtoren is.
- Toren Rotterdam: niet in OSM; geplaatst op 921 m van het baanmidden richting terminal (eAIP: ARP 921 m van TWR). Hoogte geschat.
- Torenhoogtes Lelystad en Groningen: geschat (25 m werkvloer). Maastricht: 31 m uit OSM.
- Stand Groningen: OSM heeft geen standnummers; met de hand op platform A gezet.
- Routes zijn berekend over de lijnen (A*), niet gecontroleerd tegen de echte taxiprocedures.

## Snel starten (showcase)
1. Open de map **`/Game/Airports/Showcase/Maps/Map_Showcase_ATC`**.
2. Druk op **Play**. Het scenario `DA_Scenario_Showcase` start vanzelf:
   - je staat in de toren;
   - KLM1023 (Boeing 787-9) duwt achteruit, taxiet zelf naar holding point B4 en wacht op klaring;
   - om T+200 s krijgt KLM1023 klaring en rijdt door naar de baan;
   - de GMI-camera wisselt elke 12 s van preset; het beeld staat op de monitor rechtsonder.
3. Open de console (`~`) en probeer de commando's hieronder.

## Console-commando's
| Commando | Doet |
|---|---|
| `ATC.Weather Rain 20` | Weer naar Clear, Overcast, Rain, Mist of Snow in 20 s (de weermodus wordt `fixed`, zodat het zo blijft) |
| `ATC.WeatherMode live` | Waar het weer vandaan komt: `fixed` (alleen presets), `dynamic` (verandert vanzelf), `live` (de echte METAR). Zonder argument: modus, station, laatste METAR en wat er nu komt |
| `ATC.Metar` | De METAR van het live weer; `ATC.Metar refresh` vraagt hem nu op; `ATC.Metar EHAM 021225Z 22012KT 4000 RA BKN008 11/10 Q1004` gebruikt die METAR als weer (ook offline) |
| `ATC.Time 21:30` | Simulatietijd in UTC (optioneel datum: `ATC.Time 06:00 2026-12-21`); de klok gaat dan van de echte tijd af |
| `ATC.RealTime` | Klok terug op de echte tijd (UTC van deze pc), snelheid 1 |
| `ATC.TimeScale 60` | Klok 60x sneller (alleen de zon, niet het verkeer) |
| `ATC.Vehicles 0` | Grondvoertuigen uit (`1` = aan; zonder getal: welke vliegtuigen nu service krijgen) |
| `ATC.Cmd KLM1023 Hold` | Opdracht: `Pushback`, `Taxi`, `Hold` of `Continue` (+ optioneel tag) |
| `ATC.Camera GMI-TWR Cam.EHAM.Threshold18R` | GMI-camera naar een preset; die camera wordt ook de bestuurde. Of een lens (`ATC.Camera GMI-TWR 3`), `next`, of pan en tilt (`ATC.Camera GMI-G7 10 -15 100`). Zonder camera-ID: alle camera's (`ATC.Camera Cam.Showcase.Runway`). Alleen `ATC.Camera`: lijst van alle camera's |
| `ATC.CameraUI` | Instructeurspaneel aan/uit: alle camerabeelden rechtsboven op het desktopscherm (`save` = als plaatje in `Saved/Screenshots/ATC`) |
| `ATC.CameraStats 10` | Meet 10 s: beelden per seconde per camera en GPU-tijd per frame (vergelijk met `atc.CameraCaptures 0`) |
| `ATC.LookAt KLM1023 30` | Kijk naar een vliegtuig, met beeldhoek 30° (verrekijker), of naar een torenscherm (`ATC.LookAt Screen2`) |
| `ATC.Key PageUp 1.5` | Houdt een toets 1,5 s ingedrukt, via de gewone input-mapping (demo's, tests) |
| `ATC.Watch KLM1023 80` | Volgcamera: kijk van dichtbij (80 m) naar een vliegtuig; terug met `ATC.Tower`. `ATC.Watch landing 300` / `ATC.Watch takeoff 300`: het vliegtuig dat nu landt of opstijgt |
| `ATC.Lights 0` | Taxibaanlampen uit (`1` = aan, zonder getal: stand per route in de log) |
| `ATC.TrafficDirector start` | Verkeersprogramma van de map: `start`, `stop`, `status`, `auto 0` (klaringen zelf), `rate 50` (aankomsten per uur) |
| `ATC.Traffic` | Alle vliegtuigen met toestand, route, plek en wie ze ophoudt; en wat nog wacht om te spawnen |
| `ATC.Spawn Route.EHAM.G4.Rwy24 A320 EZY12AB 900` | Vliegtuig op een route (of `Spawn.EHAM.Rwy36L`): type, callsign, start in m, eindstop; `-` = standaard |
| `ATC.Despawn EZY12AB` | Vliegtuig weg (`all` = iedereen) |
| `ATC.Radio 0` | Radioverkeer uit (`1` = aan, zonder getal: de laatste uitzendingen) |
| `ATC.Monitor 1 GMI-E6` | Welke camera scherm 1 in de toren toont (`next` = volgende) |
| `ATC.Turn -36` | Kijkrichting draaien in graden (rondkijken zonder muis); `ATC.Turn 0 -20` kijkt ook 20° omlaag |
| `ATC.Tower Tower.EHAM.West` | Naar een torenpositie (zonder tag: de eerste toren van de luchthaven) |
| `ATC.Status` | Klok (UTC en lokale tijd, echte tijd of niet), zon, weer met de weermodus en alle vliegtuigen in de log |
| `ATC.Scenario Showcase` | Start een scenario uit `/Game/Scenarios` |
| `ATC.Shot naam` | Screenshot naar `Saved/Screenshots/ATC` (`ATC.Shot naam ui`: met de widgets) |
| `ATC.After 30 ATC.Weather Mist` | Commando later uitvoeren (demo's, tests) |
| `ATC.Network` | Het taxinetwerk in de log: alle reserveringen, wie op wie wacht, en wat de botsingscontrole zag (`plot` = plaatje in `Saved/ATC`, `plot KLM1023 500` = 500 m rond dat vliegtuig) |
| `ATC.Claims 1` | De gereserveerde stukken taxibaan boven de taxibanen tekenen (één kleur per vliegtuig, rood = wacht erop) |

Editor-only (Output Log > Cmd, of headless met `-ExecCmds`):
| Commando | Doet |
|---|---|
| `ATC.CreateExampleContent` | Alle voorbeeld-assets en de showcase-map opnieuw (gooi eerst `Map_Showcase_ATC.umap` weg) |
| `ATC.SetupEHAM` | In de open EHAM-map: 26 gates met pushback-, vertrek- en aankomstroute over de gele lijnen, scenario's, en daarna `ATC.SetupEHAMTraffic`. Maakt eerst één keer een kopie in `Saved/Backup_before_ATCSetup` |
| `ATC.SetupEHAMTraffic` | Alleen het verkeer: spawnpunten, traffic mix, traffic director, GMI-camera's op de torenschermen, radiolog, scenario's `EHAM_Dag`, `EHAM_DagZelf`, `EHAM_Verkeer`. Let op: dit bouwt ook de traffic director opnieuw (daarna `ATC.AddParkedGates`) |
| `ATC.SetupGMI` | Alleen de GMI-camera's van Schiphol opnieuw, op de schermen van het torenmodel in Toren Centrum. Andere schermen: `ATC.SetupGMI SketchUp_Model13/TWR_Rechts_Mat SketchUp_Model15/CCIS_Mat ...` (in camera-volgorde). Kopie van de map in `Saved/Backup_before_GMI` |
| `ATC.TowerScreens Tower.EHAM.Centrum 12` | Alle schermen van het torenmodel binnen 12 m van het oog, met naam (`actor/materiaal`), maat, richting en afstand |
| `ATC.CreateCameraInput` | De input-acties en de mapping voor de camera's (`/ATCSim/Input/IA_GMI_*`, `IMC_GMI`) opnieuw |
| `ATC.TrafficOnPlay 1` | Gewoon Play als levend vliegveld: verkeer start en je staat in de eerste toren (`0` = de map zoals hij is). Slaat map en luchthaven op |
| `ATC.SetupFleet` | Alle vliegtuigtypes met de modellen en liveries uit het project, en de Schiphol traffic mix |
| `ATC.CheckRoutes` | Controleert of de taxiroutes van EHAM op het asfalt liggen (log + `Saved/ATC/route_check.png`), waarschuwt als een aankomstroute dwars op zijn stand eindigt, en zet bij elke aankomstroute hoeveel baan erachter ligt (voor de landing) |
| `ATC.AddParallelRoutes` | Geeft elke EHAM-vertrek- en aankomstroute een parallelle variant over de andere taxibaan, waar die er is (`Alternatives`; plaatje `Saved/ATC/parallel_routes.png`). Opnieuw draaien vervangt ze. Kopie van de map in `Saved/Backup_before_ParallelRoutes` |
| `ATC.AddParkedGates` | De vliegtuigen die in de EHAM-map geparkeerd staan (statische en skeletal modellen uit `Datasmith/Vliegtuigen`) doen mee in het verkeer: elk krijgt een gate op zijn stand, of houdt de verkeersgate waar hij al op staat. Nieuwe tags gaan eerst in `ATCSimTags.ini`: dan de editor opnieuw starten en nog een keer draaien. Daarna `ATC.CheckRoutes` en `ATC.AddParallelRoutes`. `list` = alleen laten zien. Kopie van de map in `Saved/Backup_before_ParkedGates` |
| `ATC.ProbeEHAMGates 36L 36C D7 E18` | Probeert routes voor stands zonder iets te plaatsen (log + `Saved/ATC/eham_probe.png`) |
| `ATC.ImportRadioVoice` | Radiofragmenten uit `Plugins/ATCSim/Data/Radio` importeren (eerst `Tools/MakeRadioVoice.ps1`) |
| `ATC.BuildSchematicMap EHRD` | Schematische map + stand + routes + scenario uit OSM (`EHRD`, `EHLE`, `EHGG`, `EHBK`) |
| `ATC.MapSurvey` / `ATC.MapPlot` / `ATC.MapPath` | Bovenaanzicht van de geschilderde lijnen, detailplot, testroute (PNG in `Saved/ATC`) |
| `ATC.MapNear x y r` / `ATC.MapFind naam` | Objecten rond een punt (met hoogteprofiel) / op naam |
| `ATC.GeoFit EHAM RunwayMarkings 52.3075170 4.7624393 -90` | Legt de OSM-banen over de baanverf en meet noordrichting en verschuiving |
| `ATC.PlotOsm EHLE 52.4612 5.5230 150` | OSM-data rond een punt, om stands te kiezen |
| `ATC.CreatePrecipitation` | Regen en sneeuw (Niagara + materialen) opnieuw |
| `ATC.CreateTaxiLights` | Lamp-mesh en -materiaal (`/ATCSim/Lights`), het gele lijnmateriaal en het schermmateriaal opnieuw |

## Wat zit waar
| Module | Belangrijkste klassen |
|---|---|
| `ATCSimCore` | `UATCSimClockSubsystem` (UTC-klok), `ATCUnits`, gameplay tags `Cmd.*` en `Event.*`, `UATCSimSettings` (Project Settings > Plugins > ATC Sim) |
| `ATCAircraft` | `AATCAircraft`, `UAircraftMovementComponent`, `UTaxiControllerComponent`, `UPushbackControllerComponent`, `ATaxiRoute`, `ATaxiStopMarker`, `AStand`, `UTrafficSubsystem`, `UTaxiNetworkSubsystem`, `UAircraftTypeData`, `IAircraftCommands`, `IAircraftInfo`, `UAircraftSpawnSubsystem`, `AAircraftSpawnPoint`, `UTrafficMixData`, `ATrafficDirector`, `AAirportVehicle`, `APushbackTug`, `UGroundServiceSubsystem` |
| `ATCCamera` | `AGMICamera`, `UPTZControllerComponent`, `AMonitorDisplay`, `ULensSetData`, `ULensLibrary`, `UGMICameraSubsystem` (renderbudget en bediening), `UGMICameraSettings` (Project Settings > Plugins > ATC Sim Camera), `UGMICameraPanel` |
| `ATCEnvironment` | `UWeatherSubsystem` (presets, dynamisch en live weer), `FMetarReport` (METAR-parser), `UTimeOfDaySubsystem`, `ULightingRigComponent` / `AEnvironmentRig`, `UWeatherPresetData`, `IWeatherListener`, `IDayNightListener` |
| `ATCAirports` | `UAirportData`, `UScenarioData`, `UAirportSubsystem`, `AScenarioStarter`, `URadioSubsystem`, `URadioVoiceData`, `ARadioLogDisplay`, de console-commando's |
| `ATCSimEditor` | Validatie van verwijzingen buiten het project, voorbeeldcontent, Schiphol-setup, radio-import, tests |

Blueprints (laag "samenstelling"): `/ATCSim/Blueprints/BP_Aircraft_Base`, `BP_Stand`, `BP_GMICamera`, `BP_MonitorDisplay`, `BP_EnvironmentRig`,
`BP_AircraftSpawnPoint`, `BP_RadioLogDisplay`.

## Zelf iets toevoegen
- **Vliegtuigtype**: dupliceer `DA_AircraftType_B738`, vul afmetingen en snelheden in. Zonder mesh krijg je een blok-placeholder op ware grootte.
  Een andere maatschappij met een eigen model: voeg een `Livery` toe (`Airline` = ICAO-code, bijv. `DLH`, plus `Mesh` en `MeshOffset`).
  Een vliegtuig met callsign DLH123 krijgt dan dat model. `ATC.SetupFleet` rekent de `MeshOffset` zelf uit.
- **Weerbeeld**: dupliceer een `DA_Weather_*` in `/Game/Environment/Weather`. `ATC.Weather <naam>` werkt meteen.
- **Route**: sleep een `TaxiRoute` in de map, teken de spline over de taxibaan en geef een `RouteTag` (tags staan in `Plugins/ATCSim/Config/Tags/ATCSimTags.ini`). Stops: sleep een `TaxiStopMarker` naast de lijn en zet hem in `Stop Markers`. Snelheidszones en waypoints gaan op afstand (cm).
  - **Uit stukken**: vul `Legs` met losse taxibaanstukken (routes). De spline volgt ze achter elkaar met een ronde bocht op elke aansluiting (`JoinRadiusCm`); stops, waypoints en zones komen mee. Tijdens het spelen: `StartTaxiVia([stukken])` op de TaxiController (Blueprint), of `ATaxiRoute::JoinRoutes`.
  - **Parallelle baan**: zet een tweede route met hetzelfde begin en eind over de andere taxibaan in `Alternatives`. Bij het begin van de taxi kiest het netwerk de baan (zie hieronder).
- **Standplaats**: `BP_Stand` neerzetten (pijl = neus), `Stand Tag` invullen, pushback-optie met een route die bij de standplaats begint.
- **Luchthaven**: `DA_Airport_<ICAO>` met map, torenposities en geo-referentie: `Latitude`/`Longitude` liggen op `MapOrigin`,
  `NorthOffsetDeg` is de yaw van het ware noorden (-90 = noorden langs -Y). `GeoToMap(lat, lon)` rekent een WGS84-positie om naar de map.
- **Scenario**: dupliceer `DA_Scenario_Showcase`. Vliegtuigen krijgen een standplaats en route; events zijn opdrachten op een tijd na de start.

## Taxibaanlampen (follow the greens)
Elke taxiroute heeft groene middenlijnlampen (om de 15 m, in bochten 7,5 m) en een rode stopbar bij elke stop die klaring nodig heeft.
- Taxiet er een vliegtuig, dan gaan de lampen tot 400 m vóór het vliegtuig aan, tot aan de eerstvolgende stopbar. Achter het vliegtuig gaan ze uit.
- De stopbar is rood tot de klaring (`ATC.Cmd <callsign> Continue`). Daarna is hij uit tot het vliegtuig 40 m voorbij is, dan gaat hij weer aan.
- De stopbar ligt op de geschilderde stopstreep (het waypoint met dezelfde naam als de stop), anders 30 m voorbij de stop.
- Pushbackroutes van een standplaats hebben geen lampen.
- Alle lampen van een route zijn één draw call. Een brandende lamp wordt groter met de afstand (en kleiner bij inzoomen),
  zodat hij vanuit de toren een paar pixels groot blijft. Overdag en 's nachts even fel op het scherm.
- Instellen per route: `Taxi|Lights` (aan/uit, afstand, breedte stopbar, hoe ver vooruit).
- De lampen worden pas bij Play gebouwd; in de editor-viewport zie je ze niet. Daar crashte de editor op
  ("Shader compilation failures are Fatal"): de selectie-pass van de editor kan geen pipeline maken voor een materiaal dat vertices verschuift.
  De maps zelf (ook EHAM) zijn er niet voor aangepast.

## Verkeer: spawn manager, afstand houden en een drukke dag
**Spawn manager** (`UAircraftSpawnSubsystem`). Zet vliegtuigen tijdens het spelen op een route.
- Een `FAircraftSpawnRequest` bevat route (of `RouteTag`), plek in m, type of `UTrafficMix`, callsign (leeg = automatisch, bijv. KLM1427),
  eindstop of -afstand, `bStartTaxi`, `bRemoveAtRouteEnd` en `InitialSpeedKt`.
- `RequestSpawn` zet het vliegtuig meteen neer, of zet het in een wachtrij als de plek bezet is. De rij wordt elke 0,5 s opnieuw geprobeerd,
  wie eerst vraagt gaat eerst. Zo staan vliegtuigen nooit in elkaar.
- Gespawnde vliegtuigen zijn gewone `AATCAircraft`: dezelfde taxi-, pushback-, snelheids-, lampen- en afstandslogica.
- Blueprint: `RequestSpawn`, `TrySpawn`, `Despawn`, `MakeCallsign`, event `OnAircraftSpawned (Aircraft, Callsign)`.

**Spawnpunt** (`BP_AircraftSpawnPoint`). Sleep hem naast een route: hij springt op de lijn en wijst langs de route. Instellen: `SpawnPointTag`,
`TrafficMix` of `Type`, `EndStop`, `Count`/`IntervalSeconds` voor een stroom, `bSpawnOnBeginPlay`. Blueprint: `SpawnOne`, `StartStream`, `StopStream`,
`MakeRequest` (aanpassen en zelf aan de spawn manager geven).

**Met de hand op een route.** Zet een `BP_Aircraft_Base` in de map en vul `Placed On Route` in: in de editor springt hij op de lijn, bij Play taxiet hij
(`bTaxiOnBeginPlay`) tot `PlacedEndStop` of het eind van de route.

**Data.** `UAircraftTypeData` (afmetingen, snelheden, model), `UTrafficMixData` (welke types met welke maatschappijen en hoe vaak; `DA_TrafficMix_EHAM`).
Scenario's: `Spawns` (tijd, spawnpunt of route, aantal, interval) en `bStartTrafficDirector`.

**Afstand houden.** Elk taxiënd vliegtuig kijkt vijf keer per seconde zijn route vooruit, tot remafstand + 150 m.
- Staat er een ander vliegtuig op (lichaam + spanwijdte), dan remt het en stopt 60 m achter de staart (`TaxiSeparationM`, Project Settings > ATC Sim > Traffic).
- **Altijd op de lijn.** Het hoofdlandingsgestel staat exact op de spline (getest: 0,00 cm ernaast). Verkeer verandert alleen de snelheid.
- **Reserveringen** (`UTaxiNetworkSubsystem`). Elk vliegtuig rijdt alleen over taxibaan die voor hem gereserveerd is:
  - van net achter de staart tot remafstand + 150 m vooruit;
  - een kruising in één keer tot de staart erdoor is, zodat niemand op een kruising stilstaat;
  - een tweerichtingsstuk (een andere route tegengesteld binnen 90 m) alleen als niemand er tegemoet komt. Dan zit het op slot tot hij er
    doorheen is. Wachten gebeurt 100 m ervoor.
- **Wachtrij.** Wie het langst wacht, gaat eerst, bij kruisingen, tweerichtingsstukken en pushbacks. Een landing telt alsof hij al 2 min
  wacht (`LandingHeadStartSeconds`): hij gaat vóór wie korter wacht, en wie langer wacht houdt hem in de stapel.
- **Pushback** start pas als het pushbackpad en het eerste stuk van de taxiroute erna vrij zijn. Op de eerste 700 m van die taxiroute
  mag dan niemand meer tegen hem in komen (`ReserveAheadM`): een pier-taxibaan is van de vertrekker tot hij erdoor is.
- **Landing** vraagt op final zijn afrit aan: tot 90 m naast de baan plus de eigen lengte, op de aankomstroute of een parallelle baan
  die vrij is (daarna blijft hij op die baan). Is er 1,5 km voor de drempel nog geen vrij, dan maakt hij een doorstart en verlaat hij
  het circuit.
- **Wachtstapel.** Een landing verschijnt pas op final als een afrit nu vrij te maken is (niemand erop, niemand die er langer op wacht)
  en de vorige landing op die baan 75 s eerder kwam. Hoogstens 5 min, daarna komt hij toch (`MaxHoldingSeconds`,
  `LandingSpacingSeconds`).
- **Parallelle banen.** Een route met `Alternatives` krijgt de baan zonder tegenliggers, anders de rechter (`bKeepRightOnParallelTaxiways`).
- **Instellen:** `TaxiSeparationM`, `TrafficLookAheadM`, `WingtipClearanceM`, `LandingHeadStartSeconds`, `MaxHoldingSeconds` en
  `LandingSpacingSeconds` in Project Settings > ATC Sim > Traffic.
- **Controle.** Een botsingscontrole kijkt 4× per seconde of twee vliegtuigen overlappen, en een deadlockcontrole of vliegtuigen in een
  kring op elkaar wachten (ook achter elkaar). Na 60 s geeft één uit de kring 30 s zijn stuk vóór de neus en zijn plek in de rij terug.
  Beide staan in de log en in `ATC.Network`.
- In de status staat bijvoorbeeld "Taxiing 0 kt, behind KLM1201". Event: `Event.Taxi.Traffic`.

**Traffic director** (`ATrafficDirector`, in EHAM: `TrafficDirector_EHAM` met 71 gates: 26 van `ATC.SetupEHAM` en 45 van de geparkeerde
modellen, zie hieronder). Laat een gewone dag zien:
1. Aankomsten vliegen de eindnadering (6 km, 3° glijpad), flaren, zetten ongeveer 450 m achter de baandrempel neer en remmen af tot 40 kt
   op 36C of 36R. Daarna nemen ze de afrit en taxiën ze naar een vrije gate, het laatste stuk over de lead-in-lijn: neus naar de terminal.
   Kruisend verkeer wacht zolang de landingsbaan bezet is (`bVisibleLandings`, `LandingRunwaySeconds`).
2. Na de turnaround (4–8 min, ingekort) volgt pushback naar het noorden, oosten, zuiden of westen.
3. Daarna taxiën ze naar 36L of 24, krijgen hun lijn-op-klaring en starten echt: aanloop, rotatie bij VR, klimmen.
- Vrije gates passen bij het type: E, F, G en de hoge D-nummers zijn voor wide-bodies.
- Een collega geeft de klaringen, met één vertrek per baan per 80 s; kruisingen wachten op een vrije baan. Met `ATC.TrafficDirector auto 0` of scenario `EHAM_DagZelf` geef je ze zelf.
- Bij de start staat er ook al een vertrekker in de rij voor 36L en voor 24 (`bDeparturesAtStart`): de eerste starts komen na een paar minuten.
- **De geparkeerde vliegtuigen van de map rijden mee.** Een gate met `ParkedModels` krijgt bij de start altijd een vliegtuig van hetzelfde
  type en dezelfde maatschappij (`ParkedType`, `ParkedAirline`); het model zelf wordt verborgen. Ze vertrekken verspreid over het eerste uur
  (`ParkedModelDepartureSeconds`), niet allemaal tegelijk. Wordt zo'n vliegtuig weggehaald vóór zijn pushback (een scenario maakt het
  vliegveld leeg), dan staat het model er weer. 59 van de 61 modellen doen mee; een kopie die precies op een ander model stond verdwijnt
  met dat model, en een NCA 747 ver buiten het platform (geen taxilijn in de buurt) blijft staan.
- **Geen pushback door een geparkeerd vliegtuig.** Voor de pushback kijkt de director langs de pushbackroute en het platformdeel van de
  taxiroute (elke 5 m) of de romp en vleugels een ander vliegtuig raken, ook een geparkeerd vliegtuig. Zo ja, dan wacht hij 30 s. Een
  aankomst gaat ook alleen naar een gate waarvan het laatste stuk vrij is (een A380 op G9 houdt G7 dicht).
- **Vertrekmanagement**: hoogstens 5 vertrekkers tegelijk op weg naar dezelfde baan (`MaxDeparturesTaxiing`); de volgende blijft nog even
  aan de gate, zoals met de opstarttijden van CDM.
- In de editor: `Gates`, `TrafficMix`, `ArrivalsPerHour`, `TurnaroundSeconds`, `InitialOccupancy`, `ParkedModelDepartureSeconds`,
  `MaxDeparturesTaxiing`, `bAutoClearances`, `bStartOnBeginPlay`.
- Vloot (`DA_TrafficMix_EHAM`): KLM (787-9, 787-10, 777), KLM Cityhopper (E190), Transavia en TUI (737), easyJet, Vueling en Iberia Express (A320),
  Finnair, Lufthansa, Qatar en Singapore (A350), Delta (A330), Korean, American en Air New Zealand (777), Norwegian (787-8), FedEx (777F),
  China Airlines Cargo en NCA (747), Emirates (A380). Maatschappijen zonder stemfragment worden in de radio gespeld (Quebec Tango Romeo ...).

Gemeten in EHAM (26 okt, 40 min speltijd): 10 landingen, 11 doorstarts, 17 pushbacks, 18 starts, 198 radio-uitzendingen, geen overlap
en geen vastlopers. De meeste doorstarts: aankomsten van 36C en vertrekken naar 36L rijden ten noorden van de terminal over dezelfde
taxibaan tegen elkaar in (zie sectie P van het ontwerpdocument). Op 21 okt (25 min, nog zonder reserveringen) waren het 16 aankomsten,
19 pushbacks en 10 starts, maar toen keek nog niets naar overlap.

Gemeten op 27 okt, met de geparkeerde modellen erbij (62 vliegtuigen aan de gates bij de start, 40 min speltijd): 11 landingen,
12 doorstarts, 25 pushbacks, 18 starts, 247 radio-uitzendingen, geen overlap en geen vastlopers. Daarbij bleek dat aankomsten sinds
21 okt dwars op hun stand eindigden (de route ging naar de geschilderde lijn die het dichtst bij het hoofdlandingsgestel lag); nu komen
ze over de lead-in-lijn binnen en zijn alle gates opnieuw gebouwd.

**Verder richting volledige AI-simulatie:**
- vluchtschema's (OAG-achtig) in plaats van een gemiddeld aantal per uur;
- doorstarts die terugkomen in het circuit, en naderingen met radarafstand tussen de vliegtuigen;
- echte baanwissels (zuid- en noordbedrijf);
- slot- en de-icing-wachtrijen;
- multiplayer met instructeur;
- taxiroutes op aanvraag (A* tijdens het spelen) in plaats van vooraf per gate.

## GMI-camera's op de torenschermen
Een GMI-camera kan pannen, kantelen en optisch zoomen, met presets en vloeiende overgangen. Het beeld gaat naar een render target en staat
real-time op een scherm van het torenmodel.

**Schiphol (Toren Centrum).** Vijf camera's, elk op een echt scherm van de consoles (`ATC.SetupGMI`):
| Scherm | Camera | Wat |
|---|---|---|
| 1: je eigen console, het middelste scherm (waar de vluchtlijst stond) | `GMI-TWR` | Koepelcamera op het dak van de toren (108 m) die helemaal rond draait. Een preset per baankop, in de volgorde waarin het verkeer ze gebruikt: `Cam.EHAM.Threshold36L`, `24`, `36C`, `36R`, `18R`, `06`, `18C`, `18L`, `27`, `09`, `22`, `04` |
| 2 t/m 5: de console rechts (cameradesk) | `GMI-G7`, `GMI-E6`, `GMI-D43`, `GMI-C9` | Platformcamera's, 22 m hoog aan de terminalkant van hun gate, met hun voorkant naar de gate. Presets: de gate en het platform erachter |

De radar en de strips van je eigen console blijven. Het scherm wordt zwart, het beeld (16:9) staat bovenaan en eronder camera-ID, UTC-tijd,
brandpuntsafstand en preset, zoals op echte camerabeelden. Geel = de camera die je bestuurt.

**Bediening** (Enhanced Input, `/ATCSim/Input/IMC_GMI`). De invoer gaat alleen naar de PTZ-controller, nooit naar de capture zelf.
| | Toetsen | Gamepad | VR (linkerhand) |
|---|---|---|---|
| Pannen / kantelen | pijltjes | rechterstick | thumbstick |
| Zoomen | PageUp / PageDown, + / - | triggers | Y / X (Index: B / A) |
| Volgende camera | Tab | schouderknoppen | thumbstick-klik |
| Preset | 1 t/m 9, komma / punt | d-pad links / rechts | |
| Volgende lens | L | Y | |
- Kijk naar een camerascherm en gebruik de bediening: dan bestuur je die camera. Kijk je naar buiten, dan blijft het de laatst gekozen camera.
- Pannen gaat langzamer naarmate je verder inzoomt: 40°/s bij 24 mm, 2,8°/s bij 400 mm.
- Andere toetsen: pas `IMC_GMI` aan, of kies een eigen mapping in Project Settings > Plugins > ATC Sim Camera.

**Lenzen** (`DA_LensSet_GMI`): Wide 24 mm (73,7°), Normal 50 mm (39,6°), Tele 100 mm (20,4°) en Custom 400 mm (5,2°), sensor 36 mm. Een extra
lens is een extra regel. Het bereik staat op de camera: `FocalLimitsMm` (24 tot 400 mm), `PanLimitsDeg` (±170°, een kabelstop; 360° of meer
= helemaal rond) en `TiltLimitsDeg` (-45° tot 15°). Zoomen gaat logaritmisch: 24 → 50 mm duurt even lang als 50 → 100 mm.

**Renderbudget.** Elke camera rendert de scène opnieuw, dus:
- alleen een camera waarvan een scherm in beeld is (of als het instructeurspaneel open is) tekent;
- hoogstens twee tegelijk live, op 20 beelden/s (in VR 15): eerst de camera die je bestuurt, dan de schermen die het grootst in beeld zijn.
  De rest tekent 5 beelden/s (`MaxLiveCameras`, `BackgroundRateHz`);
- hoogstens twee beelden per frame (`MaxCapturesPerFrame`);
- 1280×720 en FXAA (temporele AA heeft de frames ervoor nodig), zonder motion blur, volumetrische mist, SSR, SSAO en contact shadows;
- niets verder dan de luchthavengrens vanaf die camera (de verste baankop + 500 m);
- de belichting past zich snel aan, zoals bij een camera: een scherm dat net in beeld komt is niet eerst donker.

Gemeten op 28 okt (GTX 1080 Ti, desktop 1280×720, drukke dag; camera's aan tegen `atc.CameraCaptures 0` in hetzelfde beeld):
| In beeld | Camera's die tekenen | GPU per frame extra | Per camera, gemiddeld over de frames |
|---|---|---|---|
| Eigen console (`GMI-TWR` live) | 1 | 1,17 ms (90 fps) | 1,17 ms |
| Cameradesk (2 live, 2 op 5 Hz) | 4 | 3,55 ms (66 fps) | 0,89 ms |
| Instructeurspaneel (2 live, 3 op 5 Hz) | 5 | 4,71 ms (67 fps) | 0,94 ms |

Dat is binnen het budget van 1,5 ms per actieve camera. Eén beeld kost ongeveer 5 ms GPU: dat de camera's 20 keer per seconde tekenen en
niet elk frame, maakt het verschil.

**Instructeur.** `ATC.CameraUI` zet alle camerabeelden rechtsboven op het desktopscherm (UMG, dezelfde render targets). In de headset zie je
het niet. Wisselen wat een torenscherm toont: `ATC.Monitor 2 next` of `ATC.Monitor 1 GMI-C9`.

## Radioverkeer
Alles wat de vliegtuigen doen wordt radioverkeer in ICAO-fraseologie, met readbacks, op één frequentie (één uitzending tegelijk):
- pushback-aanvraag en goedkeuring ("face north");
- taxiklaring naar het holding point;
- "holding point, runway tree six left, ready for departure", en de startklaring met de wind uit het weer;
- kruisen van een baan;
- aankomst ("vacated runway tree six center", "taxi to stand Golf seven");
- na de start: "contact departure, good day".

Cijfers en letters worden uitgesproken zoals ICAO het wil (tree, fife, niner, Golf, Echo). Maatschappijen gebruiken hun roepnaam (Easy, Finnair, Speedbird).

**Hoe het klinkt.** De stemmen zijn de Windows-stem, met een VHF-radiogeluid (band 400–2800 Hz, wat vervorming, ruis, squelch). Piloten klinken lager dan de controller.
Opnieuw maken: `powershell -File Plugins\ATCSim\Tools\MakeRadioVoice.ps1`, daarna `ATC.ImportRadioVoice`.

**Tekst.** Elke uitzending is ook een regel:
- op het radiologscherm links in de toren;
- in de log;
- als event `URadioSubsystem::OnRadioMessage`.

**Bediening.** Volume met `atc.RadioVolume 0.8`, uit met `ATC.Radio 0`.

Er is maar één Windows-stem op deze pc (Zira). Met meer stemmen (Windows Instellingen > Spraak) kan het script piloten een eigen stem geven.

## Productie
- **De game-build compileert.** `Build.bat Import_testing_obi Win64 Development` lukt. Er zaten editor-functies in de game-modules (ook in de eigen
  airport-tools: `GetActorLabel`, `IsNaniteEnabled`, `bIsSpatiallyLoaded`), waardoor het project niet te packagen was. Die zijn vervangen of afgeschermd.
- **Cookinstellingen** (`Config/DefaultGame.ini`): alle zes maps, plus `/ATCSim`, `/Game/Scenarios`, `/Game/Aircraft`, `/Game/Environment`
  en `/Game/Camera` worden altijd gecookt. Die laden scenario's, lampen, radio en types tijdens het spelen op naam. `TrafficMix` en `RadioVoice` zijn primary asset types.
- **Geen crash als de wereld al weg is.** Elke `EndPlay` controleert dat (een test crashte hier eerst).
- **Lampen** worden pas gebouwd als een route gebruikt wordt (78 routes in EHAM) en niet in de editor.
- **Logs.** Geen per-route of per-frame meldingen. Alleen gebeurtenissen (`LogATC`) en waarschuwingen.
- **Prestatie EHAM drukke dag** (GTX 1080 Ti, desktop): 55 fps, game thread 10,7 ms, render thread 18 ms, GPU 16,9 ms, met ~20 vliegtuigen en 4 camera's.
  Zonder verkeer was het 82 fps. Voor 90 Hz VR is meer nodig, zie VR hieronder.
- **Taxibaanlampen statisch** (2-11-2026). De lampen-ISM van elke `ATaxiRoute` stond op Movable: ~99.000 dynamische instanties die elk frame opnieuw door de visibility gingen.
  Ze schakelen via custom data, dus Static is genoeg. Meting EHAM vanuit de toren, 1280×720, 60 s `csvprofile`:
  frame 29,1 → 10,6 ms gemiddeld (34 → ~95 fps), p99 39 → 15 ms, render thread 29 → 10,6 ms, GPU 26,8 → 9,9 ms, wachten op visibility 18,7 → 4,0 ms.
- **Niet gedaan:** een volledige package/cook-run (het project is 52 GB, eerste shadercompilatie duurt erg lang) en een Shipping-build.
  In Shipping werkt de console niet: instructeursbediening moet dan via UI of Blueprint.

## Tests
Session Frontend > Automation > `ATCSim` (26 tests), of zonder editor:
`UnrealEditor-Cmd.exe Import_testing_obi.uproject -ExecCmds="Automation RunTests ATCSim" -TestExit="Automation Test Queue Empty" -nullrhi -FORCELOGFLUSH`
(`-TestExit` stopt het proces hard; zonder `-FORCELOGFLUSH` mist het eind van de log soms, en lijkt het alsof er tests ontbreken.)

Nieuw: `Environment.Metar` (METAR-parser), `Environment.DynamicWeather` (geen sneeuw in juli, geen mist 's middags), `Core.RealTimeClock`,
`Aircraft.NoseGearOnLine` (beide wielen binnen 1-3 cm op de lijn in bochten van 30 m), `Vehicles.DriveAndPark`, `Vehicles.ServiceSpots`
(plekken en routes vrij van romp, gestel, motoren en elkaar, voor A320 tot A380) en `Vehicles.TurnaroundService` (service komt, en is na
de pushback weg).

De camera heeft twee tests: `Camera.PTZ` (een preset komt met pan, tilt en zoom tegelijk aan, de zoom logaritmisch; limieten; om de
kabelstop heen; de koepel draait door; langzamer pannen bij inzoomen; lens wisselen) en `Camera.CaptureBudget` (twee live op 20 Hz, de rest
op 5 Hz, een camera die niemand ziet tekent niets, nooit meer dan twee beelden per frame).

De taxi-discipline heeft eigen tests: `HeadOnOneTaxiway` (tegenliggers op één baan), `CrossingQueue` (één tegelijk op een kruising,
niemand stopt erin), `ParallelLanes` (rechts houden, of de andere baan bij tegenverkeer), `RouteFromLegs`, `LandingPriority` (een
landing gaat vóór wie op de grond wacht), `DeadlockBreaker` (een kring wordt gezien en opgelost) en `ParkedModelStandIn` (een gate met
een geparkeerd model krijgt een vliegtuig van dat type en die maatschappij; het model komt terug als dat vliegtuig weggaat zonder te
vertrekken). De tests waarin gereden wordt,
controleren ook dat niemand overlapt en dat het hoofdgestel binnen 1 cm op de lijn blijft.

## Echte tijd en echt weer
Project Settings > Plugins > ATC Sim > *Environment*.
- **Echte tijd** (`bRealTimeClock`, aan). De klok is de UTC-tijd van deze pc: is het buiten nacht, dan is het in de toren nacht.
  Hij telt geen frametijden op, dus hij loopt niet uit na een hapering of een gepauzeerde editor. `ATC.Time` en `ATC.TimeScale`
  zetten hem los, `ATC.RealTime` zet hem terug. De monitoren tonen UTC (zoals in de luchtvaart); `ATC.Status` laat ook de lokale tijd zien.
- **Gewoon Play** (`bLiveEnvironmentOnPlay`, aan): ook zonder scenario komt de dag/nacht- en weer-rig in de map, met de echte tijd en
  het weer volgens de weermodus. Uit: de map houdt het licht waarmee hij is opgeslagen.
- **Scenario's**: `bLiveEnvironment` (aan) = echte tijd en echt weer; het `Weather`-preset van het scenario is het beeld tot de METAR
  binnen is (een seconde of zo). Uit = `StartTimeUTC`, `TimeScale` en `Weather` van het scenario, en het weer blijft zoals het is
  (een oefening die mist bij nacht nodig heeft).
- **Weermodus** (`WeatherMode`, `ATC.WeatherMode`):
  - `live` (standaard): de echte METAR van de luchthaven (`MetarUrl`, aviationweather.gov), elke 10 min opgevraagd (`MetarRefreshMinutes`).
    Een nieuwe METAR schuift er in 2 min in (`LiveWeatherBlendSeconds`); de eerste bij de start meteen. Een luchthaven zonder echte
    ICAO-code (de showcase) krijgt het weer van `DefaultMetarStation` (EHAM). Geen internet of geen antwoord: het weer verandert dan
    vanzelf (zoals `dynamic`) tot er wel een METAR is.
  - `dynamic`: elke 10-30 min (`DynamicWeatherMinutes`) een nieuw, logisch weerbeeld dat er in 3-8 min (`DynamicBlendMinutes`)
    in schuift: meestal een kleine stap (helder → wat wolken → bewolkt → buien, en terug), mist alleen 's nachts en 's ochtends, sneeuw
    alleen in de winter, wind die wat draait. Met `ATC.TimeScale` gaat het weer mee sneller.
  - `fixed`: alleen presets, zoals vroeger. `ATC.Weather` zet de modus op `fixed`, zodat de keuze van de instructeur blijft.
- **Wat er uit een METAR komt** (`FMetarReport`): wind (ook `VRB`, vlagen, m/s), zicht (`9999`, `CAVOK`, meters of statute miles), wolken
  (`FEW` … `OVC`, `VV`, `CB`/`TCU`), neerslag met sterkte (`-RA`, `+TSRA`, `SHSN`, `DZ`, `RASN` op temperatuur), mist (`FG`, `BR`), recente
  regen (`RERA`), temperatuur en QNH. TEMPO/BECMG en RMK tellen niet mee. Daaruit: bewolking, wolkenbasis, zicht, regen of sneeuw, natte
  of besneeuwde grond, zonsterkte en schaduwzachtheid. De wind gaat naar de radio (startklaring).
- Het weer verandert alleen vanzelf als er een rig is die het laat zien; zonder rig worden ook geen METARs opgevraagd.

## Grondvoertuigen
Kleine voertuigen van eenvoudige vormen in hun eigen kleuren, dus zonder content: een **pushbacktruck** bij elke pushback, en rond
geparkeerde vliegtuigen een **bagagetrein** (trekker met twee karren die zijn spoor volgen), een **tankwagen** en een **cateringwagen**.
- Ze komen als een vliegtuig op zijn stand staat: eerst de bagage (na 5-15 s), dan de rest. Tanken en catering zijn na een paar minuten
  klaar en rijden weg; de bagage blijft tot de pushback. Zodra de pushback is aangevraagd rijdt alles weg, de bagagetrein een paar seconden later.
- Vaste plekken, berekend uit het type (romp, hoofdgestel, motoren): de bagage rechts bij het achterruim, de cateringwagen haaks op de
  rechter voordeur (de slurf zit links), de tankwagen links onder de vleugel achter het gestel. Ze komen en gaan over een eigen baan
  buiten de motoren (bij een A380 of 747 tussen de binnenste en de buitenste motor), en de tankwagen en de cateringwagen rijden eerst
  achteruit weg. Getest voor alle types van de vloot: niets rijdt of staat in de romp, het gestel, de motoren of in een ander voertuig.
- De pushbacktruck staat onder het neuswiel. Na het loskoppelen rijdt hij opzij weg (vóór het vliegtuig gaat taxiën) en terug naar de stand.
  Een eigen tug-model van het project (`TugClass` van de pushback controller) werkt zoals vroeger.
- Vliegtuigen die er bij de start al staan krijgen meteen hun service, de helft ervan (`ServicedAtStartShare`).
- Kosten: een voertuig is 3 tot 7 simpele meshes met één gedeeld materiaal per kleur, alleen het grootste deel werpt schaduw, ze tekenen
  niet verder dan 4 km, en ze tikken alleen als ze rijden. Hoogstens 40 tegelijk (`MaxServiceVehicles`). Zwaailicht (een klein lampje
  zonder schaduw, alleen tijdens het rijden): `atc.VehicleBeacons 0` zet ze uit.
- `ATC.Vehicles 0/1`, of `bGroundVehicles` in Project Settings > Plugins > ATC Sim > *Vehicles*.

## Precies op de lijn en soepel
- **Beide wielen op de lijn.** Het hoofdgestel stond al exact op de spline. Het neuswiel nu ook: het staat één wielbasis in een rechte
  lijn verder óp de lijn (in een bocht is dat iets verder langs de lijn dan één wielbasis). Eerst stond het één wielbasis langs de lijn,
  en dan ~30 cm buiten de lijn bij een 787 in een bocht van 30 m. De koers volgde de lijn ook met wat vertraging (`RotationSmoothing` 6);
  nu exact (`RotationSmoothing` 0). Alleen een koersverschil bij het begin van een nieuw pad (eind van de pushback → taxiroute) vervaagt
  over de eerste 10 m (`JoinBlendDistanceCm`). Test: `Aircraft.NoseGearOnLine`.
- **Geen schommelende snelheid in bochten.** De bochtsnelheid kwam uit punten die met het vliegtuig mee schoven, waardoor de limiet per
  frame een beetje anders was. Nu heeft elke route een vaste tabel (`GetCurvatureTable`): elke frame dezelfde waarden en geen
  spline-berekeningen meer per vliegtuig per frame.
- **GMI-camera's.** Met de joystick of de toetsen trekt het beeld nu soepel op en remt het af (`InputSmoothingSeconds` 0,12 s), in
  plaats van met een schok. De beelden komen in een vast ritme van hele frames (20 Hz bij 55 fps was 3-3-2 frames: hapering), en als
  twee camera's in hetzelfde frame zouden tekenen wacht één een frame (gelijkmatige GPU-last). De camera die je beweegt tekent zolang
  hij beweegt minstens 30 beelden per seconde (`MovingRateHz`). Test: `Camera.CaptureBudget`, `Camera.PTZ`.
- **De oude `AircraftSplineActor`** (game-module) tikte niet (`bCanEverTick` stond uit), dus hij bewoog nooit; nu wel, met snelheden in
  knopen, de neus naar de gate bij de pushback, en de landing rolt uit tot het eind van zijn spline.

## Soepeler (performance)
- De zon draait pas na een zichtbare stap (0,1°, in echte tijd elke 24 s) in plaats van elke seconde: elke draai van het zonlicht gooit
  de schaduw-cache weg.
- Een skylight zonder real-time capture wordt alleen opnieuw opgenomen als de lucht zichtbaar veranderd is (zon 1°, andere wolken of
  zicht, schemer), niet elke seconde (een hele cubemap).
- Lange weerovergangen sturen de rig 10 keer per seconde bij in plaats van elk frame; de rig zoekt zijn wolken en MPC één keer op.
- Bochtsnelheid uit de vaste routetabel (zie hierboven).

## KLM-livery
De KLM 777 had de speciale "orange pride"-livery en de 787-10 de "100 jaar"-livery; de geparkeerde KLM-toestellen van de Schiphol-map
(gewone KLM-kleuren) werden bij het starten van het verkeer vervangen door die modellen.
- **Verkeer draagt de modellen van de map** (`bWearMapModels` op de traffic director, aan): een vliegtuig van een type en maatschappij
  waarvan de map een geparkeerd model heeft, krijgt een kopie van dat model (alle mesh-delen, met de materialen van de map), uitgelijnd
  zoals een typemodel: hoofdgestel op de oorsprong, neus op de neus-offset, wielen op de grond, geschaald naar de typelengte, en rechtgezet
  als het geparkeerde model een paar graden schuin op zijn stand stond. Heeft de map er meer (een gewone KLM 777 en de orange pride),
  dan wint de gewone. Een geparkeerd toestel dat vertrekt houdt zijn eigen model.
- **`ATC.SetupFleet`** neemt voor KLM nu de gewone modellen (`KLM_Boeing_777_306_ER`, `KLM_Boeing_787_9_Dreamliner`) als die als één mesh
  in het project staan, en valt anders terug op de oude. Daarna `ATC.SetupFleet` opnieuw draaien om de types bij te werken.

## Weer: hoeveelheid neerslag
`PrecipitationRate` (0..1) gaat via `MPC_Environment.PrecipitationRate` naar de materialen `M_RainStreak` en `M_SnowFlake`:
die tekenen dat deel van de deeltjes (elk deeltje heeft een vaste willekeurige grootte; die dient als willekeurig getal).
Het effect zelf draait altijd op 3000 deeltjes, dus de kosten zijn bij lichte en zware regen gelijk. Windrichting gaat nog niet naar de deeltjes.

## VR
- Het VR-renderprofiel (Project Settings > Plugins > ATC Sim) gaat aan zodra er stereo actief is, al vóór het eerste frame van de map (ook zonder scenario). Om het op een monitor te meten:
  `atc.RenderProfile 2` vóór `ATC.Scenario` (0 = automatisch, 1 = altijd desktop).
- Meting met `-emulatestereo` (GTX 1080 Ti, 2 × 1720×1440): Rotterdam schematisch 19 ms GPU, showcase 21,5 ms, Schiphol mono
  3440×1440 15 ms. Volumetrische wolken kostten ~20 ms in stereo; het profiel zet ze nu goedkoper. 90 Hz (11,1 ms) wordt nog niet gehaald.
- **Crash Schiphol in stereo opgelost** (2-11-2026). De post process volume van EHAM zet screen space GI aan; dat pad kan maar één view aan
  (engine-ensure "one AO texture per view"), daarna crashte soms een render-taak. Het profiel ging pas uit bij een scenario; nu al bij de start van de map.
- **Meting EHAM Ochtendpiek in stereo** (`-emulatestereo`, 2 × 1720×1440, GTX 1080 Ti, 2-11-2026):
  | | Frame | GPU |
  |---|---|---|
  | Profiel van 13-10 (TSR, SSAO) | 30,1 ms | 29,2 ms (TSR alleen 8,8 ms) |
  | + TAA, geen SSAO | 19,3 ms | 18,5 ms |
  | + wolken 64 samples (nu het profiel) | 18,0 ms | 17,2 ms |
  | + `r.ScreenPercentage 75` (niet in het profiel) | 14,4 ms | 13,7 ms |
  90 Hz (11,1 ms) haalt de 1080 Ti hier niet; 45 Hz met reprojectie (SteamVR motion smoothing, 22,2 ms) wel. Vaste ~2,6 ms GPU
  hangt niet af van de resolutie (vermoedelijk de GMI-camerabeelden). Wolkenmodus 3 kost juist 13 ms. De resolutie tijdens het spelen
  veranderen (`r.ScreenPercentage`) crasht in geëmuleerde stereo: zet hem vóór het laden.

## Vliegtuigmodellen
Elk model wordt apart op lengte en spanwijdte geschaald (787-9: 62,8 × 60,1 m). A320, 777-300ER en A350-900 gebruiken de modellen uit
`Content/staticmeshplane`; de neus wordt automatisch gevonden (de staart is het hoogste punt). De 737-800 heeft nog geen model (blokken).

## Nog niet gedaan
- VR op een echte headset meten (zie `Docs/ATC_VR_Bouwblokken.html`, VR-benchmark: alleen met `-emulatestereo` gemeten).
- Multiplayer (instructeur + VR).
- Instructeurs-UI (nu console-commando's; in een Shipping-build is er geen console).
- De EHGG- en EHBK-modellen ×100 schalen, zodat de echte modellen gebruikt kunnen worden.
- Taxiroutes zijn per gate vooraf berekend (A* over de verf), niet getoetst aan de echte Schiphol-taxiprocedures.
