/*********************************************
 * Proyecto: MSX-SDF-1                       *
 * Prueba: matriz 8x8 con MAX7219 por SPI    *
 * Maestro: Arduino Nano                     *
 *********************************************/

// Primer paso con la matriz: verificar cables, descubrir como quedo montado el
// vidrio y medir el consumo, sin tocar el SDF-1. El Nano hace de maestro igual
// que lo va a hacer el ATmega del cartucho: los dos son ATmega328P a 5 V con el
// mismo SPI por hardware.
//
// Conexion (el modulo tipico de 5 pines):
//   Nano 5V  -> VCC        Nano GND -> GND
//   Nano D11 (MOSI, PB3) -> DIN
//   Nano D13 (SCK,  PB5) -> CLK
//   Nano D9  (PB1)       -> CS
// En el cartucho MOSI y SCK son los mismos pines que la SD (H1-2/3/4) y el CS
// tiene que ser PB0 (D8) o PB1 (D9): son los unicos GPIO libres con la SD
// montada. Uso D9 aca para que el cableado sea el mismo. D10 (PB2) no se toca:
// es el CS de la SD en el SDF-1, y en el Nano es el SS del SPI, que SPI.begin()
// deja como salida (si quedara entrada y se fuera a masa, el SPI pasa a esclavo).
//
// El MAX7219 es mudo: no devuelve nada por MISO (su DOUT es para encadenar
// modulos). No se le puede leer el estado: si algo no anda, se ve o no se ve.
//
// Monitor serie a 115200. Corre la secuencia de pruebas en orden y acepta:
//   r = rotar 90 grados   m = espejar   i = invertir (negativo)
//   + / - = brillo        t = display test        espacio = pausar
//   1..6 = ir a una prueba

#include <SPI.h>

#define CS_PIN      9
#define SPI_HZ      4000000  // el MAX7219 aguanta 10 MHz; 4 va sobrado y perdona el protoboard
#define STEP_MS     700

// Registros del MAX7219. Los digitos 1..8 son las ocho filas de la matriz.
#define REG_NOOP        0x00
#define REG_DIGIT0      0x01
#define REG_DECODEMODE  0x09
#define REG_INTENSITY   0x0A
#define REG_SCANLIMIT   0x0B
#define REG_SHUTDOWN    0x0C
#define REG_DISPLAYTEST 0x0F

// Fuente 5x7 por columnas (bit 0 = arriba), solo los caracteres que uso.
const uint8_t FONT_S[5] PROGMEM = { 0x26, 0x49, 0x49, 0x49, 0x32 };
const uint8_t FONT_D[5] PROGMEM = { 0x7F, 0x41, 0x41, 0x22, 0x1C };
const uint8_t FONT_F[5] PROGMEM = { 0x7F, 0x09, 0x09, 0x01, 0x01 };
const uint8_t FONT_GUION[5] PROGMEM = { 0x08, 0x08, 0x08, 0x08, 0x08 };
const uint8_t FONT_1[5] PROGMEM = { 0x00, 0x42, 0x7F, 0x40, 0x00 };
const uint8_t* const MENSAJE[] = { FONT_S, FONT_D, FONT_F, FONT_GUION, FONT_1 };
#define MENSAJE_LEN 5

// Una carita: algo asimetrico arriba-abajo, para que se note el espejado.
const uint8_t CARITA[8] PROGMEM = {
  0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C
};

uint8_t _rot = 0;         // cuartos de vuelta en sentido horario
bool    _mirror = false;
bool    _invert = false;
uint8_t _brillo = 2;      // 0..15; arranca bajo a proposito, por el consumo
uint8_t _prueba = 1;
bool    _pausa = false;

// ---------------------------------------------------------------- transporte

// Una trama es registro + dato, MSB primero, y se latchea en el flanco de
// subida del CS. Ojo con el CS: el MAX7219 mete en su registro de
// desplazamiento todo lo que pase por DIN, este el CS arriba o abajo; lo unico
// que hace el flanco de subida es latchear los ultimos 16 bits. Por eso en el
// cartucho esto va dentro de la mascara de PCIE1: si la ISR entra en el medio,
// clockea bytes de la SD detras de los nuestros y se latchea cualquier cosa en
// cualquier registro.
void escribir(uint8_t reg, uint8_t dato)
{
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(reg);
  SPI.transfer(dato);
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
}

// ------------------------------------------------------------- orientacion

