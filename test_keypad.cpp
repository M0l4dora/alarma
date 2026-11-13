
#include <Arduino.h>
#include <Keypad.h>

// Configuracion del teclado
const byte FILAS = 4;
const byte COLUMNAS = 4;

// Distribucion de las teclas
char teclas[FILAS][COLUMNAS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// Pines conectados al Arduino
byte pinesFilas[FILAS] = {9, 10, 11, 12};
byte pinesColumnas[COLUMNAS] = {2, 3, 5, 6};

// Crear objeto teclado
Keypad teclado = Keypad(
  makeKeymap(teclas),
  pinesFilas,
  pinesColumnas,
  FILAS,
  COLUMNAS
);

void setup() {
  Serial.begin(9600);

  Serial.println("================================");
  Serial.println("   PRUEBA TECLADO MATRICIAL 4x4");
  Serial.println("================================");
  Serial.println("Presione cualquier tecla...");
}

void loop() {

  char tecla = teclado.getKey();

  if (tecla) {
    Serial.print("Tecla presionada: ");
    Serial.println(tecla);
  }

}
