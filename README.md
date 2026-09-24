# PeakShaving lean

Ein Prozess. Kein Python, keine RAM-Disk, keine Zwischen-Datei.

```text
KOSTAL KSEM --Modbus/TCP--> peakshaving --SMA Speedwire Multicast--> SMA Sunny Island
                                      ^
                                      |
                         Sunny Island SoC via Modbus/TCP
```

Die Peak-Shaving-Logik bleibt wie im bisherigen Stand:

```text
fake grid power = real import - peak target
```

Zusätzlich werden die drei KSEM-Phasenströme überwacht. Überschreitet ein bezogener Außenleiter 30 A oder liegt er mehr als 20 A über dem schwächsten Außenleiter, fordert die Bridge über den virtuellen SMA-Zähler zusätzliche Entladung an. Bei Schieflast wird die stärkste Phase auf rechnerisch 20 A, bei einer reinen Phasenüberlast auf 30 A entlastet. Das bestehende 11-kW-Peak-Shaving bleibt als Mindestanforderung bestehen.

**Grenze:** Die drei Sunny Islands erhalten einen saldierten Leistungswunsch. Die Bridge verteilt keine unterschiedlichen Sollwerte an die drei Geräte. Eine symmetrische Entladung senkt den Bezug auf der stark belasteten Phase, beseitigt aber die Stromdifferenz zwischen den Außenleitern nicht. Die Regelung ersetzt keinen Leitungsschutz und muss am KSEM/SMA unter realer Last verifiziert werden.

Zusätzlich kann der Entladeanteil des virtuellen SMA-Energy-Meters anhand des Sunny-Island-SoC begrenzt werden. Dadurch reduziert der Sunny Island seine Unterstützung sanft, bevor der Akku seine harte Abschaltgrenze erreicht.

## Standardwerte

- KSEM: `10.0.0.70`
- KSEM Modbus TCP: `502`
- KSEM Unit ID: `71`
- Peak-Ziel: `11000 W`
- Phasengrenze: `30 A` Bezug je Außenleiter
- Schieflastauslöser: `20 A` Differenz zwischen stärkstem und schwächstem Außenleiter
- Sunny Island Modbus TCP: `502`
- Sunny Island Unit ID: `3`
- Sunny Island SoC: Register `30845`, U32, FIX0

## Phasenregelung

Die KSEM-Register 60/100/140 liefern die Ströme in 0,001 A; 62/102/142 liefern die Spannungen in 0,001 V. Bezug und Einspeisung werden anhand der jeweiligen Wirkleistung mit Vorzeichen versehen. Die Steuerwerte werden als vollständige 32-Bit-Register gelesen; ein einzelnes 16-Bit-Wort würde beispielsweise bei mehr als 6,55 kW Phasenleistung überlaufen.

Die Bridge berechnet jede Sekunde die größere Anforderung aus 11-kW-Peak-Shaving und der Phasengrenze. Die zusätzliche Anforderung wird auf 18 kW und auf den saldierten Netzbezug vor der angeforderten Unterstützung begrenzt. Bei einer aktiven Phasenregelung wird eine gleichzeitige Ladeanforderung unterdrückt. Das SoC-Derating begrenzt anschließend auch diese Entladung.

Die vorige gesendete Anforderung wird bei der nächsten Messung berücksichtigt, damit eine wirksame Entladung nicht im nächsten Zyklus wieder abgeschaltet wird. Im Log stehen `phase_A` (negativ = Einspeisung), `phase_diff_A`, `phase_assist`, `extra_request`, `fake_import` und `fake_export`.

## SoC-Derating

Bei aktivierter Sunny-Island-Abfrage gilt:

- ab `20 %` SoC beginnt das Derating
- `18 kW` maximal bei 20 % SoC
- Kennlinie: pro `0,5 %-Punkte` SoC sinkt die maximal angeforderte Entladeleistung rechnerisch um `1 kW`
- bei `11 %` SoC sind `0 kW` Entladeleistung erlaubt
- der Limiter wird beim Fallen ab 20 % aktiv
- Freigabe mit Hysterese erst wieder ab `21 %`
- die SoC-Abfrage erfolgt etwa einmal pro Sekunde
- die normale Peak-Shaving-Anforderung bleibt erhalten; nur die positive Entladeanforderung wird auf das SoC-Limit gekappt

Formel im Derating-Bereich:

```text
max_discharge_W = clamp((SoC - 11) * 2000, 0, 18000)
```

Wichtig: SMA liefert Register 30845 als `U32 FIX0`, also ohne Nachkommastellen. Die Kennlinie unterstützt zwar rechnerisch 0,5-%-Zwischenschritte, mit dem direkten Sunny-Island-Modbuswert entstehen in der Praxis aber 1-%-SoC-Schritte und damit jeweils 2-kW-Leistungsstufen.

Mit den tatsächlich per Register 30845 gelieferten ganzzahligen SoC-Werten ergibt sich daher:

```text
20 % -> 18 kW
19 % -> 16 kW
18 % -> 14 kW
17 % -> 12 kW
16 % -> 10 kW
15 % ->  8 kW
14 % ->  6 kW
13 % ->  4 kW
12 % ->  2 kW
11 % ->  0 kW
```

## Build

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Start

Ohne SoC-Limiter, vollständig kompatibel zum bisherigen Aufruf:

```bash
./build/peakshaving
```

oder:

```bash
./build/peakshaving <ksem-ip> <peak-watt> <ksem-port> <ksem-unit-id>
```

Mit Sunny-Island-SoC-Limiter:

```bash
./build/peakshaving <ksem-ip> <peak-watt> <ksem-port> <ksem-unit-id> <sunny-island-ip> [sunny-island-port] [sunny-island-unit-id]
```

Beispiel:

```bash
./build/peakshaving 10.0.0.70 11000 502 71 10.0.0.80 502 3
```

Die Sunny-Island-IP im Beispiel ist nur ein Platzhalter und muss durch die tatsächliche IP des Masters ersetzt werden.

## Verhalten bei Modbus-Fehlern

Ist noch nie ein gültiger SoC gelesen worden, verändert der SoC-Limiter die bestehende Peak-Shaving-Regelung nicht. Nach einem erfolgreichen SoC-Read wird bei einzelnen Lesefehlern zunächst der letzte gültige SoC weiterverwendet.

## Entfernt

- Python / pymodbus
- `/media/ramdisk/kostal.txt`
- Datei-Parsing und String-Konvertierung
- Dutzende einzelne Modbus-Requests
- Build-Artefakte und doppelte Projektteile

Der KSEM wird pro Zyklus in vier zusammenhängenden Modbus-Blöcken gelesen und daraus direkt das SMA-eMeter-Paket erzeugt.

## Kompatibilität

Register-Auswertung und Skalierung des KSEM bilden absichtlich weiterhin das effektive Verhalten des bisherigen funktionierenden Python+C++-Aufbaus nach. Insbesondere bleibt die alte Auswertung des zweiten 16-Bit-Registers eines angeforderten U32-Paares in den übrigen OBIS-Feldern vorerst erhalten. Die Peak- und Phasenregelung verwendet dagegen die vollständigen 32-Bit-KSEM-Werte. Die alten OBIS-Felder sollten vor einer späteren Umstellung separat gegen den realen SMA-Zähler geprüft werden. Die neue Sunny-Island-SoC-Abfrage dekodiert Register 30845 dagegen regulär als U32.