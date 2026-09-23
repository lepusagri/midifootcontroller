# Gestaltung der Weboberfläche

Der folgende Vorschlag wurde in `src/web.html` und `src/web.js` umgesetzt.
Gestaltungsreferenz und Prüfstand: [design/IMPLEMENTATION.md](design/IMPLEMENTATION.md).
Die Browserprüfung mit simulierten Gerätedaten deckt zentrale Bedienabläufe ab;
die abschließende Prüfung nach der letzten Typografieanpassung ist noch offen.

## Bedienung zuerst

Die ursprüngliche Seite war eine lange, schmale Spalte. Scan- und Speicherfunktionen
stehen vor den täglich benötigten Szenen und Effekten. Auf dem Handy liegen die
Effektbuttons ungefähr zwei Bildschirmhöhen unter dem Einstieg.

Empfohlene Struktur:

1. Kopfbereich mit „Lepus“, Verbindungszustand und aktueller Betriebsart.
2. Deutlich sichtbare aktuelle Presetnummer, Presetname und aktive Szene.
3. Spielansicht: Szenen und die sechs Effekte direkt darunter; Preset −/+ in Reichweite.
4. Presets: sechs geordnete Favoritenplätze, Suche nach Nummer oder Name, Liste mit
   markiertem aktiven Preset und kompakter Direkteingabe.
5. Verwaltung: Favoriten-Presetmodus aktivieren, Namen speichern, Smart-/Deep-Scan,
   Fortschrittsbalken und Stoppen.

Favoriten werden über den Stern in der Presetliste in den ersten freien Platz gelegt.
Die sechs Plätze entsprechen direkt den sechs Fußtastern und können verschoben oder
einzeln geleert werden. Ist der Favoritenmodus deaktiviert oder das Set vollständig
leer, bleibt die bisherige relative Presetbelegung erhalten.

Auf breiten Bildschirmen können Presetliste und Spielansicht nebeneinander stehen.
Auf dem Handy reichen drei Ansichten „Spielen“, „Presets“, „Verwaltung“; das aktive
Preset bleibt oben sichtbar.

## Gestaltung

- Dunkler, neutraler Hintergrund für den Einsatz auf der Bühne, klare helle Schrift.
- Türkis nur für aktive Zustände und die wichtigste Aktion; dezente Flächen statt
  der bisherigen starken Leuchteffekte.
- Einheitliche Abstände und Radien; größere Presetnamen, kleinere Metadaten.
- Effektbuttons in der Anordnung der Hardware, mit ausgeschriebenem Namen oder
  verständlichem Untertitel statt ausschließlich CMP/DRV/MOD.
- Mindestens 44 Pixel hohe Touch-Ziele, erkennbare Tastaturfokussierung, Zoom erlaubt.
- Aktiv, inaktiv und nicht vorhanden zusätzlich durch Text/Symbol unterscheiden.
- Scanfortschritt und Fehler als lesbare Statusanzeige, nicht als wechselnde
  Beschriftung der einzigen Aktion.

## Technischer Rahmen

HTML, CSS und JavaScript bleiben lokal auf dem ESP32 verfügbar. Keine externen
Schriftarten, CDNs oder große UI-Frameworks nötig. Die getrennten Quellen
`src/web.html` und `src/web.js` ermöglichen die Gestaltung ohne Eingriffe in MIDI
oder Hardwarelogik. Die vorhandenen Status- und Cache-Endpunkte bleiben nutzbar.
