#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include "esp_system.h"

// Serwer na porcie 80
WebServer server(80);

// Preferences do logów które zapisują sie po resecie esp by ustalić problem resetu
Preferences prefs;

// Serwo konfiguracja, przypisanie pinu serwa i ustawienie pozycji startowej (90 oznacza pozycje kół na wprost)
Servo servo;
const int SERVO_PIN = 32;
int currentServoPos = 90;
int servoDirection = 0;
unsigned long lastServoMove = 0;
const int SERVO_STEP = 1;
const int SERVO_DELAY = 2;

// Piny sterujące silnikami (IN1 i IN2 ustalają kierunek obrotu silnika, PWM na ten pin wysyłany jest sygnał odpowiedzialny za prędkość silnika)
const int AIN1 = 26;
const int AIN2 = 25;
const int PWMA = 33;
const int BIN1 = 27;
const int BIN2 = 14;
const int PWMB = 13;

// Kanały PWM (kanał generuje sygnał i potem jest przypisany do konkretnego pinu)
const int pwmA = 2;
const int pwmB = 3;

// Zmienne odpowiedzialne za prędkość samochodu oraz kierunek jazdy
int currentSpeed = 0;
int targetSpeed = 0;
int motorDirection = 0;

// Maksymalna wartość PWM
const int MAX_SPEED = 220;

// Deklaracje funkcji
void handleRoot();
void forward(int speed);
void backward(int speed);
void servoLeft();
void servoRight();
void stopMotors();
void saveResetReason();
String resetReasonToText(int reason);
void handleResetLog();

void setup()
{
  // Uruchomienie portu szeregowego. Pozwala na wypisanie logów w monitorze po podpięciu esp
  Serial.begin(115200);
  delay(300);

  // Zapisanie przyczyny ostatniego resetu do pamięci
  saveResetReason();

  prefs.begin("logs", true);
  int count = prefs.getInt("count", 0);

  Serial.println("=== LOG RESETOW ===");

  for (int i = 0; i < count; i++)
  {
    int r = prefs.getInt(("r" + String(i)).c_str(), -1);
    Serial.print(i);
    Serial.print(": ");
    Serial.println(resetReasonToText(r));
  }

  prefs.end();

  // Ustawienie pinów kierunku silnika jako output
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  // Ustawienie pinów silnika na LOW po starcie w razie przypadkowego ruszenia silników
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, LOW);

  // Konfiguracja kanału PWM silników, 20000 oznacza częstotliwość 20kHz, 8 oznacza 8 bitów czyli wartość 0 do 255
  ledcSetup(pwmA, 20000, 8);
  // Przypisanie kanału PWM do pinu PWM
  ledcAttachPin(PWMA, pwmA);

  ledcSetup(pwmB, 20000, 8);
  ledcAttachPin(PWMB, pwmB);

  // Po starcie programu zatrzymuje silniki dla dodatkowego zabezpieczenia
  stopMotors();

  // Tworzenie sieci WiFi
  WiFi.mode(WIFI_AP);
  bool apOK = WiFi.softAP("ESP32_złomek", "12345678");

  Serial.print("AP status: ");
  Serial.println(apOK ? "OK" : "FAIL");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Ustawienie serwa na 50Hz
  servo.setPeriodHertz(50);
  // Przypisanie serwa do pinu, 500 i 2400 określa zakres ruchu serwa
  servo.attach(SERVO_PIN, 500, 2400);
  // Po starcie ustawia serwo w pozycji 90 czyli koła prosto
  servo.write(currentServoPos);

  // Handler do frontu Index
  server.on("/", handleRoot);
  // Handler od strony logów
  server.on("/resetlog", handleResetLog);

  // Endpointy odpowiedzialne za sterowanie
  server.on("/forward", []()
            {
              //Start jazdy do przodu z maksymalną ustawioną prędkością
              //Prędkość będzie zwiększana płynnie przez funkcje updateSpeed()
              forward(MAX_SPEED);
              server.send(200, "text/plain", "OK"); });

  server.on("/back", []()
            {
              backward(MAX_SPEED);
              server.send(200, "text/plain", "OK"); });

  // Endpoint do automatycznego zatrzymania. Wywoływany jest po puszczeniu przycisku
  server.on("/stop", []()
            {
              stopMotors();
              server.send(200, "text/plain", "OK"); });

  // Endpointy od serwa
  server.on("/servoRight", []()
            {
              servoRight();
              server.send(200, "text/plain", "OK"); });

  server.on("/servoLeft", []()
            {
              servoLeft();
              server.send(200, "text/plain", "OK"); });

  server.on("/stopServo", []()
            {
              servoDirection = 0;
              server.send(200, "text/plain", "OK"); });

  server.on("/servoCenter", []()
            {
              currentServoPos = 90;
              servo.write(currentServoPos);
              server.send(200, "text/plain", "OK"); });

  // Uruchomienie serwera
  server.begin();
}

