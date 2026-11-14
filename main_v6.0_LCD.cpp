#include <Arduino.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

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
//
//  Pantalla: LCD 16x2 por I2C (SDA=A4, SCL=A5, VCC=5V, GND=GND).
//  Si no muestra nada, cambiar LCD_DIRECCION de 0x27 a 0x3F.
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

const byte LCD_DIRECCION = 0x27;   // probar 0x3F si no responde
const byte LCD_COLUMNAS  = 16;
const byte LCD_FILAS     = 2;
LiquidCrystal_I2C lcd(LCD_DIRECCION, LCD_COLUMNAS, LCD_FILAS);

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

// Cache de las lineas del LCD (evita reescribir si no cambian)
char lcdLinea0[LCD_COLUMNAS + 1] = "";
char lcdLinea1[LCD_COLUMNAS + 1] = "";

// ------------------------------------------------------------
//  4. PANTALLA
// ------------------------------------------------------------
// Escribe una linea completa solo si el texto cambio.
void lcdEscribir(byte fila, const char* texto) {
  char buffer[LCD_COLUMNAS + 1];
  snprintf(buffer, sizeof(buffer), "%s", texto);   // recorta a 16

  byte n = strlen(buffer);
  while (n < LCD_COLUMNAS) buffer[n++] = ' ';      // rellena con espacios
  buffer[LCD_COLUMNAS] = '\0';

  char* anterior = (fila == 0) ? lcdLinea0 : lcdLinea1;
  if (strcmp(buffer, anterior) == 0) return;
  strcpy(anterior, buffer);

  lcd.setCursor(0, fila);
  lcd.print(buffer);
}

void pantalla(const char* arriba, const char* abajo) {
  lcdEscribir(0, arriba);
  lcdEscribir(1, abajo);
}

// Titulo segun la accion en curso.
const char* tituloClave() {
  if (objetivo == ARMAR)         return "ACTIVAR ALARMA";
  if (objetivo == DESARMAR)      return "DESACTIVAR";
  if (objetivo == CAMBIAR_VIEJA) return "CLAVE ACTUAL";
  return "CLAVE NUEVA";
}

// Muestra la clave como asteriscos (nunca los digitos).
void pantallaClave() {
  char texto[LCD_COLUMNAS + 1] = "Clave: ";
  byte pos = 7;
  while (pos < 7 + largo && pos < LCD_COLUMNAS) texto[pos++] = '*';
  texto[pos] = '\0';

  pantalla(estado == SONANDO ? "DETENER ALARMA" : tituloClave(), texto);
}

void pantallaMenu() {
  if (armada) pantalla("ALARMA ARMADA", "B:Desact C:Clave");
  else        pantalla("ALARMA DESARMADA", "A:Activar C:Clav");
}

void pantallaCalentando(int segundos) {
  char texto[LCD_COLUMNAS + 1];
  snprintf(texto, sizeof(texto), "Espere... %d s", segundos);
  pantalla("CALENTANDO PIR", texto);
}

void pantallaAlarma() {
  pantalla("MOVIMIENTO!", "Clave + #");
}

// Aviso breve antes de volver al menu.
void aviso(const char* texto) {
  pantalla(texto, "");
  delay(1200);
}

// ------------------------------------------------------------
//  5. ENTRADA DE LA CLAVE (siempre oculta en el display)
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
      lcdEscribir(1, "Maximo 8 digitos");
      return true;
    }
    entrada[largo++] = t;
    entrada[largo] = '\0';
  } else if (t == '*') {
    if (largo == 0) {
      lcdEscribir(1, "Nada que borrar");
      return true;
    }
    entrada[--largo] = '\0';
  } else {
    return false;
  }

  pantallaClave();
  return true;
}

// ------------------------------------------------------------
//  6. LEDS Y BUZZER
// ------------------------------------------------------------
void modoReposo() {
  digitalWrite(pinLEDVerde, HIGH);   // reposo = verde encendido
  digitalWrite(pinLEDRojo, LOW);
  digitalWrite(pinBUZZ, LOW);
}

void dispararAlarma() {
  limpiarEntrada();
  estado = SONANDO;
  digitalWrite(pinLEDVerde, LOW);
  digitalWrite(pinLEDRojo, HIGH);    // deteccion = rojo encendido
  digitalWrite(pinBUZZ, HIGH);
  pantallaAlarma();
}

void detenerAlarma() {
  armada = false;
  modoReposo();
}

