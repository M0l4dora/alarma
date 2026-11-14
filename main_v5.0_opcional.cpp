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
//  Extra: al detectar movimiento el buzzer toca un patron ritmico
//  estilo cumbia (melodia original, editable en MELODIA/DURACIONES).
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
const byte MIN_CLAVE = 4;                            // largo minimo de la clave
const byte MAX_CLAVE = 8;                            // largo maximo de la clave

// Ritmo de alerta estilo cumbia (0 = silencio).
// Notas: G4=392  A4=440  B4=494  C5=523  D5=587  E5=659
const int MELODIA[] = {
  392, 440, 494, 523, 494, 440,
  392, 330, 392, 440, 392, 330
};
const unsigned int DURACIONES[] = {
  180, 180, 180, 300, 180, 180,
  300, 180, 180, 180, 300, 400
};
const byte PASOS = sizeof(MELODIA) / sizeof(MELODIA[0]);

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
int ultimoSegundo = -1;

// Reproduccion del ritmo de alerta
byte pasoMelodia = 0;
unsigned long tPaso = 0;
unsigned int duracionPaso = 0;

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

  Serial.print("   Clave: ");
  for (byte i = 0; i < largo; i++) Serial.print('*');
  Serial.println();
  return true;
}

// ------------------------------------------------------------
//  5. LEDS Y BUZZER
// ------------------------------------------------------------
void modoReposo() {
  digitalWrite(pinLEDVerde, HIGH);   // reposo = verde encendido
  digitalWrite(pinLEDRojo, LOW);
  noTone(pinBUZZ);
  digitalWrite(pinBUZZ, LOW);
}

// Arranca el ritmo desde el primer paso
void iniciarMelodia() {
  pasoMelodia = 0;
  tPaso = millis();
  duracionPaso = DURACIONES[0];
  if (MELODIA[0] > 0) tone(pinBUZZ, MELODIA[0]);
  else noTone(pinBUZZ);
}

// Avanza la melodia sin bloquear el programa
void actualizarMelodia() {
  if (millis() - tPaso < duracionPaso) return;

  tPaso = millis();
  pasoMelodia = (pasoMelodia + 1) % PASOS;
  duracionPaso = DURACIONES[pasoMelodia];

  if (MELODIA[pasoMelodia] > 0) tone(pinBUZZ, MELODIA[pasoMelodia]);
  else noTone(pinBUZZ);
}

void dispararAlarma() {
  limpiarEntrada();
  estado = SONANDO;
  digitalWrite(pinLEDVerde, LOW);
  digitalWrite(pinLEDRojo, HIGH);    // deteccion = rojo encendido
  iniciarMelodia();

  Serial.println("\n!!! MOVIMIENTO DETECTADO !!!");
  Serial.println("ALARMA ACTIVA: LED rojo + melodia de alerta.");
  Serial.println("Ingrese la contrasena y # para detenerla.");
}

void detenerAlarma() {
  armada = false;
  modoReposo();
}

// ------------------------------------------------------------
//  6. MENU Y ACCIONES
// ------------------------------------------------------------
void mostrarMenu() {
  limpiarEntrada();
  estado = MENU;
  Serial.println();
  Serial.println("============ MENU ============");
  Serial.print("Sistema: ");
  Serial.println(armada ? "ARMADO (vigilando)" : "DESARMADO");
  Serial.println("A activar | B desactivar | C cambiar clave");
  Serial.println("* borrar | # enter");
  Serial.println("==============================");
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

  // Con la alarma sonando, la clave solo sirve para detenerla
  if (estado == SONANDO) {
    if (claveCorrecta()) {
      detenerAlarma();
      Serial.println("\nContrasena correcta. Alarma detenida.");
      mostrarMenu();
    } else {
      limpiarEntrada();
      Serial.println("\nContrasena INCORRECTA. La alarma sigue sonando.");
    }
    return;
  }

  // La clave nueva no se compara con la actual: solo se valida el largo
  if (objetivo == CAMBIAR_NUEVA) {
    if (largo < MIN_CLAVE) {
      Serial.println("\nMinimo 4 digitos. Siga escribiendo.");
      return;
    }
    strcpy(clave, entrada);
    Serial.println("\n>>> CONTRASENA CAMBIADA <<<");
    mostrarMenu();
    return;
  }

  if (!claveCorrecta()) {
    Serial.println("\nContrasena INCORRECTA.");
    mostrarMenu();
    return;
  }

  if (objetivo == ARMAR) {
    iniciarCalentamiento();
  } else if (objetivo == DESARMAR) {
    detenerAlarma();
    Serial.println("\n>>> ALARMA DESACTIVADA <<<");
    mostrarMenu();
  } else {
    limpiarEntrada();
    objetivo = CAMBIAR_NUEVA;
    Serial.println("\nClave actual correcta. Ingrese la NUEVA (4 a 8 digitos) y #:");
  }
}

// ------------------------------------------------------------
//  8. TECLADO
// ------------------------------------------------------------
void procesarTecla(char t) {

  // ---- Escribiendo la clave (nunca se muestra el digito) ----
  if (estado == PEDIR_CLAVE || estado == SONANDO) {
    if (editarClave(t)) return;

    Serial.print("\nTecla: ");
    Serial.print(t);

    if (t == '#') {
      confirmarClave();
      return;
    }

    if (estado == SONANDO) {
      Serial.println("\nIngrese la contrasena para detener la alarma.");
    } else {
      Serial.println("\nOperacion cancelada.");
      mostrarMenu();
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

  if (t == 'A') {
    if (armada) {
      Serial.println("\nLa alarma ya esta armada.");
    } else {
      pedirClave(ARMAR, "--- ACTIVAR ALARMA ---");
    }
  } else if (t == 'B') {
    if (!armada) {
      Serial.println("\nLa alarma ya esta desactivada.");
    } else {
      pedirClave(DESARMAR, "--- DESACTIVAR ALARMA ---");
    }
  } else if (t == 'C') {
    pedirClave(CAMBIAR_VIEJA, "--- CAMBIAR CONTRASENA ---");
  } else {
    Serial.println("\nOpcion invalida. Use A, B o C.");
    mostrarMenu();
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
  mostrarMenu();
}

void loop() {
  // ---- Calentamiento del PIR (no bloquea el teclado) ----
  if (estado == CALENTANDO) {
    unsigned long t = millis() - tCalentamiento;
    if (t >= TIEMPO_CALENTAMIENTO) {
      armada = true;
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

  // ---- Ritmo de alerta mientras la alarma esta activa ----
  if (estado == SONANDO) {
    actualizarMelodia();
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
