/*
  Pneumatische Zisternen-Füllstandsmessung.
  Start der Messung und Übertragung der Ergebnise über WLAN.
  
  Arduino-Bord: "NodeMCU 1.0(ESP-12E Module)"
  Autor Wolfgang Neußer
  Stand: 24.10.2021

  Hardware:
  DOIT ESP12E Motor Shield mit L293D Motortreiber
  Amica NODE MCU ESP8266 12E
  SparkFun Qwiic MicroPressure Sensor
  Druckpumpe und Entlüftungsventil aus Oberarm-Blutdruckmesser
 
  Messablauf:
  1. Abluftventil schließen, Druckpumpe einschalten
  2. Druck kontinuierlich messen
     Wenn Druckanstieg beendet -> Pumpe ausschalten
  3. Beruhigungszeit
  4. Aktueller Druck - atmosphärischen Druck = Messdruck
     Beispiel: 29810 Pa = 3040 mmH2O = 100% Füllstand
  5. Abluftventil öffnen

*/

// Bibliothek für WLAN
#include <ESP8266WiFi.h>

// Bibliothek für die I2C-Schnittstelle
#include <Wire.h>

// Bibliothek für den Sensor (Im Bibliotheksverwalter unter "MicroPressure" suchen
// oder aus dem GitHub-Repository https://github.com/sparkfun/SparkFun_MicroPressure_Arduino_Library )
#include <SparkFun_MicroPressure.h>

// Server an Port 80 initialisieren
WiFiServer server(80);

// Konstruktor initialisieren
// Ohne Parameter werden Default Werte verwendet
SparkFun_MicroPressure mpr; 

// Bibliothek für das Flashen über WLAN
#include <ArduinoOTA.h>


// Zuordnung der Ein- Ausgänge
#define VENTIL          5                 // GPIO5 (PWM MotorA)
#define DA              0                 // GPIO0 (Richtung MotorA)
#define PUMPE           4                 // GPIO4 (PWM MotorB)
#define DB              2                 // GPIO2 (Richtung MotorB)
#define SDA            12                 // GPIO12 I2C
#define SCL            13                 // GPIO13 I2C
#define AUF             LOW               // Ventil öffnen
#define AUS             LOW               // Pumpe ausschalten
#define ZU              HIGH              // Ventil schliessen
#define EIN             HIGH              // Pumpe einschalten

// Heimnetz Parameter (an eigenes Netz anpassen)
const char* ssid = "";
const char* pass = "";

// An eigene Zisterne anpassen (zur Berechnung der Füllmenge)
//const int A = 78;                       // Grundfläche der Zisterne in cm^2 (d * d * 3,14 / 4)
const double R = 100;                       // Radius der Zisterne in cm
const double L = 671;                       // Länge der Zisterne in cm
const double maxFuellhoehe = 2000;           // Füllhöhe der Zisterne in mm
const int maxFuellmennge = 21080;           // Füllmenge der Zisterne in L

int atmDruck, messDruck, vergleichswert;
double gefilterterDruck = 100097.0;
const double druckAlpha = 0.12;
double letzteHoehe = 0.0;
int messSchritt, wassersaeule;
String hoehe = " - - ";
String volumen = "- - ";
String fuellstand = " - - "; 
String error = "Start - No Error"; 
unsigned long messung, messTakt;

double berechneVolumen(double h, double R, double L) {
    if (h <= 0.0 || R <= 0.0 || L <= 0.0) return 0.0;

    if (h >= 2.0 * R) {
        return (sq(R) * L) / 1000.0;
    }

    double normalized = constrain((R - h) / R, -1.0, 1.0);
    double a = sq(R) * L;
    double b = acos(normalized);
    double c = R - h;
    double d = sqrt(max(0.0, 2.0 * R * h - sq(h)));
    double e = sq(R);
    double v = (a * (b - c * d / e)) / 1000.0;

    if (isnan(v) || isinf(v)) {
      Serial.println(F("Fehler: Ungültige Volumenberechnung"));
      v = 0.0;
    }

    return v;
}

