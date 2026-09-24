# SI8.0H-13: phasengenaue Regelung prüfen

## Befund aus der SMA-Dokumentation

- Bei einem dreiphasigen Single-Cluster werden Modbus-Leistungsvorgaben am Master für den **gesamten Cluster** gemacht. Im netzparallelen Betrieb sind es gemeinsame Grenzen in Prozent (Register 44039/44041); bei der netzgekoppelten Inselnetz-Betriebsart ist Register 40149 ein Leistungswert **pro Phase**, aus dem SMA die dreifache Systemleistung bildet. Es gibt in dieser Beschreibung keine drei unabhängigen Sollwerte für Master, Slave 1 und Slave 2. [SMA Modbus-Bedienungsanleitung](https://files.sma.de/downloads/SI-Modbus-BA-de-12.pdf)
- Das Generator-Management kann laut SMA den Strom jeder Phase einzeln begrenzen. Das Kapitel gilt ausdrücklich für Inselnetzsysteme und ist keine Einstellung für die derzeitige netzparallele Eigenverbrauchsregelung. [SI8.0H-13 Betriebsanleitung, Generator-Management](https://files.sma.de/downloads/SI44M-80H-13-BE-de-15.pdf)
- Das SMA Energy Meter liefert phasenspezifische OBIS-Werte. Ob der Sunny Island diese bei der Verteilung der Entladeleistung **innerhalb eines Clusters** nutzt, belegt die gefundene Dokumentation nicht. Das lässt sich nur am Gerät unter definierten Lasten messen.

## Was der bestehende Bridge-Code tatsächlich sendet

Die bisherige Bridge liest für ihre Phasen-OBIS-Felder nur das untere 16-Bit-Wort vieler 32-Bit-KSEM-Register. Außerdem gibt sie KSEM-Milliampere als Ampere und eine berechnete Spannung mit Faktor 1000 zu hoch an die `libspeedwire`-Kodierung weiter. L1-Wirkleistung ist gegenüber L2/L3 um Faktor 10 zu groß. Die **saldierte, manipulierte Gesamtwirkleistung** für das 11-kW-Peak-Shaving bleibt davon getrennt.

Dieser Branch ergänzt zwei ausdrücklich aktivierbare Versuchsmodi:

- `PEAKSHAVING_PHASE_OBIS=physical`: Die phasenweisen OBIS-Felder werden vollständig und in den richtigen Einheiten aus dem KSEM gebildet. Die Gesamtwirkleistung ist weiterhin absichtlich virtuell, sodass das Paket nicht vollständig saldenkonsistent ist.
- `PEAKSHAVING_PHASE_OBIS=virtual`: Die bereits begrenzte virtuelle Gesamtwirkleistung wird auf die Phasen verteilt. Als Gewichte dienen die positiven bzw. negativen phasenweisen KSEM-Wirkleistungen. Strom, Scheinleistung und Leistungsfaktor werden dazu passend synthetisch berechnet. Bei ausschließlich L1-Bezug und 3 kW virtueller Gesamtanforderung zeigt das Paket z. B. L1 = 3 kW, L2/L3 = 0 kW. Das ist ein **Testsignal**, keine Abbildung der realen Phasenströme.

Ohne Umgebungsvariable sendet die Bridge exakt die bisherigen Phasenfelder. Das 11-kW-Peak-Shaving, die 20-A-Regelung und das SoC-Limit bleiben in allen Modi gleich. Keiner der beiden Modi ist eine dokumentierte SMA-Schnittstelle für individuelle Cluster-Sollwerte.
Die Bridge protokolliert in den Versuchsmodi `packet_phase_net_w` als Kontrolle der tatsächlich ausgesendeten phasenweisen Wirkleistungen.

## Messung

1. Nur **eine** Bridge-Instanz mit der konfigurierten SMA-Energy-Meter-Seriennummer betreiben. Vor dem Start eines anderen Binaries den laufenden Dienst stoppen. Die Software nicht als Schutz des 35-A-SLS verwenden.
2. Bei stabiler, möglichst einphasiger Testlast zunächst 120 Sekunden im bisherigen Modus messen. Die vorhandene Bridge-Instanz kann laufen, während der Logger read-only misst:

   ```bash
   python3 scripts/phase_probe.py --ksem 10.0.0.70 \
     --si <master-ip> --si <slave1-ip> --si <slave2-ip> \
     --samples 120 > baseline.csv
   ```

3. Experimentelle Bridge auf dem Raspberry Pi bauen. Mit identischen Startargumenten wie im Dienst und `PEAKSHAVING_PHASE_OBIS=physical` starten. Die Startmeldung muss `phase OBIS mode=physical (experimental)` zeigen. Unter vergleichbarer Last nochmals messen:

   ```bash
   python3 scripts/phase_probe.py --ksem 10.0.0.70 \
     --si <master-ip> --si <slave1-ip> --si <slave2-ip> \
     --samples 120 > physical.csv
   ```

4. Falls der `physical`-Versuch keine phasenspezifische Reaktion zeigt, kann `PEAKSHAVING_PHASE_OBIS=virtual` mit kleiner Last und derselben Gesamtanforderung separat getestet werden. Wieder 120 Sekunden aufzeichnen. Der Modus sendet absichtlich synthetische Messwerte; bei auffälliger Regelung oder Gerätefehlern sofort zum bisherigen Modus zurückkehren.
5. `l1_signed_a` bis `l3_signed_a` und die drei `si*_power_w` vergleichen. Die Vorzeichen der Sunny-Island-Leistung einmal anhand der Anzeige am Gerät prüfen. Bei einer echten phasenspezifischen Reaktion müsste bei gleicher Gesamtanforderung die Leistung des SI auf der belasteten Phase relativ zu den anderen steigen und die **gemessene** Stromdifferenz am KSEM kleiner werden. Ändern sich nur Anzeigen, nicht die tatsächlichen KSEM-Ströme, ist kein physischer Phasenausgleich nachgewiesen.
6. Anschließend die Umgebungsvariable entfernen und den ursprünglichen Dienst wieder starten. Wenn die Slave-IP-Adressen kein Modbus anbieten, zunächst nur die KSEM-Spalten ohne `--si` aufzeichnen und die Einzelgerät-Leistungen über die SMA-Geräteansichten exportieren.

Die Messung ist auf kleine, stabile Testlasten begrenzt. Weder eine hohe Last noch eine absichtlich provozierte SLS-Auslösung ist für diesen Test erforderlich.
