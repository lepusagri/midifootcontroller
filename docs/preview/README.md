# Webvorschau und Screenshots

`index.html` wird aus der aktuellen `src/web.html` und `src/web.js` erzeugt. `mock-api.js` liefert ausschließlich lokale Beispieldaten. Die Vorschau steuert keinen MIDI-Controller.

Nach Änderungen an der Weboberfläche die Vorschau mit `python scripts/generate_web_preview.py` vom Projektverzeichnis aus neu erzeugen. Die PNG-Dateien zeigen die Ansichten Spielen, Presets, Custom MIDI und Verwaltung sowie eine schmale Spielansicht. Sie lassen sich mit `scripts/generate_web_screenshots.ps1` aktualisieren. Für die Screenshots wird ein lokal installiertes Chrome oder Edge benötigt.