// El frame se piensa siempre igual: fila 0 arriba, bit 7 a la izquierda. Los
// modulos traen la matriz montada de cualquier manera respecto del chip, asi
// que el arreglo es de software y se decide mirando el vidrio, no el datasheet.
inline bool leerPixel(const uint8_t* f, uint8_t x, uint8_t y)
{
  return (f[y] >> (7 - x)) & 1;
}

void transformar(const uint8_t* origen, uint8_t* destino)
{
  for (uint8_t i = 0; i < 8; i++)
    destino[i] = 0;

  for (uint8_t y = 0; y < 8; y++)
  {
    for (uint8_t x = 0; x < 8; x++)
    {
      uint8_t sx = x, sy = y;

      // Coordenada de origen para cada cuarto de vuelta horario.
      switch (_rot & 3)
      {
        case 1: sx = y;     sy = 7 - x; break;
        case 2: sx = 7 - x; sy = 7 - y; break;
        case 3: sx = 7 - y; sy = x;     break;
      }
      if (_mirror)
        sx = 7 - sx;

      if (leerPixel(origen, sx, sy))
        destino[y] |= 0x80 >> x;
    }
  }

  if (_invert)
    for (uint8_t i = 0; i < 8; i++)
      destino[i] = ~destino[i];
}

// Manda el cuadro entero: ocho tramas y listo. El barrido de las filas lo hace
// el chip por hardware, asi que despues no hay nada que refrescar.
void mostrar(const uint8_t* frame)
{
  uint8_t f[8];
  transformar(frame, f);
  for (uint8_t i = 0; i < 8; i++)
    escribir(REG_DIGIT0 + i, f[i]);
}

void limpiar()
{
  for (uint8_t i = 0; i < 8; i++)
    escribir(REG_DIGIT0 + i, 0x00);
}

void inicializar()
{
  escribir(REG_DISPLAYTEST, 0x00); // fuera del modo test
  escribir(REG_DECODEMODE,  0x00); // sin decodificar: el BCD es para 7 segmentos
  escribir(REG_SCANLIMIT,   0x07); // las ocho filas (menos filas = mas brillo y menos consumo)
  escribir(REG_INTENSITY,   _brillo);
  limpiar();
  escribir(REG_SHUTDOWN,    0x01); // 1 = operando; en 0 apaga los LEDs y conserva los registros
}

// ------------------------------------------------------------------ pruebas

