# Zisterne Wasserstand

Ein selbstgebauter Wasserstandssensor, der den Füllstand einer Zisterne (liegender Zylinder) misst und die Ergebnisse per lokalem Webserver bereitstellt.
Diese können dann einfach in Home Assistant integriert werden.

## Beschreibung

Dieses Projekt nutzt einen ESP8266 zusammen mit einem umfunktionierten Blutdruck-Messgerät, um den Wasserstand in einer Zisterne zu erfassen. Die Zisterne ist ein liegender Zylinder, deshalb werden die Messwerte nicht nur in Höhe, sondern auch in Volumen umgerechnet.

Der Sensor stellt die Messwerte lokal über einen Webserver zur Verfügung. Home Assistant kann diese Daten per multiscrape auslesen und als Sensorwerte anzeigen.

## Komponenten

- ESP8266 (https://www.az-delivery.de/en/products/nodemcu)
- ESP-12E ESP8266 IoT WiFi Motorshield L293D (https://www.roboter-bausatz.de/p/esp-12e-esp8266-iot-wifi-motorshield-l293d-motortreiber-h-bruecke)
- SparkFun Qwiic MicroPressure Sensor (SEN-16476)
- Blutdruck-Messgerät mit Manschette
- nötige Elektronik zum Anschluss des Sensors an den Arduino
- Versorgungsspannung für Arduino und Sensoren (5V USB)
- Luftschlauch bis auf den Boden der Zysterne


## Funktionsweise

- Ein Schlauch geht in die Zisterne, bis auf den Boden
- Der Schlauch ist mit dem Drucksensor verbunden
- Der Kompressor des Blutdruck-Messgeräts erzeugt einen Druck im System
- Der Drucksensor misst den benötigen Druck, bis die Luft aus dem Schlauchende gepresst werden kann

Ablauf:

1. Wenn der Wasserstand steigt, steigt der Druck am Schlauchende
2. Dieser Druck drückt gegen die Luft im Schlauch
3. Wenn der Kompressor Luft in den Schlauch Pumpt entsteht ein Gegendruck
4. Der Sensor misst genau diesen Gegendruck
5. Der Gegendruck wird in eine Wassersäule umgerechnet
6. Die Wassersäule wird in ein Volumen in der Zisterne umgerechnet
7. Werte werden über Webserver im Netzwerk freigegeben 

## Berechnung für liegende Zisterne

Bei einem liegenden Zylinder hängt das Füllvolumen nicht linear von der Füllhöhe ab. Eine typische Formel für das Volumen eines horizontalen Zylindersegments ist:

```text
V = L * (R^2 * arccos((R - h) / R) - (R - h) * sqrt(2 * R * h - h^2))
```

- `V` = Volumen
- `L` = Länge des Zylinders
- `R` = Radius
- `h` = Füllhöhe

Für Home Assistant kann außerdem ein Prozentwert berechnet werden:

```text
prozent = (aktuelles_volumen / maximal_volumen) * 100
```

## Home Assistant Integration

Ein möglicher Weg ist die Verwendung eines multiscrape Sensors.

```yaml
# Zisternen-Messungen
multiscrape:
  - name: Zisterne
    resource: http://xxx
    scan_interval: 1800
    log_response: True
    timeout: 20
    verify_ssl: False
    sensor:
      - unique_id: zisterne_fuellhoehe
        name: Zisterne Füllhöhe
        select: "#hoehe"
        state_class: measurement
        unit_of_measurement: "cm"
        icon: mdi:waves-arrow-up
      - unique_id: zisterne_volumen
        name: Zisterne Volumen
        select: "#volumen"
        state_class: measurement
        unit_of_measurement: "L"
        icon: mdi:waves
      - unique_id: zisterne_fuellstand
        name: Zisterne Fuellstand
        select: "#fuellstand"
        state_class: measurement
        unit_of_measurement: "%"
        icon: mdi:water-percent
      - unique_id: zisterne_error
        name: Zisterne Error
        select: "#error"
        icon: mdi:alert-circle
```


## Installation

1. Arduino-Sketch erstellen und auf das Board laden.
2. Blutdrucksensor an den analogen Eingang anschließen.
3. Lokales Netzwerk konfigurieren (Wi-Fi oder Ethernet).
4. Webserver-Code auf dem Arduino aktivieren.
5. Home Assistant so konfigurieren, dass er die Webserver-URL abfragt.

## Tipps zur Kalibrierung

- Messe den tatsächlichen Wasserstand manuell bei bekannten Füllständen.
- Vergleiche die Sensorwerte mit den realen Höhen.
- Erstelle ggf. eine Kalibriertabelle oder einen Korrekturfaktor für bessere Genauigkeit.
