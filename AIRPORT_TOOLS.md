# Airport tools (EHAM)

> **Nieuw: ATC Sim Bouwblokken.** De herbruikbare systemen (taxi, pushback, GMI-camera, weer, dag/nacht, luchthavens) staan in de plugin `Plugins/ATCSim`. Uitleg: `Plugins/ATCSim/README.md`, ontwerp: `Docs/ATC_VR_Bouwblokken.html`, demo: map `/Game/Airports/Showcase/Maps/Map_Showcase_ATC`. Dit bestand gaat alleen over de EHAM-map.

Alle commando's typ je in de editor onderin bij **Output Log > Cmd**. Ze staan in `Source/Import_testing_obi/AirportTools.cpp`.

## ATC-scenario in EHAM (13 okt, middag)
- `ATC.SetupEHAM` heeft 6 actors toegevoegd in de outliner-map **ATC**: `Stand_G7`, `Stand_G4` en vier routes
  (`Route_EHAM_G7_Pushback`, `Route_EHAM_G7_Rwy36L`, `Route_EHAM_G4_Pushback`, `Route_EHAM_G4_Rwy24`). Ze zijn onzichtbaar in het spel
  (geen eigen middenlijn; de route volgt de gele lijnen van de map). Verder is er niets aan de map veranderd.
- Kopie van de map van vóór die actors: `Saved/Backup_before_ATCSetup/EHAM.umap`.
- Gewoon Play ziet er hetzelfde uit als eerst. Met `ATC.Scenario EHAM_Ochtendpiek` (console) start het scenario: torenzicht vanuit
  Toren Centrum, KLM1023 G7 → Polderbaan 36L, EZY45LK G4 → Kaagbaan 24. Uitleg: `Plugins/ATCSim/README.md`.
- (21 okt) Het spline-vliegtuig `Aircraft_1` (China Airlines 747 met volgcamera) en zijn FlightPath zijn uit de map gehaald (`Airport.RemoveFlight`).
- (27 okt) De oude splines die je zelf had gezet zijn weg: 11 × `BP_Spline` en de 11 A350's die erover reden (`BP_SplineObject`),
  met `Airport.RemoveSplines`. De 109 taxiroutes van ATC zijn niet aangeraakt. De twee Blueprints staan nog in Content en de map
  `EHAM-REGEN` heeft de splines nog. Kopie van de map van daarvoor: `Saved/Backup_before_RemoveSplines/EHAM.umap`.
- De map is 1:1 en op het ware noorden gebouwd rond Toren Centrum (+X oost, +Y zuid); de OSM-banen liggen binnen 10 m van de baanverf.
- Taxibaanlampen (14 okt): groene lampen gaan aan vóór het taxiënde vliegtuig, rode stopbar bij de holding points tot de klaring.
  Ze worden bij Play op de routes gezet; de map zelf is er niet voor veranderd. Uitzetten: `ATC.Lights 0`.

## Drukke dag op Schiphol (21 okt)
- `ATC.SetupEHAM` maakt nu **26 gates** (B, C, D, E, F, G, H), elk met een pushback-, vertrek- (36L of 24) en aankomstroute
  (van de afrit van 36C of 36R tot nose-in op de gate). Alles in de outliner-map **ATC**.
- Extra in de map:
  - `TrafficDirector_EHAM` (verkeersprogramma);
  - spawnpunten `Spawn_EHAM_Rwy36L` en `Spawn_EHAM_Rwy24`;
  - vier gatecamera's `GMI_G7`, `GMI_E6`, `GMI_D43` en `GMI_C9`;
  - de schermwand `TowerScreen_1..4` en het radiolog-scherm `TowerRadioLog` in Toren Centrum.
- Kopie van de map van vóór deze ronde: `Saved/Backup_before_ATCTraffic/EHAM.umap`.
- Scenario's: `ATC.Scenario EHAM_Dag` (aankomsten, turnarounds en vertrekken met startrol, plus radioverkeer; een collega geeft de klaringen)
  en `ATC.Scenario EHAM_DagZelf` (alle klaringen geef je zelf).
