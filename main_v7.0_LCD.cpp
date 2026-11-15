#include <Arduino.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================
//  ALARMA PIR 501 + TECLADO 4x4 + LCD 16x2 I2C
//  Reposo: LED verde | Deteccion: LED rojo + buzzer intermitente
//  A activar | B desactivar | C cambiar clave | * borrar | # enter
//  LCD: SDA=A4, SCL=A5. Si no muestra nada, probar 0x3F.
// ============================================================

// ---------- 1. HARDWARE ----------
const int pinLEDVerde = 13, pinLEDRojo = 7, pinBUZZ = 8, pinPIR = 4;

char teclas[4][4] = {{'1', '2', '3', 'A'}, {'4', '5', '6', 'B'},
                     {'7', '8', '9', 'C'}, {'*', '0', '#', 'D'}};
byte pinesFilas[4] = {9, 10, 11, 12}, pinesColumnas[4] = {2, 3, 5, 6};

Keypad teclado(makeKeymap(teclas), pinesFilas, pinesColumnas, 4, 4);

// ---------- 2. CONFIGURACION ----------
const unsigned long CALENTAMIENTO = 10000UL;   // estabilizacion del PIR
const unsigned long BIP = 200UL;               // intermitencia del buzzer
const byte MIN_CLAVE = 4, MAX_CLAVE = 8;
LiquidCrystal_I2C lcd(0x27, 16, 2);            // 0x3F si no responde

// ---------- 3. ESTADO ----------
enum Estado { MENU, PEDIR_CLAVE, CALENTANDO, SONANDO };
enum Objetivo { ARMAR, DESARMAR, CAMBIAR_VIEJA, CAMBIAR_NUEVA };

Estado estado = MENU;
Objetivo objetivo = ARMAR;
bool armada = false;

char clave[MAX_CLAVE + 1] = "1234";   // nunca se muestra
char entrada[MAX_CLAVE + 1] = "";
byte largo = 0;

unsigned long tCalentamiento = 0;
int ultimoSegundo = -1;

char lcdLinea[2][17] = {"", ""};      // cache de las 2 lineas

// ---------- 4. PANTALLA ----------
void lcdEscribir(byte fila, const char* texto) {
  char buffer[17];
  snprintf(buffer, sizeof(buffer), "%s", texto);   // recorta a 16
  byte n = strlen(buffer);
  while (n < 16) buffer[n++] = ' ';
  buffer[16] = '\0';

  if (!strcmp(buffer, lcdLinea[fila])) return;     // no cambio: no reescribe
  strcpy(lcdLinea[fila], buffer);
  lcd.setCursor(0, fila);
  lcd.print(buffer);
}

void pantalla(const char* arriba, const char* abajo) {
  lcdEscribir(0, arriba);
  lcdEscribir(1, abajo);
}

void pantallaClave() {
  char texto[17] = "Clave: ";
  byte pos = 7;
  while (pos < 7 + largo) texto[pos++] = '*';
  texto[pos] = '\0';

  const char* titulo =
      estado == SONANDO         ? "DETENER ALARMA" :
      objetivo == ARMAR         ? "ACTIVAR ALARMA" :
      objetivo == DESARMAR      ? "DESACTIVAR"     :
      objetivo == CAMBIAR_VIEJA ? "CLAVE ACTUAL"   : "CLAVE NUEVA";
  pantalla(titulo, texto);
}

void pantallaCalentando(int s) {
  char texto[17];
  snprintf(texto, sizeof(texto), "Espere... %d s", s);
  pantalla("CALENTANDO PIR", texto);
}

// Aviso breve antes de volver al menu.
void aviso(const char* texto) {
  pantalla(texto, "");
  delay(1200);
}

// ---------- 5. CLAVE ----------
void limpiarEntrada() { entrada[0] = '\0'; largo = 0; }
bool claveCorrecta() { return !strcmp(entrada, clave); }

bool editarClave(char t) {
  if (t >= '0' && t <= '9') {
    if (largo >= MAX_CLAVE) { lcdEscribir(1, "Maximo 8 digitos"); return true; }
    entrada[largo++] = t;
    entrada[largo] = '\0';
  } else if (t == '*') {
    if (!largo) { lcdEscribir(1, "Nada que borrar"); return true; }
    entrada[--largo] = '\0';
  } else return false;

  pantallaClave();
  return true;
}

