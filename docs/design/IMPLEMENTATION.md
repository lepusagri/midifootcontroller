# Lepus Weboberfläche: Gestaltung und Umsetzung

Visuelle Vorlage: `lepus-concept.png`, erzeugt mit dem integrierten ImageGen-Werkzeug.
Grundlage ist der vom Nutzer zur Umsetzung freigegebene Vorschlag in `WEB_DESIGN.md`.
Die Vorlage ist eine Entwicklungsreferenz, kein Bestandteil der ausgelieferten Webseite.

## System vor der Umsetzung

- Neutraler dunkler Hintergrund #101313, Flächen #1a1e1e, Linien #303737.
- Haupttext #f1f4f3, sekundärer Text #a6b2ae, Akzent #63dbc2.
- Segoe UI/Systemschrift, Nummern in Consolas/Monospace. Titel 36px, Abschnitt 23px,
  normale Bedienbeschriftung 16px, sekundäre Texte 14px; kleinere mobile Varianten.
- 8px Radius, Abstände 8/12/16/24/32px, maximal 1440px breite Arbeitsfläche.
- Flache, leicht türkis getönte aktive Schalter; keine Schatten oder Leuchteffekte.
- Wortmarke als Text. Nur Suche und Richtungspfeile als kleine SVG-Liniensymbole.
- Kopfzeile, aktuelles Preset als durchgehendes Band, offene zweispaltige Arbeitsfläche,
  acht Szenen in vier Spalten und sechs Effekte in drei Spalten. Rechts Presetliste.
- Auf Mobilgeräten drei getrennte Ansichten und ein haftender Preset-Kopfbereich.

Zulässige sichtbare Grundbeschriftungen: Lepus; Spielen; Presets; Verwaltung;
Verbunden / Verbindung unterbrochen / Warte auf MIDI; Modus mit Gerätestatus;
Szene mit Nummer und echtem Namen; Preset zurück; Preset weiter; Szenen; Effekte;
6 Schalter; Direktwahl; Laden; Aktiv; Aus; Nicht vorhanden; Suchfeld mit
„Nummer oder Name suchen“. Preset-/Szenennamen kommen ausschließlich vom Gerät.

Die Verwaltungsansicht führt die vorhandenen Funktionen im selben System fort:
Preset-Namen, Speichern, unbekannte Presets scannen, vollständiger Scan,
Fortschritt und Scan stoppen. Suche, leere Trefferlisten, Lade-/Fehlerzustände,
Cachezustände und HTTP-Fehler brauchen zusätzliche kurze Funktionsbeschriftungen.

Bewusste Abweichungen von der generierten Grafik: keine gerenderten Texturen oder
Bildartefakte; Zustandsmarkierungen auch an den inaktiven Szenen; echte 501 Einträge
statt einer statischen Liste mit sieben Beispielen; Verwaltung und Mobilansichten
werden entsprechend dem beauftragten Funktionsumfang ergänzt. Keine erfundenen
Presetnamen oder Kennzahlen in der Firmware.

## Umsetzungs- und Prüfstand

Die Oberfläche ist in `src/web.html` und `src/web.js` umgesetzt. Die letzte
Typografieanpassung verwendet unter anderem 46px für den Presettitel, 28px für
Abschnittstitel und Courier New für die große Presetnummer; mobile Größen sind
separat begrenzt.

Abgleich des ersten Desktop-Screenshots mit der generierten Vorlage:

| Bereich | Ergebnis / Anpassung |
| --- | --- |
| Aufbau | Kopfzeile, Presetband und zweispaltige Spielansicht entsprechen der Vorlage. |
| Farben | Dunkle Flächen und türkis markierte aktive Zustände übernommen. |
| Typografie | Zunächst zu klein; Titel, Navigation und Schaltertexte anschließend vergrößert. |
| Nummern | Monospace-Schrift anschließend auf Courier New umgestellt. |
| Abstände | Abstand zum Effektbereich und Höhe der Presetliste anschließend erhöht. |
| Mobil | Erste Aufnahme bei 390 × 844 zeigt Szenen und Effekte ohne horizontales Scrollen. |

Browserprüfung mit simulierten Gerätedaten: Presetwahl, Suche, Eingabevalidierung,
Szenen-/Effektbefehle, Speichern, Scan/Stop und mehrere Fehler-/Wiederverbindungsfälle
wurden erfolgreich durchlaufen. Der Lauf endete an einer mobilen Navigationsprüfung,
die unmittelbar nach dem Klick prüfte. Der Test wartet jetzt auf den Ansichtswechsel;
ein erneuter Browserstart wurde vom Nutzer abgelehnt. Daher sind dieser Schritt,
weitere schmale Ansichten und der visuelle Abgleich der letzten Anpassungen offen.
Kein Test am physischen Controller.
