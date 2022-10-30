// Version 0.1
#include <FastLED.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include "EspMQTTClient.h"
#include <PubSubClient.h>


#define NUM_LEDS 4
#define DATA_PIN 22
CRGB leds[NUM_LEDS];


//buton
#define TRIGGER_PIN 23


// MQTT Broker
const char *mqtt_broker = "broker.emqx.io";
const char *topic = "esp32_ch/test";
const char *mqtt_username = "emqx";
const char *mqtt_password = "public";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

enum estado {
  Inactivo = 0,
  BusquedaWifi,
  ConexionMQTT,
  ReceiveData
};

enum estado Estado_Lamp = Inactivo;
int timeout = 120; // seconds to run for
WiFiManager wm;
QueueHandle_t queue;


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("Inicializacion");

  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(0);
  FastLED.show();
  queue = xQueueCreate( 10, 8 * sizeof( char ) );

  if (queue == NULL) {
    Serial.println("Error creating the queue");
  } else {
    Serial.println("Queue OK");
  }

  xTaskCreate(TaskControlLED, "Control LED", 1024, NULL, 2, NULL);
}

void loop() {
  // put your main code here, to run repeatedly:

  //Input
  if (digitalRead(TRIGGER_PIN) == LOW) {
    Estado_Lamp = BusquedaWifi;
  }


  // maquina de estados

  switch (Estado_Lamp) {
    case Inactivo:
      Serial.println("Estado: Inactivo");// Estado con leds
      bool res;
      res = wm.autoConnect("AutoConnectAP", "password"); // password protected ap

      if (!res) {
        Serial.println("Failed to connect");
        // ESP.restart();
      }
      else {
        //if you get here you have connected to the WiFi
        Serial.println("connected...yeey :)");
        Estado_Lamp = ConexionMQTT;
      }

      break;
    case BusquedaWifi:
      Serial.println("Estado: Busqueda wifi");// Estado con leds
      wm.setConfigPortalTimeout(timeout);
      if (!wm.startConfigPortal("AutoConnectAP", "password")) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again, or maybe put it to deep sleep
        ESP.restart();
        delay(5000);
      } else {
        Serial.println("connected...yeey :)");
        Estado_Lamp = ConexionMQTT;
      }



      break;
    case ConexionMQTT:
      Serial.println("Estado: Conexion MQTT"); //estado led ,  conectando a Servidor
      client.setServer(mqtt_broker, mqtt_port);
      client.setCallback(callback);
      while (!client.connected()) {
        String client_id = "esp32-client-";
        client_id += String(WiFi.macAddress());
        Serial.printf("The client %s connects to the public mqtt broker\n", client_id.c_str());
        if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
          Serial.println("Public emqx mqtt broker connected"); //estado led , conectado a Servidor
        } else {
          Serial.print("failed with state ");
          Serial.print(client.state());
          delay(2000);
        }
      }
      client.subscribe(topic);
//      FastLED.setBrightness(0);
//      FastLED.show();
      
      client.publish("brillo", "0");  //Publica
      client.publish("color", "#FFFFFF");  //Publica
      Estado_Lamp = ReceiveData;
      Serial.println("Estado: Recibir datos");


      break;

    case ReceiveData:

      client.loop();

      break;

  }

}


void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  char data_q[length + 1];
  memset ( data_q, '\0', length + 1);
  for (int i = 0; i < length; i++) {
    Serial.print((char) payload[i]);
    data_q[i] = (char)payload[i];
  }
  if ( xQueueSend(queue, &data_q, portMAX_DELAY) != pdPASS ) {
    Serial.println("Error to load data");
  }
  Serial.println();
  Serial.println("-----------------------");
}



void TaskControlLED(void *pvParameters) {
  // init color default
  unsigned int red = 255;
  unsigned int green = 255;
  unsigned int blue = 255;

  char red_str[3];
  char green_str[3];
  char blue_str[3];
  memset(red_str, '\0', 3);
  memset(green_str, '\0', 3);
  memset(blue_str, '\0', 3);

  char data_MQTT[8];
  memset (data_MQTT, '\0', 8);
  for (;;) { // A Task shall never return or exit.
    xQueueReceive(queue, &data_MQTT, portMAX_DELAY);
    
    if ( data_MQTT[0] == '#') {  // color information in payload
      red_str[0] = data_MQTT[1];
      red_str[1] = data_MQTT[2];
      green_str[0] = data_MQTT[3];
      green_str[1] = data_MQTT[4];
      blue_str[0] = data_MQTT[5];
      blue_str[1] = data_MQTT[6];

      red = (unsigned int)strtoul(red_str, NULL, 16);
      green = (unsigned int)strtoul(green_str, NULL, 16);
      blue = (unsigned int)strtoul(blue_str, NULL, 16);

      for (int i = 0; i < 4; i++) {
        leds[i] = CRGB(red, green, blue);
      }
      
      client.publish("color",(char *) data_MQTT);  //Publica

    } else {
      int brightness;
      brightness = atoi(data_MQTT) * 255.0 / 100;
      for (int i = 0; i < 4; i++) {
        leds[i] = CRGB(red, green, blue);
      }

      FastLED.setBrightness((int)brightness);
      client.publish("brillo",(char *) data_MQTT);  //Publica
    }

    FastLED.show();

    memset (data_MQTT, '\0', 8);
  }
}