void sendeWebResponse(WiFiClient& client) {
    client.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nRefresh: 15\r\n\r\n<!DOCTYPE HTML><html>"));
    client.print(F("<head><title>Fuellstandsmesser</title>"));
    client.print(F("<meta charset=\"utf-8\" http-equiv='refresh' content='60'>"));
    client.print(F("<meta name='viewport' content='width=device-width, initial-scale=1.0' /></head>"));
    client.print(F("<h1>Füllstand Zisterne</h1><br>"));
    client.print(F("<table>"));
    client.print(F("<tr><td><b>Füllhöhe:</b> </td><td id=\"hoehe\">")); client.print(hoehe); client.print(F("</td><td>cm</td></tr>"));
    client.print(F("<tr><td><b>Volumen:</b> </td><td id=\"volumen\">")); client.print(volumen); client.print(F("</td><td>L</td></tr>"));
    client.print(F("<tr><td><b>Füllstand:</b> </td><td id=\"fuellstand\">")); client.print(fuellstand); client.print(F("</td><td>%</td></tr>"));
    client.print(F("<tr><td><b>Errorcode:</b> </td><td id=\"error\">")); client.print(error); client.print(F("</td></tr>"));
    client.print(F("</table></html>"));
    error = "No Error";
    if (messSchritt == 0) messSchritt = 1;
}

// **************************************************************************************
// State-Machine Füllstandsmessung
//
void messablauf() {
   switch (messSchritt) {
      case 0:  // Regelmäßig aktuellen atmosphärischen Druck erfassen
         if (!digitalRead(VENTIL) && !digitalRead(PUMPE)) {
            atmDruck = messDruck;      
         }
         break;

      case 1:  // Messung gestartet
         vergleichswert = messDruck;
         digitalWrite(VENTIL, ZU);
         digitalWrite(PUMPE, EIN);
         messung = millis() + 2000;
         messSchritt = 2;
         break;
      
      case 2:  // warten solange Druck steigt
         if (messDruck > vergleichswert + 10) {
            vergleichswert = messDruck;
            messung = millis() + 1000;
         }
         if (wassersaeule > (maxFuellhoehe + 200)) {
            Serial.println("Fehler: Messleitung verstopft!");
            error = "Fehler: Messleitung verstopft!";
            messSchritt = 4;
         }
         else if (messung < millis()) {
            digitalWrite(PUMPE, AUS);
            messung = millis() + 100;
            messSchritt = 3;
         }
         break;

      case 3:  // Beruhigungszeit abgelaufen, Messwert ermitteln
         if (messung < millis()) {

            double h = wassersaeule / 10.0; // wasserhöhe in cm     

            // Bei zu großen Abweichungen liegt ein Messfehler vor, daher letzte Messung verwenden
            if (h < letzteHoehe * 0.1) {
              h = letzteHoehe;  
            }

            double v = berechneVolumen(h, R, L);
            letzteHoehe = h;
            // Für Webausgabe Variablen füllen
               

            volumen = String(v);
            hoehe = String(h);    
            // Umrechnung Wassersäule in 0 - 100%
            fuellstand = String(map(v, 0, maxFuellmennge, 0, 100));
            
            if( h > R*2 ) {
              volumen = String(maxFuellmennge);
              fuellstand = String(100);
            }            
            Serial.println("Füllhöhe: "+ String(h) + "cm");
            Serial.println("Volumen: " + volumen);
            Serial.println("Füllstand: " + fuellstand);
            Serial.println();
            messSchritt = 4;
         }
         break;

      case 4:  // Ablauf beenden
         digitalWrite(VENTIL, AUF);
         digitalWrite(PUMPE, AUS); 
         messSchritt = 0;
         break;
         
      default:
         messSchritt = 0;
         break;
   }
}