// Funkcja zapisuje przyczyne resetu ESP
void saveResetReason()
{
  // Otwarcie logs w pamięci preferences
  prefs.begin("logs", false);

  // Odczytanie liczby zapisanych resetów
  int count = prefs.getInt("count", 0);
  // Pobranie przyczyny ostatniego resetu ESP. esp_reset_reason zwraca numer typu resetu
  int reason = (int)esp_reset_reason();

  // Zapisanie tylko 100 pierwszych logów by nie robić śmietnika
  if (count >= 100)
  {
    count = 0;
    prefs.clear();
  }
  // Zmienia format błędu - numer na tekst
  prefs.putInt(("r" + String(count)).c_str(), reason);
  prefs.putInt("count", count + 1);

  // Zamknięcie dostępu do prefs
  prefs.end();

  // Wypisanie informacji o zapisanym resecie w monitorze portu szeregowego
  Serial.print("Zapisano reset: ");
  Serial.println(resetReasonToText(reason));
}

// Funkcja formatuje numer resetu na tekst
String resetReasonToText(int reason)
{
  switch (reason)
  {
  // Podpięcie zasilania
  case ESP_RST_POWERON:
    return "POWERON";
  // Reset przyciskiem EN/RST
  case ESP_RST_EXT:
    return "EXTERNAL RESET";
  // Reset wykonany programowo
  case ESP_RST_SW:
    return "SOFTWARE RESET";
  // Reset po błędzie programu
  case ESP_RST_PANIC:
    return "CRASH";
  // Reset po zbyt długiej obsłudze przerwania
  case ESP_RST_INT_WDT:
    return "INTERRUPT WATCHDOG";
  // Reset przez watchdog zadania
  case ESP_RST_TASK_WDT:
    return "TASK WATCHDOG";
  // Inny reset watchdog
  case ESP_RST_WDT:
    return "OTHER WATCHDOG";
  // Wybudzenie
  case ESP_RST_DEEPSLEEP:
    return "DEEPSLEEP";
  // Reset po spadku napięcia
  case ESP_RST_BROWNOUT:
    return "BROWNOUT - spadek napiecia";
  // Reset SDIO
  case ESP_RST_SDIO:
    return "SDIO";
  // Obsługa nieznanych resetów
  default:
    return "UNKNOWN: " + String(reason);
  }
}

// Podstrona /resetlog
void handleResetLog()
{
  // Otwarcie pamięci prefs
  prefs.begin("logs", true);

  // Odczytanie liczby resetów
  int count = prefs.getInt("count", 0);
  // Przygotowanie tekstu do wysłania do przeglądarki
  String text = "Liczba zapisanych resetow: " + String(count) + "\n\n";

  // Iterowanie po wszystkich wpisach
  for (int i = 0; i < count; i++)
  {
    int r = prefs.getInt(("r" + String(i)).c_str(), -1);
    text += String(i) + ": " + resetReasonToText(r) + "\n";
  }

  prefs.end();

  server.send(200, "text/plain", text);
}

// Funkcja odpowiedzialna za płynną zmiane prędkości
// Wywoływana jest w loop
void updateSpeed()
{
  // Jeśli aktualna prędkość jest mniejsza od docelowej to zwiększ o 1
  if (currentSpeed < targetSpeed)
  {
    currentSpeed += 1;
  }

  // Jeśli aktualna prędkość jest większa od docelowej to zmniejsz o 4
  if (currentSpeed > targetSpeed)
  {
    currentSpeed -= 4;
  }

  // Zabezpieczenie przed wartościami ujemnymi
  if (currentSpeed < 0)
    currentSpeed = 0;
  // Zabezpieczenie przed wpisaniem zbyt dużej wartości.
  if (currentSpeed > MAX_SPEED)
    currentSpeed = MAX_SPEED;

  // Wysłanie wartości PWM na silniki
  ledcWrite(pwmA, currentSpeed);
  ledcWrite(pwmB, currentSpeed);

  // Jeśli aktualna i docelowa prędkość są równe 0 to auto jest zatrzymane.
  if (currentSpeed == 0 && targetSpeed == 0)
  {
    // Ustawienie kierunku jazdy na 0 czyli informacja brak jazdy
    motorDirection = 0;
    // Wyłączenie pinów kierunku silników
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, LOW);
  }
}

// Płynny ruch serwa i aktualizowanie pozycji
void servoLeft()
{
  servoDirection = 1;
}

void servoRight()
{
  servoDirection = -1;
}

void updateServo()
{
  if (millis() - lastServoMove < SERVO_DELAY)
  {
    return;
  }

  lastServoMove = millis();

  if (servoDirection == 1)
  {
    currentServoPos += SERVO_STEP;
  }

  if (servoDirection == -1)
  {
    currentServoPos -= SERVO_STEP;
  }

  if (currentServoPos < 25)
  {
    currentServoPos = 25;
    servoDirection = 0;
  }

  if (currentServoPos > 155)
  {
    currentServoPos = 155;
    servoDirection = 0;
  }

  servo.write(currentServoPos);
}

