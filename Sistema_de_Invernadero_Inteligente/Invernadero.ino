#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <ESP32Servo.h>
#include <WiFi.h>
#include <HTTPClient.h>

// Configuración WiFi
const char* ssid = "INFINITUM2A4D_2.4";
const char* password = "uTDmnG5K9S";

// URL de tu Web App
const String WEBAPP_URL = "https://script.google.com/macros/s/AKfycbyKfeY2f4XnlJyb7KbGmVBEIvXoOeu2ejmI_KtKhZ-dfsTMi6nLAqGd7S3pmMnk2c4F9A/exec";

// Configuración de la pantalla LCD (dirección I2C, columnas, filas)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Configuración del sensor DHT11
#define DHTPIN 4
#define DHTTYPE DHT11

// Pines para sensores analógicos de ESP32
const int PIN_HUMEDAD_SUELO = 34;
const int PIN_NIVEL_AGUA = 35;

// Pines para relés
const int PIN_RELE_BOMBA = 18;
const int PIN_RELE_FOCO = 19;
const int PIN_RELE_VENTILADOR = 5;

// Pin para servomotor
const int PIN_SERVO = 16;

// Umbrales
const int UMBRAL_SUELO = 250;
const int UMBRAL_AGUA = 100;
const float UMBRAL_TEMP = 28.0;

// Variables para registrar estados
String estado_bomba = "APAGADA";
String estado_ventilador = "APAGADO";
int posicion_servo = 0;

// Variables para mensajes especiales
bool agua_agotada = false;
bool suelo_humedo = false;
String ultimoMensaje = ""; // Variable de control para evitar parpadeos

// Contador para enviar datos a Google Sheets cada cierto tiempo
unsigned long tiempoUltimoEnvio = 0;
const unsigned long intervaloEnvio = 60000; // Enviar datos cada 1 minuto (60000ms)

// Crear objeto DHT
DHT dht(DHTPIN, DHTTYPE);

// Crear objeto Servo
Servo servoMotor;

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);  // Pines SDA y SCL del ESP32
  lcd.init();          // Inicializa pantalla LCD
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Sistema de Riego");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...");

  dht.begin();
  Serial.println("Sensor DHT11 iniciado...");

  ESP32PWM::allocateTimer(0);
  servoMotor.setPeriodHertz(50);
  servoMotor.attach(PIN_SERVO, 500, 2400);
  servoMotor.write(0);

  pinMode(PIN_RELE_BOMBA, OUTPUT);
  pinMode(PIN_RELE_FOCO, OUTPUT);
  pinMode(PIN_RELE_VENTILADOR, OUTPUT);

  digitalWrite(PIN_RELE_BOMBA, HIGH);
  digitalWrite(PIN_RELE_FOCO, HIGH);
  digitalWrite(PIN_RELE_VENTILADOR, HIGH);

  digitalWrite(PIN_RELE_FOCO, LOW);  // Encender foco

  // Conectar a WiFi
  conectarWiFi();
  
  delay(2000);
}