// 1. Fila por fila: dice cual es el REG_DIGIT0 fisico y hacia donde crecen.
void pruebaFilas()
{
  Serial.println(F("[1] fila por fila, de arriba hacia abajo"));
  for (uint8_t y = 0; y < 8 && !Serial.available(); y++)
  {
    uint8_t f[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    f[y] = 0xFF;
    mostrar(f);
    delay(STEP_MS / 2);
  }
}

// 2. Columna por columna: lo mismo para el otro eje. Si las filas salen como
//    columnas, falta una rotacion (tecla r).
void pruebaColumnas()
{
  Serial.println(F("[2] columna por columna, de izquierda a derecha"));
  for (uint8_t x = 0; x < 8 && !Serial.available(); x++)
  {
    uint8_t f[8];
    for (uint8_t i = 0; i < 8; i++)
      f[i] = 0x80 >> x;
    mostrar(f);
    delay(STEP_MS / 2);
  }
}

// 3. Una esquina sola: desambigua el espejado, que las filas y las columnas
//    solas no delatan. Tiene que quedar arriba a la izquierda.
void pruebaEsquina()
{
  Serial.println(F("[3] un punto en la esquina de arriba a la izquierda"));
  uint8_t f[8] = { 0x80, 0, 0, 0, 0, 0, 0, 0 };
  mostrar(f);
  delay(STEP_MS * 2);
}

// 4. Rampa de brillo: los 16 pasos del registro de intensidad.
void pruebaBrillo()
{
  Serial.println(F("[4] rampa de brillo, 16 pasos"));
  uint8_t f[8];
  for (uint8_t i = 0; i < 8; i++)
    f[i] = pgm_read_byte(&CARITA[i]);
  mostrar(f);

  for (uint8_t n = 0; n < 16 && !Serial.available(); n++)
  {
    escribir(REG_INTENSITY, n);
    delay(120);
  }
  escribir(REG_INTENSITY, _brillo);
  delay(STEP_MS);
}

// 5. Todo encendido: el momento de medir el consumo, con el tester en serie con
//    el 5 V. El slot del MSX no da mucho mas que un par de cientos de mA, asi
//    que de aca sale el techo del registro de intensidad en el cartucho.
void pruebaConsumo()
{
  Serial.println(F("[5] los 64 LEDs encendidos: medir el consumo aca"));
  uint8_t f[8];
  for (uint8_t i = 0; i < 8; i++)
    f[i] = 0xFF;
  mostrar(f);

  for (uint8_t n = 0; n < 16 && !Serial.available(); n += 3)
  {
    escribir(REG_INTENSITY, n);
    Serial.print(F("    intensidad "));
    Serial.println(n);
    delay(1500);
  }
  escribir(REG_INTENSITY, _brillo);
}

// 6. "SDF-1" desplazandose: el caso real del dispositivo SPI:, ocho tramas por
//    cuadro y nada mas.
void pruebaTexto()
{
  Serial.println(F("[6] SDF-1 desplazandose"));

  uint8_t cols[MENSAJE_LEN * 6 + 8];
  uint8_t n = 0;
  for (uint8_t c = 0; c < MENSAJE_LEN; c++)
  {
    for (uint8_t i = 0; i < 5; i++)
      cols[n++] = pgm_read_byte(&MENSAJE[c][i]) << 1; // una fila de aire arriba
    cols[n++] = 0x00;                                 // separacion entre letras
  }
  for (uint8_t i = 0; i < 8; i++)
    cols[n++] = 0x00;                                 // que termine de salir

  for (uint8_t desde = 0; desde + 8 < n && !Serial.available(); desde++)
  {
    // De columnas a filas: la misma trasposicion que arregla un modulo girado.
    uint8_t f[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    for (uint8_t x = 0; x < 8; x++)
      for (uint8_t y = 0; y < 8; y++)
        if ((cols[desde + x] >> y) & 1)
          f[y] |= 0x80 >> x;
    mostrar(f);
    delay(90);
  }
  delay(STEP_MS);
}

// -------------------------------------------------------------------- teclas

void estado()
{
  Serial.print(F("    rotacion "));
  Serial.print(_rot * 90);
  Serial.print(F("  espejo "));
  Serial.print(_mirror ? F("si") : F("no"));
  Serial.print(F("  invertido "));
  Serial.print(_invert ? F("si") : F("no"));
  Serial.print(F("  brillo "));
  Serial.println(_brillo);
}

void atenderSerie()
{
  while (Serial.available())
  {
    char c = Serial.read();
    switch (c)
    {
      case 'r': _rot = (_rot + 1) & 3; estado(); break;
      case 'm': _mirror = !_mirror;    estado(); break;
      case 'i': _invert = !_invert;    estado(); break;
      case '+':
        if (_brillo < 15)
          _brillo++;
        escribir(REG_INTENSITY, _brillo);
        estado();
        break;
      case '-':
        if (_brillo)
          _brillo--;
        escribir(REG_INTENSITY, _brillo);
        estado();
        break;
      case 't':
        // El display test prende los 64 LEDs al maximo sin tocar los registros:
        // separa un problema de cableado de uno de datos.
        Serial.println(F("    display test 2 s (consumo maximo)"));
        escribir(REG_DISPLAYTEST, 0x01);
        delay(2000);
        escribir(REG_DISPLAYTEST, 0x00);
        break;
      case ' ':
        _pausa = !_pausa;
        Serial.println(_pausa ? F("    en pausa") : F("    sigo"));
        break;
      default:
        if (c >= '1' && c <= '6')
        {
          _prueba = c - '0';
          Serial.print(F("    voy a la prueba "));
          Serial.println(_prueba);
        }
        break;
    }
  }
}

// ---------------------------------------------------------------------------

void setup()
{
  Serial.begin(115200);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH); // arriba antes del primer reloj, o latchea basura
  SPI.begin();

  inicializar();

  Serial.println(F("MAX7219 listo (no contesta nada: se ve o no se ve)"));
  Serial.println(F("teclas: r rotar  m espejar  i invertir  + - brillo  t test  1..6 prueba  espacio pausa"));
  estado();
}

void loop()
{
  atenderSerie();

  if (_pausa)
  {
    delay(50);
    return;
  }

  switch (_prueba)
  {
    case 1: pruebaFilas();    break;
    case 2: pruebaColumnas(); break;
    case 3: pruebaEsquina();  break;
    case 4: pruebaBrillo();   break;
    case 5: pruebaConsumo();  break;
    case 6: pruebaTexto();    break;
  }

  limpiar();
  if (!Serial.available()) // si tocaste una tecla, la atiende antes de avanzar
    _prueba = (_prueba % 6) + 1;
}
