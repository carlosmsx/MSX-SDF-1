/*********************************************
 * Proyecto: MSX-SDF-1                       *
 * Prueba: enlace I2C con un ESP32           *
 * Esclavo: ESP32 DevKit                     *
 *********************************************/

// El ESP32 como esclavo I2C en 0x42. Muestra por serie cada linea que recibe
// y deja cargada la respuesta para la proxima lectura del maestro (el Nano de
// maestro-nano.ino, y despues el ATmega del SDF-1).
//
// La respuesta se carga en onReceive y no en onRequest: el ESP32 clasico (el
// DevKit) no puede estirar el reloj. Cuando el maestro lee, el esclavo manda
// lo que ya tenia en la cola; onRequest recien corre despues de la lectura y
// lo que escribe queda para la siguiente (esp32-hal-i2c-slave.c, core 3.3.8).
//
// Largo fijo: si el maestro lee menos de lo cargado, lo que sobra queda en la
// cola y corre todas las respuestas siguientes. RESPONSE_LEN es el mismo en
// los dos sketches.
//
// Placa: "ESP32 Dev Module". Monitor serie a 115200. Alimentado por su USB:
// el pin 5V/VIN no se conecta al SDF-1 ni al Nano.

#include <Arduino.h>
#include <Wire.h>

#define I2C_ADDR      0x42  // tiene que coincidir con maestro-nano.ino
#define RESPONSE_LEN  32
#define SDA_PIN       21    // al lado LV del adaptador de niveles
#define SCL_PIN       22

unsigned long _count = 0;

void onReceive(int len)
{
  char line[RESPONSE_LEN + 1];
  int n = 0;
  while (Wire.available())
  {
    int c = Wire.read();
    if (n < RESPONSE_LEN)
      line[n++] = (char)c;
  }
  line[n] = 0;
  Serial.printf("recibido [%d]: %s\n", len, line);

  // Relleno con ceros: el maestro corta ahi.
  char response[RESPONSE_LEN];
  memset(response, 0, sizeof(response));
  snprintf(response, sizeof(response), "hola desde ESP32 %lu", _count++);
  Wire.slaveWrite((uint8_t *)response, sizeof(response));
}

void setup()
{
  Serial.begin(115200);
  Wire.onReceive(onReceive);
  if (!Wire.begin((uint8_t)I2C_ADDR, SDA_PIN, SCL_PIN, 100000))
    Serial.println("Wire.begin fallo");
  Serial.printf("Esclavo I2C en 0x%02X, SDA %d, SCL %d\n", I2C_ADDR, SDA_PIN, SCL_PIN);
}

void loop()
{
  delay(1000);
}
