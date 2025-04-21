import network
from machine import Pin, ADC, PWM
from umqtt.simple import MQTTClient
from time import sleep
import machine
import random

# === Configuración WiFi ===
WIFI_SSID = "INFINITUM2A4D_2.4"
WIFI_PASSWORD = "uTDmnG5K9S"

# === Configuración MQTT ===
MQTT_BROKER = "broker.emqx.io"
MQTT_PORT = 1883
MQTT_CLIENT_ID = "esp32_riego_123"
MQTT_TOPIC_SENSOR_SUELO = "gds0642/carh/suelo"  # Temperatura en lugar de humedad
MQTT_TOPIC_SENSOR_AGUA = "gds0642/carh/agua"
MQTT_TOPIC_SERVO = "gds0642/carh/servo"
MQTT_TOPIC_SERVO_POS = "gds0642/carh/servo_pos"  # Tópico para feedback de posición del servo
MQTT_TOPIC_RELE_FOCO = "gds0642/carh/foco"

# === Pines ===
PIN_SENSOR_SUELO = 34
PIN_SENSOR_AGUA = 35
PIN_RELE_FOCO = 19
PIN_SERVO = 16

# === Inicialización de componentes ===
adc_suelo = ADC(Pin(PIN_SENSOR_SUELO))
adc_suelo.atten(ADC.ATTN_11DB)  # Rango de 0 a 3.3V
adc_agua = ADC(Pin(PIN_SENSOR_AGUA))
adc_agua.atten(ADC.ATTN_11DB)
rele_foco = Pin(PIN_RELE_FOCO, Pin.OUT)
rele_foco.value(0)  # Apagado por defecto
servo = PWM(Pin(PIN_SERVO), freq=50)
servo.duty(26)  # Posición inicial 0 grados

# Variable global para el cliente MQTT y posición del servo
cliente = None
posicion_servo_actual = 0  # Para mantener registro de la posición del servo

# === Conectar WiFi ===
def conectar_wifi():
    print("Conectando a WiFi...")
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    wlan.connect(WIFI_SSID, WIFI_PASSWORD)
    for i in range(10):
        if wlan.isconnected():
            break
        print(".", end="")
        sleep(1)
    if wlan.isconnected():
        print("\nWiFi conectada:", wlan.ifconfig())
    else:
        print("\nNo se pudo conectar. Reiniciando...")
        machine.reset()

# === Función para mover servo ===
def mover_servo(angulo):
    global posicion_servo_actual
    # Asegurar que el ángulo esté dentro del rango válido
    angulo = max(0, min(180, angulo))
    
    # Para ESP32, el rango de duty es de 0 a 1023
    duty = int(26 + (angulo / 180) * 102)
    servo.duty(duty)
    posicion_servo_actual = angulo
    
    print("Servo movido a:", angulo, "grados")
    
    # Publicar la posición actual del servo para actualizar la interfaz
    if cliente:
        cliente.publish(MQTT_TOPIC_SERVO_POS, str(angulo))
    
    return angulo

# === Función para manejar mensajes MQTT ===
def manejar_mensaje(topic, msg):
    try:
        topic = topic.decode('utf-8') if isinstance(topic, bytes) else topic
        msg_str = msg.decode('utf-8').strip() if isinstance(msg, bytes) else str(msg).strip()
        
        print(f"\nMensaje recibido - Tópico: {topic}, Mensaje: {msg_str}")
        
        if topic == MQTT_TOPIC_SERVO:
            # Verificación estricta del formato
            if msg_str.lower().startswith("servo:"):
                try:
                    angulo = int(msg_str.split(':')[1])
                    angulo_final = mover_servo(angulo)
                    # Publicar el ángulo final (puede ser diferente si estaba fuera de rango)
                    cliente.publish(MQTT_TOPIC_SERVO_POS, str(angulo_final))
                except (IndexError, ValueError):
                    print("Formato inválido. Use 'servo:ANGULO'")
            elif msg_str.isdigit():
                angulo = int(msg_str)
                angulo_final = mover_servo(angulo)
                # Publicar el ángulo final
                cliente.publish(MQTT_TOPIC_SERVO_POS, str(angulo_final))
            else:
                print("Mensaje no reconocido para servo")
                
        elif topic == MQTT_TOPIC_RELE_FOCO:
            estado = 1 if msg_str.lower() in ('1', 'on', 'true') else 0
            rele_foco.value(estado)
            print("Foco:", "ENCENDIDO" if estado else "APAGADO")
            
    except Exception as e:
        print("Error crítico en manejar_mensaje:", e)
        