void loop() {
  // Simular valores aleatorios
  int valor_suelo = random(0, 4096);  // Simulación de humedad del suelo
  int valor_nivel = analogRead(PIN_NIVEL_AGUA); // Real

  float temperatura = random(200, 400) / 10.0;  // Simulación de temperatura

  if (isnan(temperatura)) {
    Serial.println("Fallo al leer temperatura del sensor DHT11!");
    temperatura = 0;
  }

  // Verificar si el suelo está suficientemente húmedo
  suelo_humedo = (valor_suelo >= UMBRAL_SUELO);

  // Control de la bomba
  if (valor_suelo < UMBRAL_SUELO && valor_nivel > UMBRAL_AGUA) {
    estado_bomba = "ENCENDIDA";
    digitalWrite(PIN_RELE_BOMBA, LOW);
    agua_agotada = false;
  } else {
    estado_bomba = "APAGADA";
    digitalWrite(PIN_RELE_BOMBA, HIGH);

    if (valor_nivel <= UMBRAL_AGUA) {
      agua_agotada = true;
    }
  }

  // Control del ventilador y servo
  if (temperatura > UMBRAL_TEMP) {
    estado_ventilador = "ENCENDIDO";
    digitalWrite(PIN_RELE_VENTILADOR, LOW);
    if (posicion_servo < 180) {
      posicion_servo += 10;
      if (posicion_servo > 180) posicion_servo = 180;
    } else {
      posicion_servo = 0;
    }
    servoMotor.write(posicion_servo);
  } else {
    estado_ventilador = "APAGADO";
    digitalWrite(PIN_RELE_VENTILADOR, HIGH);
    servoMotor.write(0);
    posicion_servo = 0;
  }

  // Mostrar por Serial
  Serial.print("Humedad del suelo: ");
  Serial.println(valor_suelo);
  Serial.print("Nivel de agua: ");
  Serial.println(valor_nivel);
  Serial.print("Temperatura: ");
  Serial.print(temperatura);
  Serial.println(" °C");
  Serial.print("Estado de la bomba: ");
  Serial.println(estado_bomba);
  Serial.print("Estado del ventilador: ");
  Serial.println(estado_ventilador);
  Serial.print("Posición servo: ");
  Serial.println(posicion_servo);
  Serial.println();

  // === Mostrar en LCD solo si el mensaje cambia ===
  String mensajeActual = "";

  if (agua_agotada) {
    mensajeActual = "TENGO SED";
    if (ultimoMensaje != mensajeActual) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ALERTA:");
      lcd.setCursor(0, 1);
      lcd.print(mensajeActual);
      ultimoMensaje = mensajeActual;
    }
  } 
  else if (suelo_humedo) {
    mensajeActual = "PLANTAS FELICES :)";
    if (ultimoMensaje != mensajeActual) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(":)");
      lcd.setCursor(0, 1);
      lcd.print(mensajeActual);
      ultimoMensaje = mensajeActual;
    }
  } 
  else {
    mensajeActual = "Esperando...";
    if (ultimoMensaje != mensajeActual) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Sistema OK");
      lcd.setCursor(0, 1);
      lcd.print(mensajeActual);
      ultimoMensaje = mensajeActual;
    }
  }

  // Enviar datos a Google Sheets cada intervalo definido
  unsigned long tiempoActual = millis();
  if (tiempoActual - tiempoUltimoEnvio >= intervaloEnvio) {
    enviarDatosAGoogleSheets(valor_suelo, valor_nivel, temperatura, estado_bomba, estado_ventilador);
    tiempoUltimoEnvio = tiempoActual;
  }

  delay(3000);
}

void conectarWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conectando WiFi");
  
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(intentos % 16, 1);
    lcd.print(".");
    intentos++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado a WiFi!");
    Serial.print("Dirección IP: ");
    Serial.println(WiFi.localIP());
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Conectado!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    Serial.println("\nError al conectar WiFi");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Error WiFi!");
    delay(2000);
  }
}

void enviarDatosAGoogleSheets(int humedadSuelo, int nivelAgua, float temperatura, String estadoBomba, String estadoVentilador) {
  // Verificar si estamos conectados a WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Error: No hay conexión WiFi");
    return;
  }
  
  HTTPClient http;
  
  // Preparar la URL con los parámetros
  String url = WEBAPP_URL;
  url += "?humedadSuelo=" + String(humedadSuelo);
  url += "&nivelAgua=" + String(nivelAgua);
  url += "&temperatura=" + String(temperatura);
  url += "&estadoBomba=" + estadoBomba;
  url += "&estadoVentilador=" + estadoVentilador;
  
  // Iniciar conexión HTTP
  http.begin(url);
  
  // Enviar solicitud GET
  int httpCode = http.GET();
  
  // Verificar respuesta
  if (httpCode > 0) {
    String respuesta = http.getString();
    Serial.println("Respuesta del servidor: " + respuesta);
    
    // Mostrar confirmación en LCD brevemente
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Datos enviados");
    lcd.setCursor(0, 1);
    lcd.print("Codigo: " + String(httpCode));
    delay(1000);
  } else {
    Serial.println("Error en solicitud HTTP: " + String(httpCode));
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Error al enviar");
    lcd.setCursor(0, 1);
    lcd.print("Codigo: " + String(httpCode));
    delay(1000);
  }
  
  http.end();
}