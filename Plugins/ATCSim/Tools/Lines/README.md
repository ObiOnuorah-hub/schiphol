# Taxi-routes op de gele lijn

Rekent uit hoe elke taxi-route van EHAM precies over de gele lijnen loopt. De kaart zelf verandert pas met `ATC.ApplyLines`.

Alles komt in `Saved/ATC/Lines`. Nodig: Node.js. De editor moet dicht zijn bij stap 1 en 3.

1. Kaart uitlezen (verf, asfalt, routes, gates):

       UnrealEditor-Cmd.exe Import_testing_obi.uproject -EnablePlugins=PythonScriptPlugin -nullrhi -unattended -nosplash -ExecutePythonScript="<dit pad>/export.py"

2. Uitrekenen. Schrijft `Data/Lines/EHAM.txt` en zegt welke pushback elke gate krijgt:

       node lines.js

3. In de kaart zetten (maakt eerst een back-up in `Saved/Backup_before_Lines`):

       ATC.ApplyLines
       ATC.CheckRoutes
       ATC.CheckStands 6

4. Controleren: opnieuw stap 1 en dan

       node audit.js       hoe ver de routes van de gele lijn liggen
       node clusters.js    de plekken waar ze er naast liggen
       node sheet.js uit.png 3 640 x,y,half ...    plaatjes van die plekken (meter)

Na `ATC.SetupEHAM`, `ATC.AddParkedGates`, `ATC.AddParallelRoutes` of `ATC.AddLateExits` moet dit opnieuw.

## Hoe het werkt

- `chains.js`: het midden van elke geverfde strook (0,3 m) als lijn.
- `graph.js`: welke lijnen middenlijnen zijn (al gebruikt of langs een OSM-taxibaan) en welke randlijnen zijn (gras aan één kant).
- `net.js`: verbindingen. Lijnen die elkaar raken, rechtdoor over een gat, en een bocht (straal minstens 20 m) als een lijn stopt. Alleen over asfalt en nooit over een randlijn.
- `solve.js`: de beste weg over die lijnen, dicht bij de oude route.
- `lines.js`: gates op hun stoplijn, pushbacks, oplijnen op de baan, gladmaken en wegschrijven. Met de hand gezet: P1, P5, P9 en D3 (`HAND`).