# === Conectar a MQTT ===
def conectar_mqtt():
    global cliente
    mover_servo(0)  # Inicializar servo en posición 0
    try:
        cliente = MQTTClient(MQTT_CLIENT_ID, MQTT_BROKER, port=MQTT_PORT)
        cliente.set_callback(manejar_mensaje)
        cliente.connect()
        cliente.subscribe(MQTT_TOPIC_SERVO)
        cliente.subscribe(MQTT_TOPIC_RELE_FOCO)
        print("Conectado a MQTT y suscrito a tópicos")
        
        # Publicar la posición inicial del servo para sincronizar la interfaz
        cliente.publish(MQTT_TOPIC_SERVO_POS, str(posicion_servo_actual))
        
    except Exception as e:
        print("Error al conectar a MQTT:", e)
        sleep(5)
        machine.reset()
    return cliente

# Variables para mantener consistencia en los valores
ultima_temperatura = 27.0  # Valor inicial de temperatura
ultimo_valor_agua = 2000   # Valor inicial medio

# === Loop principal ===
def loop_principal():
    global cliente, ultima_temperatura, ultimo_valor_agua, posicion_servo_actual
    
    while True:
        try:
            cliente.check_msg()  # Revisa mensajes entrantes
            
            # Temperatura: variación pequeña con respecto al valor anterior
            # El valor estará entre 26°C y 28°C
            variacion_temp = random.uniform(-0.2, 0.2)
            temperatura = max(26.0, min(28.0, ultima_temperatura + variacion_temp))
            temperatura = round(temperatura, 1)  # Redondear a 1 decimal
            ultima_temperatura = temperatura
            
            # Nivel de agua: valor máximo de 50% (2048 en escala de 0-4095)
            # Pequeña variación desde el valor anterior para simular cambios graduales
            variacion_agua = random.randint(-40, 40)
            valor_agua = max(1900, min(2048, ultimo_valor_agua + variacion_agua))
            ultimo_valor_agua = valor_agua
            
            # Calcular porcentaje de agua
            porcentaje_agua = round((valor_agua / 4095) * 100, 1)
            
            print(f"Temperatura: {temperatura}°C, Nivel agua: {valor_agua} ({porcentaje_agua}%), Servo: {posicion_servo_actual}°")
            
            # Publicar valores
            cliente.publish(MQTT_TOPIC_SENSOR_SUELO, str(temperatura))  # Temperatura
            cliente.publish(MQTT_TOPIC_SENSOR_AGUA, str(porcentaje_agua))  # Nivel de agua
            cliente.publish(MQTT_TOPIC_SERVO_POS, str(posicion_servo_actual))  # Posición actual del servo
            
            sleep(5)
        except Exception as e:
            print("Error en el loop principal:", e)
            sleep(5)
            try:
                print("Intentando reconectar...")
                if cliente:
                    try:
                        cliente.disconnect()
                    except:
                        pass
                sleep(2)
                cliente = conectar_mqtt()
            except:
                print("Error al reconectar. Reiniciando...")
                machine.reset()

# === Ejecución ===
try:
    conectar_wifi()
    cliente = conectar_mqtt()
    loop_principal()
except Exception as e:
    print("Error general:", e)
    machine.reset()