- Rondkijken in de toren zonder muis: `ATC.Turn 36` (schermen rechts), `ATC.Turn -36` (radiolog links). Uitleg: `Plugins/ATCSim/README.md`.
- De projectcode in `AirportTools.cpp`, `Aircraft.cpp` en `FlightPath.cpp` gebruikte editor-functies in de game-module. Daardoor kon het project
  niet gepackaged worden. Dat is opgelost: de game-build compileert.

## Levend Schiphol bij gewoon Play (21 okt, middag)
- Druk op **Play**: je staat in Toren Centrum en het verkeer loopt meteen. 14 vliegtuigen staan aan de gates en er staan 2 vertrekkers in de rij
  voor 36L en 24. Ongeveer elke 1,5 min komt er een landing binnen.
- **Landingen**: vanuit de lucht (6 km, 3°), flare, touchdown op 36C of 36R, uitrollen, afrit, naar de gate.
- **Starts**: startrol, rotatie, klimmen. De eerste na ongeveer 2,5 min.
- **Rechts houden**: tegenliggers gaan elk aan hun eigen kant langs elkaar. Twee grote wide-bodies wachten op elkaar (te breed).
  *Vervangen op 26 okt: vliegtuigen blijven nu op de lijn, zie Taxi-discipline.*
- **Meer modellen**: 12 types met 22 modellen van 20 maatschappijen uit `Content/staticmeshplane` en de Datasmith-vliegtuigen (zie `Plugins/ATCSim/README.md`).
- **Kijken**: `ATC.Watch landing 300`, `ATC.Watch takeoff 300` of `ATC.Watch <callsign>`, en terug met `ATC.Tower`.
- **Weer rustig**: `ATC.TrafficOnPlay 0` in de editor.
- **Taxiroutes gecontroleerd** met `ATC.CheckRoutes`: de 78 routes liggen op het asfalt, op 7 korte stukjes van 20–65 m na (bochten en
  bij de gates). De lijnen die helemaal naast het asfalt lagen, waren de taxi-splines van de oude 747. Die zijn weg.
- **Kopie van vóór deze ronde**: `Saved/Backup_before_LiveTraffic` (map, vliegtuigtypes, luchthaven-data) en `Saved/Backup_before_TrafficOnPlay/EHAM.umap`.

## Baannummers terug (21 okt, avond)
- De baannummers (18L, 36R, ...) en de witte en gele letters op het asfalt waren onzichtbaar. Hun oudermateriaal, op 12 okt nagemaakt in de
  engine-map, staat op Modulate, en wit × asphalt = asphalt.
- `Airport.FixLetters` hangt ze aan een nieuw projectmateriaal `/Game/EHAM/Materials/M_RunwayLetters` (masked: de alpha van de textuur
  snijdt de letter uit). Kopie van de oude materialen: `Saved/Backup_before_FixLetters`.
- Ontdekt: een masked materiaal dat een kopie is van `M_TerrainVT` snijdt niets uit (de vlakken blijven dicht). Daarom bleven de
  boomkaarten en de donkere vlakken toen blokken. `M_RunwayLetters` is daarom vanaf nul gemaakt.