void setup() {
   // Motortreiber-Signale

   // Richtung Motor A
   pinMode(DA, OUTPUT);
   digitalWrite(DA, HIGH);
   // PWM Motor A
   pinMode(VENTIL, OUTPUT);
   digitalWrite(VENTIL, AUF);
   
   // Richtung Motor B
   pinMode(DB, OUTPUT);
   digitalWrite(DB, HIGH);
   // PWM Motor B
   pinMode(PUMPE, OUTPUT);
   digitalWrite(PUMPE, AUS); 
   
   Serial.begin(115200);                     
   delay(10);
   Serial.println();
   
   // I2C initialisieren mit 400 kHz
  //  Wire.begin(SDA, SCL, 400000);        
   Wire.begin(SDA, SCL, 400000);            
    

   // Drucksensor initialisieren
   // Die Default-Adresse des Sensors ist 0x18
   // Für andere Adresse oder I2C-Bus: mpr.begin(ADRESS, Wire1)
   if(!mpr.begin()) {
      Serial.println("Keine Verbindung zum Drucksensor.");
      error = "Fehler: Keine Verbindung zum Drucksensor.";
      delay(5000);
      ESP.restart();
   }

   // WiFi initialisieren
   WiFi.mode(WIFI_STA);
   Serial.println("Verbindung zu " + String(ssid) + " wird hergestellt");
   WiFi.begin(ssid, pass);
   while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
   }
   
   server.begin();
   Serial.println("Server ist gestartet");
   Serial.print("IP-Adresse: ");
   Serial.println(WiFi.localIP());
   
   ArduinoOTA.onStart([]() {        // Pumpe und Ventil ausschalten beim Flashen
      digitalWrite(VENTIL, AUF);
      digitalWrite(PUMPE, AUS);
      messSchritt = 0;
   });
   // Passwort zum Flashen
   ArduinoOTA.setPassword((const char *)"esp8266");
   // OTA initialisieren
   ArduinoOTA.begin();              

   messTakt = 0;
   messSchritt = 0;
   atmDruck = 100097;                    // Ausgangswert Atmosphärendruck in Pa 
}

void loop() {
   static String inputString;
   // OTA-Service bedienen                                                                                                                              
   ArduinoOTA.handle();                   
   yield();

   // Kommandos über serielle Schnittstelle
   if (Serial.available()) {
      char inChar = (char)Serial.read();
      if ((inChar == '\r') || (inChar == '\n')) {
         if (inputString == "?") {
            Serial.println("Kommandos: ");
            Serial.println("p1 = Pumpe EIN");
            Serial.println("p0 = Pumpe AUS");
            Serial.println("v1 = Ventil ZU");
            Serial.println("v0 = Ventil AUF");
            Serial.println("start = Messung starten");
            Serial.println();
         }
         else if (inputString == "p1") {
            Serial.println("Pumpe EIN");
            digitalWrite(PUMPE, EIN);
         }
         else if (inputString == "p0") {
            Serial.println("Pumpe AUS");
            digitalWrite(PUMPE, AUS);
         }
         else if (inputString == "v1") {
            Serial.println("Ventil ZU");
            digitalWrite(VENTIL, ZU);
         }
         else if (inputString == "v0") {
            Serial.println("Ventil AUF");
            digitalWrite(VENTIL, AUF);
         }
         else if (inputString == "start") {
            if (messSchritt == 0) {
               Serial.println("Messung gestartet");
               messSchritt = 1;
            }
         }
         inputString = "";
      } else inputString += inChar;
   } 

   // Alle 10 ms Sensorwert auslesen                                          
   unsigned long currentMillis = millis();
   if (currentMillis - messTakt >= 10) {
      double sensorDruck = mpr.readPressure(PA);
      gefilterterDruck = druckAlpha * sensorDruck + (1.0 - druckAlpha) * gefilterterDruck;
      messDruck = int(gefilterterDruck + 0.5);
      // Umrechnung Pa in mmH2O   
      wassersaeule = (messDruck - atmDruck) * 10197 / 100000;
      if (wassersaeule < 0) wassersaeule = 0;
      messTakt = currentMillis;
   }

   static unsigned long lastWifiRetry = 0;
   if (WiFi.status() != WL_CONNECTED && currentMillis - lastWifiRetry >= 5000) {
      lastWifiRetry = currentMillis;
      WiFi.reconnect();
   }

   // Sicherheitsabschaltung der Pumpe bei Überdruck
   if ((messSchritt == 0) && (wassersaeule > (maxFuellhoehe + 300))) {
      digitalWrite(PUMPE, AUS);
      Serial.println("Überdruck. Messleitung verstopft!");
      error = "Fehler: Überdruck. Messleitung verstopft!";
   }
                                          
   // State-Machine 
   messablauf();

   // Start der Messung und Übergabe des letzten Ergebnisses bei jeder Client-Anfrage
   WiFiClient client = server.available();
   if (client) {

      while (client.connected()){
        if (client.available()){
          String line = client.readStringUntil('\r');
          Serial.print(line);
          if (line.length() == 1 && line[0] == '\n'){
            sendeWebResponse(client);
            break;
          }
        }
      }
      while (client.available()) {
        client.read();
      }

      client.stop();
   }

}