// ------------------------------------------------------------
//  7. MENU Y ACCIONES
// ------------------------------------------------------------
void mostrarMenu() {
  limpiarEntrada();
  estado = MENU;
  pantallaMenu();
}

void pedirClave(Objetivo obj) {
  limpiarEntrada();
  objetivo = obj;
  estado = PEDIR_CLAVE;
  pantallaClave();
}

void iniciarCalentamiento() {
  limpiarEntrada();
  estado = CALENTANDO;
  ultimoSegundo = -1;
  tCalentamiento = millis();
  modoReposo();
  pantallaCalentando(TIEMPO_CALENTAMIENTO / 1000);
}

// ------------------------------------------------------------
//  8. CONFIRMACION CON #
// ------------------------------------------------------------
void confirmarClave() {

  // Con la alarma sonando, la clave solo sirve para detenerla
  if (estado == SONANDO) {
    if (claveCorrecta()) {
      detenerAlarma();
      aviso("ALARMA DETENIDA");
      mostrarMenu();
    } else {
      limpiarEntrada();
      lcdEscribir(1, "Clave incorrecta");
    }
    return;
  }

  // La clave nueva no se compara con la actual: solo se valida el largo
  if (objetivo == CAMBIAR_NUEVA) {
    if (largo < MIN_CLAVE) {
      lcdEscribir(1, "Minimo 4 digitos");
      return;
    }
    strcpy(clave, entrada);
    aviso("CLAVE CAMBIADA");
    mostrarMenu();
    return;
  }

  if (!claveCorrecta()) {
    aviso("CLAVE INCORRECTA");
    mostrarMenu();
    return;
  }

  if (objetivo == ARMAR) {
    iniciarCalentamiento();
  } else if (objetivo == DESARMAR) {
    detenerAlarma();
    aviso("DESACTIVADA");
    mostrarMenu();
  } else {
    limpiarEntrada();
    objetivo = CAMBIAR_NUEVA;
    pantallaClave();
  }
}

// ------------------------------------------------------------
//  9. TECLADO
// ------------------------------------------------------------
void procesarTecla(char t) {

  // ---- Escribiendo la clave (nunca se muestra el digito) ----
  if (estado == PEDIR_CLAVE || estado == SONANDO) {
    if (editarClave(t)) return;

    if (t == '#') {
      confirmarClave();
      return;
    }

    if (estado == SONANDO) {
      pantalla("DETENER ALARMA", "Solo clave + #");
    } else {
      aviso("CANCELADO");
      mostrarMenu();
    }
    return;
  }

  // ---- Calentando el PIR ----
  if (estado == CALENTANDO) return;

  // ---- MENU ----
  if (t == 'A') {
    if (armada) {
      aviso("YA ESTA ARMADA");
      mostrarMenu();
    } else {
      pedirClave(ARMAR);
    }
  } else if (t == 'B') {
    if (!armada) {
      aviso("YA DESACTIVADA");
      mostrarMenu();
    } else {
      pedirClave(DESARMAR);
    }
  } else if (t == 'C') {
    pedirClave(CAMBIAR_VIEJA);
  } else {
    aviso("OPCION INVALIDA");
    mostrarMenu();
  }
}

// ------------------------------------------------------------
//  10. SETUP Y LOOP
// ------------------------------------------------------------
void setup() {
  pinMode(pinLEDVerde, OUTPUT);
  pinMode(pinLEDRojo, OUTPUT);
  pinMode(pinBUZZ, OUTPUT);
  pinMode(pinPIR, INPUT);
  modoReposo();

  lcd.init();
  lcd.backlight();
  lcd.clear();

  mostrarMenu();
}

void loop() {
  // ---- Calentamiento del PIR (no bloquea el teclado) ----
  if (estado == CALENTANDO) {
    unsigned long t = millis() - tCalentamiento;
    if (t >= TIEMPO_CALENTAMIENTO) {
      armada = true;
      mostrarMenu();
    } else {
      int s = (TIEMPO_CALENTAMIENTO - t + 999) / 1000;
      if (s != ultimoSegundo) {
        ultimoSegundo = s;
        pantallaCalentando(s);
      }
    }
  }

  // ---- Buzzer intermitente mientras la alarma esta activa ----
  if (estado == SONANDO) {
    digitalWrite(pinBUZZ, (millis() / TIEMPO_BIP) % 2 ? HIGH : LOW);
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