- Bekijken: `Airport.Shots 40 runways` (close-ups van alle drempels), `Airport.MeshUV <mesh>` (UV's van een mesh).

## Taxi-discipline (26 okt)
- Vliegtuigen blijven **exact op hun lijn**: hoofdgestel en neuswiel staan op de spline. Opzij gaan voor tegenliggers is weg.
- Ze rijden alleen over taxibaan die voor hen **gereserveerd** is. Twee vliegtuigen gebruiken nooit hetzelfde stuk. Wie een stuk niet krijgt,
  wacht, en wie het langst wacht gaat eerst. Een kruising wordt in één keer gereserveerd. Een stuk waar twee richtingen over één baan gaan,
  gaat op slot voor de andere richting.
- Een **landing** wacht in de stapel tot zijn afrit vrij te maken is (hoogstens 5 min) en vraagt die op final aan. In de rij telt hij
  alsof hij al 2 min wacht. Is de afrit 1,5 km voor de drempel nog niet vrij, dan maakt hij een doorstart.
- **Parallelle banen**: `ATC.AddParallelRoutes` gaf 31 van de 52 routes een tweede rit over een parallelle taxibaan
  (`Route.EHAM.<gate>.Alt.<rit>`, outliner-map **ATC**). Bij de start van de taxi kiest het vliegtuig de baan met het minste tegenverkeer,
  anders de rechter. Kopie van de map van daarvoor: `Saved/Backup_before_ParallelRoutes/EHAM.umap`.
- **Kijken**: `ATC.Claims 1` (reserveringen in beeld, rood = wacht), `ATC.Network` (alle reserveringen en wachtenden in de log) en
  `ATC.Network plot <naam>` (plaatje in `Saved/ATC`).
- **Botsingscontrole**: elke overlap komt als `Collision monitor` in de log, een vastloper als `Taxi deadlock`. `ATC.Network` telt ze.
- **Gemeten** (40 min speltijd, 4× versneld): geen overlap en geen vastlopers; 10 landingen, 11 doorstarts, 17 pushbacks, 18 starts.
  De meeste doorstarts komen doordat de aankomsten van 36C en de vertrekken naar 36L ten noorden van de terminal over dezelfde
  taxibaan in tegengestelde richting rijden. De vertrekroutes naar 36L hebben (nog) geen parallelle baan.
- Uitleg en ontwerp: `Plugins/ATCSim/README.md` en `Docs/ATC_VR_Bouwblokken.html`, sectie P.

## Alle geparkeerde vliegtuigen rijden mee (27 okt)
- In de map stonden 61 vliegtuigen die nooit bewogen: statische en skeletal modellen uit `Datasmith/Vliegtuigen` (KLM, Cityhopper,
  Transavia, TUI, easyJet, Vueling, Lufthansa, Delta, Korean, American, Air New Zealand, Norwegian, Singapore, FedEx, China Airlines
  Cargo, NCA en Emirates). 14 ervan stonden op een gate van het verkeer: daar parkeerde het verkeer er dwars doorheen.
- `ATC.AddParkedGates` geeft elk model een eigen gate (45 nieuwe, met pushback-, vertrek- en aankomstroute) of koppelt het aan de
  verkeersgate waar het al op stond. Bij Play komt er een vliegtuig van hetzelfde type en dezelfde maatschappij op zijn plek en verdwijnt
  het model. Ze vertrekken verspreid over het eerste uur; daarna komen er weer aankomsten op die gates.
- 13 KLM Cityhoppers staan niet op een stand uit OSM (vooral het regionale platform): hun stand is gemaakt van het model zelf (P1–P13).
- Blijft staan: één NCA 747 op 2,8 km buiten het platform (geen taxilijn in de buurt). Een dubbele KLM 787 die precies op een andere
  stond, verdwijnt samen met die andere.
- De map is nu ongeveer 155 MB (was 56 MB) door de ~200 extra routes. Kopie van daarvoor: `Saved/Backup_before_ParkedGates/EHAM.umap`.
- Zodat het niet botst of vastloopt: geen pushback of aankomst door een geparkeerd vliegtuig heen, hoogstens 5 vertrekkers tegelijk op
  weg naar één baan, vertrekken via de pier-regel (B, C, H naar 24; D t/m G naar 36L), en vrachtvliegtuigen lijnen op vanaf hun eigen
  kant van baan 24.
- Gevonden en opgelost: aankomsten parkeerden sinds 21 okt dwars op hun stand (bij D16 zelfs bijna achterstevoren). Ze komen nu over de
  lead-in-lijn binnen. Daarvoor zijn alle gates opnieuw gebouwd (`ATC.SetupEHAM`, daarna `ATC.AddParkedGates`, `ATC.CheckRoutes` en
  `ATC.AddParallelRoutes`). Kopie van de map van daarvoor: `Saved/Backup_before_RebuildGates/EHAM.umap`.
- **Gemeten** (40 min speltijd, 4× versneld): 62 vliegtuigen aan de gates bij de start, 25 pushbacks, 18 starts, 11 landingen,
  12 doorstarts, geen overlap en geen vastlopers.

## GMI-camera's op de torenschermen (28 okt)
- De camerabeelden staan nu op de echte schermen van het torenmodel in Toren Centrum, niet meer op de losse 2×2-wand in de lucht.
- Je eigen console: het middelste scherm (waar de vluchtlijst stond) toont `GMI-TWR`, een nieuwe koepelcamera op het dak van de toren
  met een preset per baankop. De radar en de strips blijven.
- De console rechts is de cameradesk: de vier platformcamera's (G7, E6, D43, C9).
- Bedienen: pijltjes, PageUp/PageDown, Tab, 1–9 en L (of gamepad, of de linker VR-controller). Kijk naar een scherm om die camera te
  besturen. Alle beelden voor de instructeur: `ATC.CameraUI`. Alles staat in `Plugins/ATCSim/README.md`.
- Opnieuw zetten: `ATC.SetupGMI` (alleen camera's en schermen). Kopie van de map van daarvoor: `Saved/Backup_before_GMI/EHAM.umap`.
- **Gemeten**: binnen het budget van 1,5 ms GPU per camera (0,9–1,2 ms), want hoogstens twee camera's tekenen 20 beelden per seconde.

## Stand nu
De map staat op de versie van **13 okt 10:51** (mooi, met nog een paar donkere vlakken bij de gates).
De ronde fixes daarna is teruggedraaid met `Airport.UndoLastFixes`:
- de vlakken bij de gates/touchdown zijn weer de donkere versie (geen "subtiele" versie);
- geen extra korrel op het gras;
- de materialen gebruiken weer de engine-map (`D:\UE_5.6\Engine\Plugins\Interchange\Assets\Content\Materials`), de kopieën in `Parents` zijn weg;
- de bomen zijn weer de oude boomkaarten;
- het spline-vliegtuig staat niet meer in de map (level komt uit `Saved/Backup_before_PlaceFlight`).

De nieuwere versie staat nog in `Saved/Backup_before_Undo`. Terugzetten: editor dicht, dan de map `Saved/Backup_before_Undo/Content` over `D:\Import_testing_obi\Content` kopiëren.

## Wat er gefixt is (tot 10:51)
| Probleem | Fix |
|---|---|
| C++ bouwde niet | `D:\UE_5.6\...\GameFramework\Actor.h` regel 80 had getypte rommel |
| Markeringen onzichtbaar | `Airport.RaiseMarkings` (5/10 cm omhoog) |
| Dambord op markeringen | ontbrekende parent-materialen teruggezet / gekopieerd |
| Terrein zwart in het spel | `M_TerrainVT` (leest Virtual Textures) |
| Zwarte vlakken bij gates/touchdown | `M_GroundOverlay` (masked, nog wel donker) |
| Schaduwen pikzwart | PostProcessVolume unbound + Screen Space GI |
| Wazige grond | VT anisotropic aan, `r.MaxAnisotropy=16`, pool 3000 MB |
| Glas op vliegtuigen fout | Disallow Nanite op 66 meshes |
| Simpele KLM/Emirates | 6x vervangen door detailmodellen (map `Aircraft_HQ`), oude verborgen |
| Vliegtuigen beige/groenblauw, zwarte neus | `M_AircraftPaint` voor 903 materialen |

## Handige commando's
| Commando | Doet |
|---|---|
| `Airport.SoftenShadows 3` | SkyLight sterker (getal = sterkte), slaat op |
| `Airport.RaiseMarkings 5` | markeringen optillen (cm) |
| `Airport.PlaceFlight` | spline-vliegtuig (KLM 787) op de Polderbaan zetten, slaat op |
| `Airport.RemoveFlight` | spline-vliegtuigen en hun FlightPath uit de open map halen, slaat op |
| `Airport.RemoveSplines` | je eigen `BP_Spline`'s en de vliegtuigen die erover rijden (`BP_SplineObject`) uit de open map halen, slaat op |
| `ATC.AddParkedGates` | de geparkeerde vliegtuigen van de map in het verkeer (gate per model); daarna `ATC.CheckRoutes` en `ATC.AddParallelRoutes`. Zie `Plugins/ATCSim/README.md` |
| `ATC.SetupGMI` | de GMI-camera's op de schermen van Toren Centrum (koepel op het dak, vier platformcamera's), slaat op |
| `ATC.TowerScreens` | alle schermen van het torenmodel rond het oog, met naam en maat |
| `Airport.FixLetters` | baannummers en geschilderde letters zichtbaar (masked `M_RunwayLetters`); `debug` = rood, `alpha` = alpha als kleur |
| `Airport.Shots 60` | in Play: screenshots naar `Saved/Screenshots/Airport` |
| `Airport.Shots 60 hq` | alleen de nieuwe vliegtuigen |
| `Airport.ListGround` | alle grondmaterialen in de log |
| `Airport.EngineDeps` | welke engine-materialen het project gebruikt |

## Spline-vliegtuig
- De C++ (`Aircraft`, `FlightPath`) zit nog in het project, alleen niet meer in de map.
- Terugzetten: `Airport.PlaceFlight`. Takeoff en landing komen op de Polderbaan; Pushback, Taxi en TaxiToGate moet je zelf over de taxibanen slepen.
- Ander model: klik **Aircraft_KLM_787** aan > Static Mesh. Wijst de neus verkeerd: **Mesh Rotation** Yaw 90 / -90 / 180.

## Nog open
- Donkere vlakken bij gates/touchdown.
- Bomen: de oude boomkaarten blijven blokken van dichtbij. Echte oplossing: 3D-bomen (bv. gratis Megascans via Fab).
- Livery-textures van de KLM/Emirates detailmodellen ontbreken (zoek de originele downloads op de andere pc).
- VR: gemeten op 13 okt (GTX 1080 Ti): 11,3 ms GPU op 1080p, 15 ms op 3440×1440, ook met het VR-profiel van de plugin. Te zwaar voor 90 Hz.
  De map crasht met `-emulatestereo` (oorzaak onbekend, engine-symbolen nodig). Zie `Docs/ATC_VR_Bouwblokken.html`, sectie N.
- Project hangt weer aan de engine-map: op een andere pc missen dan materialen.

## Back-ups
`Saved/Backup_before_SwapPlanes`, `_FixLiveries`, `_GlossyPaint`, `_PlaceFlight`, `_Undo` (versie van 13 okt 12:00), `_ATCSetup`, `_ATCTraffic`,
`_TrafficOnPlay`, `_LiveTraffic` (21 okt, vóór het levende Schiphol), `_ParallelRoutes` (26 okt, vóór de parallelle routes),
`_RemoveSplines` (27 okt, vóór het weghalen van je oude splines), `_ParkedGates` (27 okt, vóór de gates voor de geparkeerde vliegtuigen),
`_RebuildGates` (27 okt, vóór het opnieuw bouwen van alle gates), `_GMI` (28 okt, vóór de camera's op de torenschermen),
`_RegenClean` (27 okt, EHAM-REGEN en de twee spline-Blueprints; daar is nog niets aan veranderd).
