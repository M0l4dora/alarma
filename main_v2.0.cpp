#include <Arduino.h>

const int pinLED = 7;
const int pinBUZZ = 8;
const int pinPIR = 4;

bool movimientoDetectado = false;

void setup() {
  pinMode(pinLED, OUTPUT);
  pinMode(pinBUZZ, OUTPUT);
  pinMode(pinPIR, INPUT);

  // Indica que el sensor PIR está calentándose
  digitalWrite(pinLED, HIGH);

  // Tiempo de calentamiento del PIR
  delay(10000);

  // Sensor listo
  digitalWrite(pinLED, LOW);
}

void loop() {
  int estado = digitalRead(pinPIR);

  // Si detecta movimiento y antes no estaba activo
  if (estado == HIGH && !movimientoDetectado) {
    movimientoDetectado = true;

    // Alerta de movimiento
    digitalWrite(pinLED, HIGH);
    digitalWrite(pinBUZZ, HIGH);
    delay(500);

    digitalWrite(pinLED, LOW);
    digitalWrite(pinBUZZ, LOW);
    delay(500);
  }

  // Si el PIR vuelve a LOW, permite nueva detección
  if (estado == LOW) {
    movimientoDetectado = false;
  }

  delay(50);
}
