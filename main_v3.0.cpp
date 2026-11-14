#include <Arduino.h>
#include <Keypad.h>

// ============================================================
//  ALARMA CON SENSOR PIR 501 Y TECLADO MATRICIAL 4x4
// ============================================================
//  Consigna:
//   - Reposo (sin deteccion) ..... LED verde encendido
//   - Deteccion .................. LED rojo + buzzer intermitente
//   - Solo se desactiva con la clave correcta por teclado
//
//  Teclas: A activar | B desactivar | C cambiar clave
//          * borrar   | # enter
// ============================================================

// ------------------------------------------------------------
//  1. HARDWARE
// ------------------------------------------------------------
const int pinLEDVerde = 13;   // LED de reposo
const int pinLEDRojo  = 7;    // LED de alarma
const int pinBUZZ     = 8;    // buzzer
const int pinPIR      = 4;    // sensor de movimiento

const byte FILAS = 4;
const byte COLUMNAS = 4;
char teclas[FILAS][COLUMNAS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
byte pinesFilas[FILAS] = {9, 10, 11, 12};
byte pinesColumnas[COLUMNAS] = {2, 3, 5, 6};

Keypad teclado(makeKeymap(teclas), pinesFilas, pinesColumnas, FILAS, COLUMNAS);

// ------------------------------------------------------------
//  2. CONFIGURACION
// ------------------------------------------------------------
const unsigned long TIEMPO_CALENTAMIENTO = 10000UL;  // estabilizacion del PIR
const unsigned long TIEMPO_BIP = 400UL;              // intermitencia del buzzer
const byte MIN_CLAVE = 4;                            // largo minimo de la clave
const byte MAX_CLAVE = 8;                            // largo maximo de la clave

// ------------------------------------------------------------
//  3. ESTADO DEL SISTEMA
// ------------------------------------------------------------
enum Estado {
  MENU,          // esperando una opcion (A, B o C)
  PEDIR_CLAVE,   // escribiendo una clave
  CALENTANDO,    // estabilizando el PIR
  SONANDO        // alarma disparada
};

enum Objetivo {
  ARMAR,
  DESARMAR,
  CAMBIAR_VIEJA,
  CAMBIAR_NUEVA
};

Estado estado = MENU;
Objetivo objetivo = ARMAR;
bool armada = false;                 // el usuario activo la alarma

char clave[MAX_CLAVE + 1] = "1234";  // clave actual (no se muestra nunca)
char entrada[MAX_CLAVE + 1] = "";    // lo que se va escribiendo
byte largo = 0;

unsigned long tCalentamiento = 0;
unsigned long tBip = 0;
int ultimoSegundo = -1;
bool bipEncendido = false;

// ------------------------------------------------------------
//  4. ENTRADA DE LA CLAVE (siempre oculta en el monitor)
// ------------------------------------------------------------
void limpiarEntrada() {
  entrada[0] = '\0';
  largo = 0;
}

bool claveCorrecta() {
  return strcmp(entrada, clave) == 0;
}

void mostrarClaveOculta() {
  Serial.print("   Clave: ");
  if (largo == 0) Serial.print("(vacia)");
  for (byte i = 0; i < largo; i++) Serial.print('*');
  Serial.println();
}

// Agrega o borra un caracter. Devuelve false si la tecla no es de edicion.
bool editarClave(char t) {
  if (t >= '0' && t <= '9') {
    if (largo >= MAX_CLAVE) {
      Serial.println("   (maximo 8 digitos)");
      return true;
    }
    entrada[largo++] = t;
    entrada[largo] = '\0';
  } else if (t == '*') {
    if (largo == 0) {
      Serial.println("   (nada que borrar)");
      return true;
    }
    entrada[--largo] = '\0';
  } else {
    return false;
  }
  mostrarClaveOculta();
  return true;
}

// ------------------------------------------------------------
//  5. LEDS Y BUZZER
// ------------------------------------------------------------
void modoReposo() {
  digitalWrite(pinLEDVerde, HIGH);   // reposo = verde encendido
  digitalWrite(pinLEDRojo, LOW);
  digitalWrite(pinBUZZ, LOW);
  bipEncendido = false;
}

void dispararAlarma() {
  limpiarEntrada();
  estado = SONANDO;
  digitalWrite(pinLEDVerde, LOW);
  digitalWrite(pinLEDRojo, HIGH);    // deteccion = rojo encendido
  tBip = millis();
  bipEncendido = true;
  digitalWrite(pinBUZZ, HIGH);

  Serial.println("\n!!! MOVIMIENTO DETECTADO !!!");
  Serial.println("ALARMA ACTIVA: LED rojo + buzzer intermitente.");
  Serial.println("Ingrese la contrasena y # para detenerla.");
}

void detenerAlarma() {
  armada = false;
  modoReposo();
}

// ------------------------------------------------------------
//  6. MENU Y TRANSICIONES
// ------------------------------------------------------------
void mostrarMenu() {
  Serial.println();
  Serial.println("============ MENU ============");
  Serial.print("Sistema: ");
  Serial.println(armada ? "ARMADO (vigilando)" : "DESARMADO");
  Serial.println("A = Activar alarma");
  Serial.println("B = Desactivar alarma");
  Serial.println("C = Cambiar contrasena");
  Serial.println("* = borrar | # = enter");
  Serial.println("==============================");
}

void volverAlMenu() {
  limpiarEntrada();
  estado = MENU;
  mostrarMenu();
}

void pedirClave(Objetivo obj, const char* mensaje) {
  limpiarEntrada();
  objetivo = obj;
  estado = PEDIR_CLAVE;
  Serial.println();
  Serial.println(mensaje);
  Serial.println("Ingrese la contrasena (* borra, # confirma):");
}

void iniciarCalentamiento() {
  limpiarEntrada();
  estado = CALENTANDO;
  ultimoSegundo = -1;
  tCalentamiento = millis();
  modoReposo();
  Serial.println("\nClave correcta. Calentando el sensor (10 s)...");
}

// ------------------------------------------------------------
//  7. CONFIRMACION CON #
// ------------------------------------------------------------
void confirmarClave() {
  bool ok = claveCorrecta();

  switch (objetivo) {

    case ARMAR:
      if (ok) {
        iniciarCalentamiento();
        return;
      }
      Serial.println("\nContrasena INCORRECTA. La alarma no se activo.");
      volverAlMenu();
      break;

    case DESARMAR:
      if (ok) {
        detenerAlarma();
        Serial.println("\n>>> ALARMA DESACTIVADA <<<");
      } else {
        Serial.println("\nContrasena INCORRECTA. La alarma sigue activa.");
      }
      volverAlMenu();
      break;

    case CAMBIAR_VIEJA:
      if (ok) {
        limpiarEntrada();
        objetivo = CAMBIAR_NUEVA;
        Serial.println("\nClave actual correcta.");
        Serial.println("Ingrese la NUEVA contrasena (4 a 8 digitos) y #:");
        return;
      }
      Serial.println("\nContrasena INCORRECTA. No se cambio la clave.");
      volverAlMenu();
      break;

    case CAMBIAR_NUEVA:
      if (largo < MIN_CLAVE) {
        Serial.print("\nMinimo ");
        Serial.print(MIN_CLAVE);
        Serial.println(" digitos. Siga escribiendo.");
        return;
      }
      strcpy(clave, entrada);
      Serial.println("\n>>> CONTRASENA CAMBIADA <<<");
      volverAlMenu();
      break;
  }
}

// ------------------------------------------------------------
//  8. TECLADO
// ------------------------------------------------------------
void procesarTecla(char t) {

  // ---- Escribiendo la clave (nunca se muestra el digito) ----
  if (estado == PEDIR_CLAVE || estado == SONANDO) {
    if (editarClave(t)) return;   // digitos y '*': solo se ven asteriscos

    Serial.print("\nTecla: ");
    Serial.print(t);

    if (t == '#') {
      if (estado == SONANDO) {
        if (claveCorrecta()) {
          detenerAlarma();
          Serial.println("\nContrasena correcta. Alarma detenida.");
          volverAlMenu();
        } else {
          limpiarEntrada();
          Serial.println("\nContrasena INCORRECTA. La alarma sigue sonando.");
        }
      } else {
        confirmarClave();
      }
      return;
    }

    // Una letra cancela (o se rechaza si la alarma esta sonando)
    if (estado == SONANDO) {
      Serial.println("\nIngrese la contrasena para detener la alarma.");
    } else {
      Serial.println("\nOperacion cancelada.");
      volverAlMenu();
    }
    return;
  }

  // ---- Calentando el PIR ----
  if (estado == CALENTANDO) {
    Serial.print("\nTecla: ");
    Serial.print(t);
    Serial.println(" (calentando, espere)");
    return;
  }

  // ---- MENU ----
  Serial.print("\nTecla: ");
  Serial.print(t);

  switch (t) {
    case 'A':
      if (armada) {
        Serial.println("\nLa alarma ya esta armada.");
      } else {
        pedirClave(ARMAR, "--- ACTIVAR ALARMA ---");
      }
      break;

    case 'B':
      if (!armada) {
        Serial.println("\nLa alarma ya esta desactivada.");
      } else {
        pedirClave(DESARMAR, "--- DESACTIVAR ALARMA ---");
      }
      break;

    case 'C':
      pedirClave(CAMBIAR_VIEJA, "--- CAMBIAR CONTRASENA ---");
      break;

    default:
      Serial.println("\nOpcion invalida. Use A, B o C.");
      mostrarMenu();
      break;
  }
}

// ------------------------------------------------------------
//  9. SETUP Y LOOP
// ------------------------------------------------------------
void setup() {
  pinMode(pinLEDVerde, OUTPUT);
  pinMode(pinLEDRojo, OUTPUT);
  pinMode(pinBUZZ, OUTPUT);
  pinMode(pinPIR, INPUT);
  modoReposo();

  Serial.begin(9600);
  Serial.println("\n=== ALARMA PIR 501 + TECLADO 4x4 ===");
  Serial.println("LED verde: reposo | LED rojo + buzzer: alarma");
  Serial.println("A activar | B desactivar | C cambiar clave");
  mostrarMenu();
}

void loop() {
  // ---- Calentamiento del PIR (no bloquea el teclado) ----
  if (estado == CALENTANDO) {
    unsigned long t = millis() - tCalentamiento;
    if (t >= TIEMPO_CALENTAMIENTO) {
      armada = true;
      estado = MENU;
      Serial.println("\n>>> ALARMA ARMADA <<<");
      mostrarMenu();
    } else {
      int s = (TIEMPO_CALENTAMIENTO - t + 999) / 1000;
      if (s != ultimoSegundo) {
        ultimoSegundo = s;
        Serial.print("Calentando... ");
        Serial.print(s);
        Serial.println(" s");
      }
    }
  }

  // ---- Buzzer intermitente mientras la alarma esta activa ----
  if (estado == SONANDO && millis() - tBip >= TIEMPO_BIP) {
    tBip = millis();
    bipEncendido = !bipEncendido;
    digitalWrite(pinBUZZ, bipEncendido);
  }

  // ---- Deteccion de movimiento ----
  if (estado == MENU && armada && digitalRead(pinPIR) == HIGH) {
    dispararAlarma();
  }

  // ---- Teclado ----
  char t = teclado.getKey();
  if (t) procesarTecla(t);

  delay(20);
}
