# Lepus MIDI Foot Controller

## PlatformIO

Das Projekt ist für ein **DOIT ESP32 DevKit V1** eingerichtet.

1. PlatformIO in VS Code öffnen und diesen Ordner als Projekt laden.
2. Das Ziel `esp32doit-devkit-v1` auswählen.
3. Das Board per USB anschließen, anschließend **Build** und **Upload** ausführen.
4. Den seriellen Monitor mit 115200 Baud öffnen.

Die Firmware verwendet GPIO 21/22 für I2C und GPIO 16/17 für MIDI. Vor dem
ersten Einsatz WLAN-Zugangsdaten und Hostnamen in `LepusMidiController.ino`
anpassen. PlatformIO lädt beim ersten Build alle Bibliotheken automatisch,
einschließlich AxeFxControl direkt aus dessen GitHub-Repository.
