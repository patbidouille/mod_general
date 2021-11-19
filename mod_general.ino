/*
 Projet de capteur général a mettre dans une piéce quelconque.
 Ce module contient un capteur de luminosité TLS2561, de température, pression, humidité(?) BMP280, 
 un radar doppler RCWL-0516 et accessoirement un émetteur RF433MHz.
 Ce module renvoit ses informations à une centrale domotique ou d'autre module par MQTT.

  Il cherche une connexion sur un serveur MQTT puis il envoie toutes ses infos en MQTT
  - Il renvoie une détection par le radar.
  - La pression barométrique et les autres parmêtres du BMP280
  La température, l'humidité (si le capteur le peu), la lumiére.
  - Si il reçoit une instruction par MQTT, il répond suivant la demande.
  - Si il a un émetteur RF, il envoi l'ordre reçu par MQTT ou suivant un événement.
  
Instruction en MQTT :
  - Change le temps d'envois des mesures.
  topic = mod_gen1/afftemps -> affiche la valeur de la tempo enregistrée
  topic = mod_gen1/conftemps -> configure la valeur de la tempo enregistrée, message conttent la valeur.

Particularité du mod_gen1 :
C'est le prototype, il a en plus un émetteur RF (pin 1 "TX"), un afficheur Oled (jaune/bleu I2C)
il aura la possibilité d'être branché sur un chargeur.
- idées de fonctions :
* ouvre la lumière si détection et lumière basse.
* fait le relais d'une commande mqtt en rf433

 Exemples :
 MQTT:
 https://github.com/aderusha/IoTWM-ESP8266/blob/master/04_MQTT/MQTTdemo/MQTTdemo.ino
 Witty:
 https://blog.the-jedi.co.uk/2016/01/02/wifi-witty-esp12f-board/
 Module tricapteur:
 http://arduinolearning.com/code/htu21d-bmp180-bh1750fvi-sensor-example.php


*/

#include <ESP8266WiFi.h>    // lib pour le wifi 
//#include <WiFiUdp.h>
#include <ArduinoOTA.h>     // lib pour la prog OTA
#include <PubSubClient.h>
#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier. For a connection via I2C using Wire include
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h> //Si BME280 mettre: #include <Adafruit_BME280.h>
//#include <TSL2561.h>
#include <Adafruit_TSL2561_U.h>
//#include <Thread.h>
// Graphic
//#include <Adafruit_GFX.h>      // include Adafruit graphics library
#include <Adafruit_SSD1306.h>  // include Adafruit SSD1306 OLED display driver
#include <ArduinoJson.h> 
#include <EEPROM.h>

// Include Init variables sensibles
#include "init.h"

//Definition des ports
#define radar 3     //gpio3 (RX)

// Déclarations serveur MQTT
const char* mqtt_server = "192.168.1.81";
const char* mqttUser = "mod";
const char* mqttPassword = "Plaqpsmdp";
const char* svrtopic = "domoticz/in";
const char* topic_Domoticz_OUT = "domoticz/out";

// Création objet
//###
// Pour afficheur 
#define OLED_RESET  -1    // define display reset pin pas de reset
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);  // initialize Adafruit display library
//###

WiFiClient modgen;
PubSubClient client(mqtt_server,1883,callback,modgen);
//BMP280
#define BMP280_I2C_ADDRESS  0x76    // define device I2C address: 0x76 or 0x77 (0x77 is library default address)
Adafruit_BMP280 bmp280;             // initialize Adafruit BMP280 library
// Capteur de lumiére
#define id_mod = 3500;              // idx pour domoticz
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 3500);

// on introduit les threads info : https://github.com/ivanseidel/ArduinoThread
/*Thread thrPushLux = Thread();
Thread thrPushbmp = Thread();
Thread thrPushpression = Thread();
*/

#define NOP __asm__ __volatile__ ("nop\n\t")

// Variables
String clientId = "modgen1"; // Id du client
int idx = 3500;         // idx pour domoticz
long lux = 0;           // Luminosité
float temperature = 0;  // température bmp280
float pression = 0;     // pression atmos bmp280

char msg[100];          // Buffer pour envoyer message mqtt
int value = 0;
unsigned long readTime;
char floatmsg[10];      // Buffer pour les nb avec virgules
char message_buff[100]; // Buffer qui permet de décoder les messages MQTT reçus
char _buffer[9];        // Buffer pour la conversion de la température et pression
long now = 0;           // variable du temps actuel
long lastMsg = 0;       // Horodatage du dernier message publié sur MQTT

