# PeakShaving lean

Ein Prozess. Kein Python, keine RAM-Disk, keine Zwischen-Datei.

```text
KOSTAL KSEM --Modbus/TCP--> peakshaving --SMA Speedwire Multicast--> SMA
```

Die Peak-Shaving-Logik bleibt wie im bisherigen Stand:

```text
fake grid power = real import - peak target
```

Standardwerte:

- KSEM: `10.0.0.70`
- Modbus TCP: `502`
- Unit ID: `71`
- Peak-Ziel: `11000 W`

## Build

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j
```

## Start

```bash
./build/peakshaving
```

Optional:

```bash
./build/peakshaving <ksem-ip> <peak-watt> <port> <unit-id>
```

Beispiel:

```bash
./build/peakshaving 10.0.0.70 11000 502 71
```

## Entfernt

- Python / pymodbus
- `/media/ramdisk/kostal.txt`
- Datei-Parsing und String-Konvertierung
- Dutzende einzelne Modbus-Requests
- Build-Artefakte und doppelte Projektteile

Der KSEM wird jetzt pro Zyklus in vier zusammenhängenden Modbus-Blöcken gelesen und daraus direkt das SMA-eMeter-Paket erzeugt.

## Kompatibilität

Register-Auswertung und Skalierung bilden absichtlich zunächst das effektive Verhalten des bisherigen funktionierenden Python+C++-Aufbaus nach. Insbesondere bleibt vorerst die alte Auswertung des zweiten 16-Bit-Registers eines angeforderten U32-Paares erhalten. Nach erfolgreichem Test kann die Registerdekodierung separat bereinigt werden, ohne gleichzeitig die Architektur zu ändern.