// ---------- 6. LEDS Y BUZZER ----------
void modoReposo() {
  digitalWrite(pinLEDVerde, HIGH);     // reposo = verde
  digitalWrite(pinLEDRojo, LOW);
  digitalWrite(pinBUZZ, LOW);
}

void dispararAlarma() {
  limpiarEntrada();
  estado = SONANDO;
  digitalWrite(pinLEDVerde, LOW);
  digitalWrite(pinLEDRojo, HIGH);      // deteccion = rojo
  digitalWrite(pinBUZZ, HIGH);
  pantalla("MOVIMIENTO!", "Clave + #");
}

void detenerAlarma() { armada = false; modoReposo(); }

// ---------- 7. MENU Y ACCIONES ----------
void mostrarMenu() {
  limpiarEntrada();
  estado = MENU;
  if (armada) pantalla("ALARMA ARMADA", "B:Desact C:Clave");
  else        pantalla("ALARMA DESARMADA", "A:Activar C:Clav");
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
  pantallaCalentando(CALENTAMIENTO / 1000);
}

// ---------- 8. CONFIRMACION CON # ----------
void confirmarClave() {
  if (estado == SONANDO) {                   // la clave solo detiene la alarma
    if (!claveCorrecta()) { limpiarEntrada(); lcdEscribir(1, "Clave incorrecta"); return; }
    detenerAlarma();
    aviso("ALARMA DETENIDA");
  } else if (objetivo == CAMBIAR_NUEVA) {    // solo se valida el largo
    if (largo < MIN_CLAVE) { lcdEscribir(1, "Minimo 4 digitos"); return; }
    strcpy(clave, entrada);
    aviso("CLAVE CAMBIADA");
  } else if (!claveCorrecta()) {
    aviso("CLAVE INCORRECTA");
  } else if (objetivo == ARMAR) {
    iniciarCalentamiento();
    return;
  } else if (objetivo == DESARMAR) {
    detenerAlarma();
    aviso("DESACTIVADA");
  } else {                                   // clave vieja correcta: pedir la nueva
    limpiarEntrada();
    objetivo = CAMBIAR_NUEVA;
    pantallaClave();
    return;
  }
  mostrarMenu();
}

// ---------- 9. TECLADO ----------
void procesarTecla(char t) {
  if (estado == PEDIR_CLAVE || estado == SONANDO) {
    if (editarClave(t)) return;
    if (t == '#') { confirmarClave(); return; }
    if (estado == SONANDO) pantalla("DETENER ALARMA", "Solo clave + #");
    else { aviso("CANCELADO"); mostrarMenu(); }
    return;
  }

  if (estado == CALENTANDO) return;

  if (t == 'A') {
    if (armada) { aviso("YA ESTA ARMADA"); mostrarMenu(); }
    else pedirClave(ARMAR);
  } else if (t == 'B') {
    if (!armada) { aviso("YA DESACTIVADA"); mostrarMenu(); }
    else pedirClave(DESARMAR);
  } else if (t == 'C') pedirClave(CAMBIAR_VIEJA);
  else { aviso("OPCION INVALIDA"); mostrarMenu(); }
}

// ---------- 10. SETUP Y LOOP ----------
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
  if (estado == CALENTANDO) {                    // cuenta regresiva
    unsigned long t = millis() - tCalentamiento;
    if (t >= CALENTAMIENTO) { armada = true; mostrarMenu(); }
    else {
      int s = (CALENTAMIENTO - t + 999) / 1000;
      if (s != ultimoSegundo) { ultimoSegundo = s; pantallaCalentando(s); }
    }
  }

  if (estado == SONANDO)                         // buzzer intermitente
    digitalWrite(pinBUZZ, (millis() / BIP) % 2 ? HIGH : LOW);

  if (estado == MENU && armada && digitalRead(pinPIR) == HIGH) dispararAlarma();

  char t = teclado.getKey();
  if (t) procesarTecla(t);
  delay(20);
}