bool debug = false;      // Affiche sur la console si True
bool mess = false;      // true si message reçu
String sujet = "";      // contient le topic
String mesg = "";       // contient le message reçu
int interval = 10000;   // en ms, interval entre 2 mesures des capteurs.

long lsmg = 20000;      // Valeur de temps entre 2 lectures, redéfini dans le setup
long lastdetec = 0;     // ancien temps de détection
long lsdtc = 4000;      // temps de nieveau haut du radar

//========================================
void setup() {
  //Serial.begin(115200);
  pinMode(radar, INPUT);     // Initialize la pin du radar
  //I2C stuff (SDA,SCL)
  Wire.pins(2, 0);
  Wire.begin(2, 0);

  setupwifi(debug);         // Init le wifi.
  ArduinoOTA.setHostname("mod_general"); // on donne une petit nom a notre module
  ArduinoOTA.begin(); // initialisation de l'OTA
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  client.subscribe(topic_Domoticz_OUT);

  // initialize the BMP280 sensor
  bmp280.begin(BMP280_I2C_ADDRESS);

  // init TSL2561
  tsl.enableAutoRange(true);            /* Auto-gain ... switches automatically between 1x and 16x */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);      /* fast but low resolution */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);  /* medium resolution and speed   */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_402MS);  /* 16-bit data but slowest conversions */

    
  // initialize the SSD1306 OLED display with I2C address = 0x3D
  display.begin(SSD1306_SWITCHCAPVCC, 0x3c);
  // clear the display buffer.
  display.clearDisplay();
  display.setTextSize(1);   // text size = 1
  display.setTextColor(WHITE, BLACK);  // set text color to white and black background
  display.display();        // update the display

  if (WiFi.status() != WL_CONNECTED) {
    display.setCursor(11, 21);
    display.print("Non connecté");
    display.display();        // update the display
    delay(3000);
  }
  
  // gestion des appels en pseudo sous traitement des fonctions lecture des capteurs.
  /*thrPushLux.onRun(litlum);
  thrPushbmp.onrun(litbmp280);
  thrPushLux.setInterval(interval);
  */
  // On initialise le compteur pour les mesures
  lastMsg = millis();
  lastdetec = millis();
  
  EEPROM.begin(512);
  // On regarde ce qui est dans l'eeprom, sinon on met la valeur par défaut.
  lsmg=eeGetInt(100);
  if ((lsmg == 0) || (lsmg == -1))  {
    lsmg=20000;
    eeWriteInt(100, lsmg);
  } 
}

//****************************************
void loop() {
  // a chaque iteration, on verifie si une mise a jour nous est envoyee
  // si tel est cas, la lib ArduinoOTA se charge de gerer la suite :)
  ArduinoOTA.handle(); 

  if (!client.connected()) {
    reconnect();
  }
  // doit être appelée régulièrement pour permettre au client de traiter les messages entrants pour envoyer des données de publication et de rafraîchir la connexion
  client.loop();

  if ( mess ) { 
    traiteMQTT(); 
    mess=false;
  }
  
  //scan radar
  if (millis() - lastdetec > lsdtc) {
    evenement();
  }
  aff();
  if (millis() - lastMsg > lsmg) {
      lastMsg = millis();
      traitelum();
      traitebmp();
  }
  
  /*
  if(thrPushLux.shouldRun()){
    thrPushLux.run();
  }
  if(thrPushbmp.shouldRun()){
    thrPushbmp.run();
  }
*/
}

//========================================
void aff(){
  display.clearDisplay();
  display.setTextSize(2);   // text size = 2

  // print lux
  sprintf(_buffer, "%05u", (int)(lux));
  display.setCursor(20, 0);            // move cursor to position (15, 0) pixel
  display.print(_buffer);
  display.setCursor(85, 0);
  display.print("Lux");
  
 
  // print data on the LCD
  // 1: print temperature
  if(temperature < 0)
    sprintf(_buffer, "-%02u.%02u C", (int)abs(temperature), (int)(abs(temperature) * 100) % 100 );
  else
    sprintf(_buffer, " %02u.%02u C", (int)temperature, (int)(temperature * 100) % 100 );
  display.setCursor(11, 21);
  display.print(_buffer);
 
  // print degree symbols ( ° )
  //display.drawCircle(89, 23, 2, WHITE);
 
  // 2: print pressure
  sprintf(_buffer, "%04u.%02u", (int)(pression/100), (int)((uint32_t)pression % 100));
  display.setCursor(3, 50);
  display.print(_buffer);
  display.setCursor(91, 50);
  display.print("hPa");
 
  // update the display
  display.display();
  
}
//========================================
void evenement() {
// test de présence et envoie une alerte par MQTT.
  if (digitalRead(radar) == HIGH) { 
      // Envoi notification de présence
    int idx = 3500;
    String name = "presence_gen1"; 
    String svalue = "Détection";
    Emetmessage(idx, name, svalue);
    client.publish(svrtopic,msg,false);
    client.publish("mod_gen1/detection","ON");
    lastdetec = millis(); //delais du niveau haut 
    // Affichage
    display.clearDisplay();
    display.setTextSize(2);   // text size = 2
    display.setCursor(11, 21);
    display.println(F("ALERTE !"));
  } 
  
  // get temperature
  sensors_event_t event;
  tsl.getEvent(&event);
  lux=event.light;
  // get temperature and pressure from library
  temperature     = bmp280.readTemperature();   // get temperature
  pression = bmp280.readPressure();      // get pressure
}


