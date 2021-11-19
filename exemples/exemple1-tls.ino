#include <ESP8266WiFi.h>
const char* ssid     = "MON SSID";
const char* password = "lacléeWifi!!!!";


#include <Wire.h>
#include <TSL2561.h>
TSL2561 tsl(TSL2561_ADDR_FLOAT); 

#include <Thread.h>
Thread thrPushLux = Thread();

void pushLuxOnConstellation() {  
  uint32_t lum = tsl.getFullLuminosity();
  uint16_t ir, full;
  ir = lum >> 16;
  full = lum & 0xFFFF;
  uint32_t lux = tsl.calculateLux(full, ir);
  
  constellation.pushStateObject("Lux", stringFormat("{ 'Lux':%d, 'Broadband':%d, 'IR':%d }", lux, full, ir), "LightSensor.Lux", 300);
}

void setup(void) {
  Serial.begin(115200);  delay(10);

  if (tsl.begin()) {
    Serial.println("Found sensor");
  } else {
    Serial.println("No sensor?");
    while (1);
  }
  
  // You can change the gain on the fly, to adapt to brighter/dimmer light situations
  //tsl.setGain(TSL2561_GAIN_0X);         // set no gain (for bright situtations)
  tsl.setGain(TSL2561_GAIN_16X);      // set 16x gain (for dim situations)  
  // Changing the integration time gives you a longer time over which to sense light
  // longer timelines are slower, but are good in very low light situtations!
  //tsl.setTiming(TSL2561_INTEGRATIONTIME_13MS);  // shortest integration time (bright light)
  //tsl.setTiming(TSL2561_INTEGRATIONTIME_101MS);  // medium integration time (medium light)
  tsl.setTiming(TSL2561_INTEGRATIONTIME_402MS);  // longest integration time (dim light)

  // Set Wifi mode
  if (WiFi.getMode() != WIFI_STA) {
    WiFi.mode(WIFI_STA);
    delay(10);
  }
  
  // Connect to Wifi  
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
  Serial.println(constellation.getSentinelName());
  
  JsonObject& settings = constellation.getSettings();
  int interval = 10000; //ms
  if(settings.containsKey("Interval")) {
    interval = settings["Interval"].as<int>();
  }

  thrPushLux.onRun(pushLuxOnConstellation);
  thrPushLux.setInterval(interval);

  constellation.addStateObjectType("LightSensor.Lux", TypeDescriptor().setDescription("Lux data informations").addProperty("Broadband", "System.Int32").addProperty("IR", "System.Int32").addProperty("Lux", "System.Int32"));
  constellation.declarePackageDescriptor();
  
  constellation.writeInfo("ESP LightSensor '%s' is started (Push interval: %d sec)", constellation.getSentinelName(), interval);  
}

void loop(void) {
  if(thrPushLux.shouldRun()){
    thrPushLux.run();
  }
}
