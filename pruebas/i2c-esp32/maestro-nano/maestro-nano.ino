/*********************************************
 * Proyecto: MSX-SDF-1                       *
 * Prueba: enlace I2C con un ESP32           *
 * Maestro: Arduino Nano                     *
 *********************************************/

// Primer paso de la prueba con el ESP32: verificar cables, adaptador de niveles
// y el sketch esclavo, sin tocar el SDF-1. El Nano hace de maestro igual que lo
// va a hacer el ATmega del cartucho: los dos son ATmega328P a 5 V.
//
// Conexion (todo con los dos USB enchufados a la misma PC):
//   Nano 5V  -> adaptador HV        ESP32 3V3    -> adaptador LV
//   Nano GND -> adaptador GND (HV y LV) <- ESP32 GND
//   Nano A4  -> HV1   LV1 -> ESP32 GPIO21 (SDA)
//   Nano A5  -> HV2   LV2 -> ESP32 GPIO22 (SCL)
// Sin 5V ni VIN del ESP32 conectados a nada: se alimenta por su USB.
//
// Monitor serie a 115200. Al arrancar busca direcciones; despues, cada segundo,
// le manda una linea al ESP32 y le lee la respuesta.

#include <Wire.h>

#define ESP32_ADDR    0x42  // tiene que coincidir con esclavo-esp32.ino
#define RESPONSE_LEN  32    // bytes que se leen por pedido; el esclavo contesta este largo
#define PERIOD_MS     1000

unsigned long _count = 0;

void scan()
{
  Serial.print(F("Direcciones que responden:"));
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++)
  {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0)
    {
      Serial.print(F(" 0x"));
      if (addr < 0x10)
        Serial.print('0');
      Serial.print(addr, HEX);
      found++;
    }
  }
  if (!found)
    Serial.print(F(" ninguna (revisar cables, alimentacion y pull-ups)"));
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  Wire.begin();  // prende los pull-ups internos a 5 V: del lado HV del adaptador no molestan
  Serial.println(F("Maestro I2C listo"));
  scan();
}

void loop()
{
  char line[24];
  snprintf(line, sizeof(line), "ping %lu", _count++);

  // Escritura: endTransmission devuelve 0 si el esclavo reconocio todo,
  // 2 si no contesto a la direccion, 3 si no reconocio un dato, 5 timeout.
  Wire.beginTransmission(ESP32_ADDR);
  Wire.write(line);
  uint8_t err = Wire.endTransmission();
  Serial.print(F("> "));
  Serial.print(line);
  Serial.print(F("  resultado "));
  Serial.println(err);

  // Le doy tiempo al ESP32 para preparar la respuesta en su onReceive.
  delay(50);

  // Lectura de largo fijo: el esclavo no puede estirar el reloj, asi que
  // conviene que el largo pedido sea el que tiene cargado.
  uint8_t n = Wire.requestFrom((uint8_t)ESP32_ADDR, (uint8_t)RESPONSE_LEN);
  Serial.print(F("< "));
  while (Wire.available())
  {
    char c = Wire.read();
    if (c == 0)
      break;  // el resto es relleno
    Serial.print(c >= 32 && c < 127 ? c : '.');
  }
  while (Wire.available())
    Wire.read();
  Serial.print(F("  ("));
  Serial.print(n);
  Serial.println(F(" bytes)"));

  delay(PERIOD_MS);
}