//========================================
void traitelum() {
    // renvoie le niveau de la lumiere tous les lsmg sec
  sensors_event_t event;
  tsl.getEvent(&event); 
  if (event.light)
  {
    lux=event.light;     
    
      idx = 3503;
      String name = "Luminosite_gen1"; 
      sprintf(_buffer, "%05u", (int)(lux));
      String svalue = _buffer;
      Emetmessage(idx, name, svalue);
      client.publish(svrtopic,msg,false);
      svalue.toCharArray(msg,80);
      client.publish("mod_gen1/luminosite", msg); 
    
  }
  else
  {
    String name = "Luminosité"; 
    String svalue = "Erreur senseur";
    Emetmessage(idx, name, svalue);
    client.publish(svrtopic,msg,false);
    client.publish("mod_gen1/luminosite","ERREUR");
  }

}

//========================================
void traitebmp() {
  // renvoie le niveau de la lumiere tous les lsmg sec
    idx = 3501;
    String name = "Température_gen1"; 
    sprintf(_buffer, "%02u.%02u", (int)abs(temperature), (int)(abs(temperature) * 100) % 100 );
    String svalue = _buffer;
    Emetmessage(idx, name, svalue);
    client.publish(svrtopic,msg,false);
    svalue.toCharArray(msg,80);
    client.publish("mod_gen1/temperature", msg); 
    idx = 3502;
    name = "Pression_gen1"; 
    sprintf(_buffer, "%02u.%02u", (int)(pression), (int)((uint32_t)pression % 100));
    svalue = _buffer;
    Emetmessage(idx, name, svalue);
    client.publish(svrtopic,msg,false);
    svalue.toCharArray(msg,80);
    client.publish("mod_gen1/pression", msg); 
      
}


//========================================
void traiteMQTT() {
    // affiche message reçu en MQTT
  // if domoticz message
      
    if ( sujet == topic_Domoticz_OUT) {
      String recept; 
      const char* cmd;
      const char* command;
      Receptionmessage(debug, recept, cmd, command);
      if ( recept == "1545" ) {      
        if ( strcmp(command, "bas") == 0) {
          if ( strcmp(cmd, "ON") == 0 ) {  // On baisse
            
          }
        }
        if ( strcmp(command, "haut") == 0) {
          if ( strcmp(cmd, "ON") == 0 ) {  // On baisse
            
          }
        }   
      }  // if ( idx == 1 ) {
      recept="";               
    } // if domoticz message
  
  if ( sujet == "mod_gen1/afftemps" ) {
      snprintf (msg, 80, "Valeur de conf du temps", String(lsmg).c_str());
      client.publish("mod_gen1/afftemps", String(lsmg).c_str());  
        if (debug) {
         // Serial.print("conf lsmg = ");
         // Serial.println(lsmg);   
        }
  }
  if ( sujet == "mod_gen1/conftemps" ) {  
      lsmg = mesg.toInt();
      eeWriteInt(50, lsmg);
      if (debug) {
        //Serial.print("temps récupéré ");
        //Serial.println(lsmg);
      }
  }  
 
}

//========================================
void eeWriteInt(int pos, int val) {
    byte* p = (byte*) &val;
    EEPROM.write(pos, *p);
    EEPROM.write(pos + 1, *(p + 1));
    EEPROM.write(pos + 2, *(p + 2));
    EEPROM.write(pos + 3, *(p + 3));
    EEPROM.commit();
}

//========================================
int eeGetInt(int pos) {
  int val;
  byte* p = (byte*) &val;
  *p        = EEPROM.read(pos);
  *(p + 1)  = EEPROM.read(pos + 1);
  *(p + 2)  = EEPROM.read(pos + 2);
  *(p + 3)  = EEPROM.read(pos + 3);
  return val;
}


//========================================


//========================================