void loop()
{
  // Obsługa klientów serwera. Sprawdza czy przycisk na stronie został wciśnięty
  server.handleClient();
  // Płynna aktualizacja prędkości silników oraz serwa
  updateSpeed();
  updateServo();
  // Minimalne opóźnienie by program nie wykonywał pętli zbyt szybko
  delay(5);
}

// Funkcja jazdy do przodu. Parametr speed określa wartość do której silniki mają płynnie dojść
void forward(int speed)
{
  // Zabezpieczenie silników i sterownika przed nagłą zmianą kierunku
  if (motorDirection == -1)
  {
    // Jeśli silnik jechał do tyłu to ustawia wymaganą prędkość na 0 i płynnie hamuje do czasu zatrzymania po czym dopiero rusza w przeciwnym kierunku
    targetSpeed = 0;
    while (currentSpeed > 0)
    {
      updateSpeed();
      delay(5);
    }
    delay(300);
  }

  // Ustawienie kierunku silników do przodu
  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, HIGH);
  digitalWrite(BIN2, LOW);

  // Przypisanie do zmiennej globalnej, że auto jedzie do przodu
  motorDirection = 1;
  targetSpeed = speed;
}

void backward(int speed)
{
  if (motorDirection == 1)
  {
    targetSpeed = 0;
    while (currentSpeed > 0)
    {
      updateSpeed();
      delay(5);
    }
    delay(300);
  }

  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, HIGH);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, HIGH);

  motorDirection = -1;
  targetSpeed = speed;
}

// Funkcja zatrzymania silników
// Target speed ustawiany jest na 0 przez co updateSpeed() w loop wie, że ma robić płynne hamowanie
void stopMotors()
{
  targetSpeed = 0;
}

// HTML
void handleRoot()
{
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>

body {
  text-align:center;
  user-select:none;
}

#joyBox {
  width: 180px;
  height: 180px;
  border: 3px solid black;
  border-radius: 50%;
  margin: 15px auto;
  position: relative;
  background: #ddd;
  touch-action: none;
}

#stick {
  width: 60px;
  height: 60px;
  background: #777;
  border-radius: 50%;
  position: absolute;
  left: 60px;
  top: 60px;
}

button {
  width: 100px;
  height: 45px;
  font-size: 16px;
  margin: 4px;
}

</style>
</head>

<body>

<div id="joyBox"
ontouchstart="startJoy(event)"
ontouchmove="moveJoy(event)"
ontouchend="stopJoy()"
onmousedown="startMouse(event)"
onmousemove="moveMouse(event)"
onmouseup="stopJoy()"
onmouseleave="stopJoy()">

<div id="stick"></div>

</div>

<button onclick="fetch('/stop');fetch('/stopServo');resetStick();">
STOP
</button>

<button onclick="fetch('/servoCenter');resetStick();">
CENTER
</button>

<br><br>

<a href="/resetlog">Pokaż logi resetów</a>

<script>

let joyActive=false;

let lastMotor="";
let lastServo="";

const joyBox=document.getElementById("joyBox");
const stick=document.getElementById("stick");

function sendMotor(cmd){
    if(cmd!=lastMotor){
        fetch(cmd);
        lastMotor=cmd;
    }
}

function sendServo(cmd){
    if(cmd!=lastServo){
        fetch(cmd);
        lastServo=cmd;
    }
}

function handleJoy(x,y){

    let rect=joyBox.getBoundingClientRect();

    let centerX=rect.left+rect.width/2;
    let centerY=rect.top+rect.height/2;

    let dx=x-centerX;
    let dy=y-centerY;

    let max=60;

    if(dx>max) dx=max;
    if(dx<-max) dx=-max;

    if(dy>max) dy=max;
    if(dy<-max) dy=-max;

    stick.style.left=(60+dx)+"px";
    stick.style.top=(60+dy)+"px";

    if(dy<-25){
        sendMotor('/forward');
    }
    else if(dy>25){
        sendMotor('/back');
    }
    else{
        sendMotor('/stop');
    }

    if(dx<-20){
        sendServo('/servoLeft');
    }
    else if(dx>20){
        sendServo('/servoRight');
    }
    else{
        sendServo('/stopServo');
    }
}

function startJoy(event){
    joyActive=true;
    event.preventDefault();

    let touch=event.touches[0];
    handleJoy(touch.clientX,touch.clientY);
}

function moveJoy(event){
    if(!joyActive) return;
    event.preventDefault();

    let touch=event.touches[0];
    handleJoy(touch.clientX,touch.clientY);
}

function startMouse(event){
    joyActive=true;
    handleJoy(event.clientX,event.clientY);
}

function moveMouse(event){
    if(!joyActive) return;
    handleJoy(event.clientX,event.clientY);
}

function stopJoy(){
    joyActive=false;

    fetch('/stop');
    fetch('/stopServo');

    lastMotor="";
    lastServo="";

    resetStick();
}

function resetStick(){
    stick.style.left="60px";
    stick.style.top="60px";
}

</script>


</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}
