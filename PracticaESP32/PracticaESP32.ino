#include <WiFi.h>
#include <EEPROM.h>

const char* ssid = "DESKTOP-RTTQ50L 0411";
const char* password = "574;Cm23";

WiFiServer server(80);
String header;

const int pinAnalogico = 34;  // Pin analógico del sensor de gases
const int buzzerPin = 0;      // Pin del zumbador
const int touchPin = T0;

#define NUM_READINGS 20  // Cantidad de puntos a mostrar
#define EEPROM_SIZE 512
#define CALIBRATION_ADDR 0

int readings[NUM_READINGS];
int readIndex = 0;
bool bufferFull = false;
int sensorValue;
int calibratedValue = 0;
bool alertActive = false;
bool clientConnected = true;
unsigned long lastClientActivity = 0;
const long clientTimeout = 10000;

unsigned long currentTime = 0;
unsigned long previousTime = 0;
unsigned long lastBuzzerBeep = 0;
const long timeoutTime = 5000;
int buzzerInterval = 2000;               // Intervalo inicial del zumbador
bool forceAwake = true;

void setup() {
  Serial.begin(115200);

  // Inicializar EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Cargar valor calibrado de la EEPROM
  calibratedValue = EEPROM.readInt(CALIBRATION_ADDR);
  Serial.print("Valor calibrado cargado: ");
  Serial.println(calibratedValue);

  // Configurar timer wakeup para 5 segundos
  esp_sleep_enable_timer_wakeup(5 * 1000000);

  // Configurar para despertar con sensor touch
  esp_sleep_enable_touchpad_wakeup();

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(pinAnalogico, INPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // Inicializar el array de lecturas
  for(int i = 0; i < NUM_READINGS; i++) {
    readings[i] = 0;
  }

  Serial.print("Conectando a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  server.begin();
}

void calibrarSensor() {
  // Leer el valor actual del sensor
  int currentValue = analogRead(pinAnalogico);
  
  // Guardar en EEPROM
  EEPROM.writeInt(CALIBRATION_ADDR, currentValue);
  EEPROM.commit();
  
  calibratedValue = currentValue;
  
  Serial.print("Sensor calibrado. Valor: ");
  Serial.println(calibratedValue);
}

void actualizarZumbador() {
  if (calibratedValue == 0) return; // No calibrado aún
  
  // Como es inversamente proporcional:
  // - Valor BAJO = más gas (peligro)
  // - Valor ALTO = menos gas (seguro)
  
  int difference = sensorValue - calibratedValue; // Invertir la diferencia
  int safeThreshold = calibratedValue * 0.2; // 20% del valor calibrado
  
  if (sensorValue <= calibratedValue) {
    // Alerta máxima - valor por DEBAJO del calibrado = mucho gas
    digitalWrite(buzzerPin, HIGH);
    buzzerInterval = 100;
  } 
  else if (sensorValue <= (calibratedValue + safeThreshold)) {
    // Muy cerca del límite - pitido muy rápido
    buzzerInterval = 200;
  }
  else if (sensorValue <= (calibratedValue * 1.5)) {
    // Aproximándose al límite - pitido medio
    buzzerInterval = 500;
  }
  else if (sensorValue <= (calibratedValue * 2.0)) {
    // Detectando algo de gas - pitido lento
    buzzerInterval = 1000;
  }
  else {
    // Normal - valor muy por encima = seguro, sin pitido
    digitalWrite(buzzerPin, LOW);
    return;
  }
  
  // Control del pitido basado en intervalo
  if (millis() - lastBuzzerBeep >= buzzerInterval) {
    digitalWrite(buzzerPin, !digitalRead(buzzerPin));
    lastBuzzerBeep = millis();
  }
}

void enterLightSleep() {
  if (!clientConnected && !forceAwake) {
    Serial.println("Durmiendo 5 segundos...");
    digitalWrite(buzzerPin, LOW);
    esp_light_sleep_start();
    Serial.println("Despierto - Enviando datos...");
  }
}

void lecturaSensor() {
  sensorValue = analogRead(pinAnalogico);
  
  // Verificar alerta
  if (sensorValue < calibratedValue && calibratedValue > 0) {
    alertActive = true;
    forceAwake = true;  // No dormir mientras hay alarma
    Serial.println("ALARMA ACTIVA - Modo vigilancia continua");
  } else {
    alertActive = false;
    forceAwake = false; // Permitir dormir cuando no hay alarma
  }
  
  // Bufer circular
  readings[readIndex] = sensorValue;
  readIndex++;
  if (readIndex >= NUM_READINGS) {
    readIndex = 0;
    bufferFull = true;
  }
  
  // Actualizar zumbador
  actualizarZumbador();
  
  Serial.print("Valor sensor: ");
  Serial.print(sensorValue);
  Serial.print(" | Calibrado: ");
  Serial.print(calibratedValue);
  Serial.print(" | Alerta: ");
  Serial.println(alertActive ? "ACTIVA" : "inactiva");
}

void loop() {
  WiFiClient client = server.available();

  if (client) {
    clientConnected = true;
    lastClientActivity = millis();
    Serial.println("Nuevo cliente conectado.");

    currentTime = millis();
    previousTime = currentTime;
    String currentLine = "";
    header = "";

    while (client.connected() && currentTime - previousTime <= timeoutTime) {
      currentTime = millis();
      if (client.available()) {
        char c = client.read();
        header += c;

        if (c == '\n') {
          if (currentLine.length() == 0) {

            // === ENDPOINT /calibrate === Calibrar sensor ===
            if (header.indexOf("GET /calibrate") >= 0) {
              calibrarSensor();
              
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: application/json");
              client.println("Connection: close");
              client.println("Access-Control-Allow-Origin: *");
              client.println();
              client.println("{\"status\":\"success\",\"calibrated_value\":" + String(calibratedValue) + "}");
              lastClientActivity = millis();  // Actualizar actividad

              break;
            }

            // === ENDPOINT /data === Obtener datos del sensor ===
            else if (header.indexOf("GET /data") >= 0) {
              // Crear JSON con las lecturas
              String json = "{";
              json += "\"values\":[";
              
              int count = bufferFull ? NUM_READINGS : readIndex;
              int startIndex = bufferFull ? readIndex : 0;
              
              for (int i = 0; i < count; i++) {
                int index = (startIndex + i) % NUM_READINGS;
                json += String(readings[index]);
                if (i < count - 1) json += ",";
              }
              json += "],";
              json += "\"current_value\":" + String(sensorValue) + ",";
              json += "\"calibrated_value\":" + String(calibratedValue) + ",";
              json += "\"alert_active\":" + String(alertActive ? "true" : "false") + ",";
              json += "\"buzzer_interval\":" + String(buzzerInterval);
              json += "}";

              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: application/json");
              client.println("Connection: close");
              client.println("Access-Control-Allow-Origin: *");
              client.println();
              client.println(json);
              lastClientActivity = millis();  // Actualizar actividad
              
              break;
            }

            // === ENDPOINT raíz / === Página web ===
            else if (header.indexOf("GET / ") >= 0 || header.indexOf("GET / ") >= 0 || header.indexOf("GET /HTTP") >= 0) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();

              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta name='viewport' content='width=device-width, initial-scale=1'>");
              client.println("<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>");
              client.println("<style>");
              client.println("body { ");
              client.println("  font-family: Arial; ");
              client.println("  text-align: center; ");
              client.println("  margin: 0; ");
              client.println("  padding: 20px; ");
              client.println("  min-height: 100vh; ");
              client.println("  display: flex; ");
              client.println("  flex-direction: column; ");
              client.println("  align-items: center; ");
              client.println("  justify-content: center; ");
              client.println("  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);");
              client.println("  color: white;");
              client.println("}");
              client.println(".container { ");
              client.println("  max-width: 900px; ");
              client.println("  width: 100%; ");
              client.println("  display: flex; ");
              client.println("  flex-direction: column; ");
              client.println("  align-items: center; ");
              client.println("  background: rgba(255,255,255,0.1);");
              client.println("  backdrop-filter: blur(10px);");
              client.println("  border-radius: 20px;");
              client.println("  padding: 30px;");
              client.println("  box-shadow: 0 8px 32px rgba(0,0,0,0.3);");
              client.println("}");
              client.println("h1 { color: white; margin-bottom: 10px; text-shadow: 2px 2px 4px rgba(0,0,0,0.5); }");
              client.println(".info-panel { ");
              client.println("  background: rgba(255,255,255,0.2); ");
              client.println("  padding: 20px; ");
              client.println("  border-radius: 15px; ");
              client.println("  margin: 15px 0; ");
              client.println("  width: 100%; ");
              client.println("  max-width: 500px; ");
              client.println("  border: 1px solid rgba(255,255,255,0.3);");
              client.println("}");
              client.println(".calibrate-btn { ");
              client.println("  background: #4CAF50; ");
              client.println("  color: white; ");
              client.println("  border: none; ");
              client.println("  padding: 15px 30px; ");
              client.println("  border-radius: 10px; ");
              client.println("  cursor: pointer; ");
              client.println("  font-size: 16px; ");
              client.println("  margin: 15px 0; ");
              client.println("  font-weight: bold; ");
              client.println("  transition: all 0.3s ease; ");
              client.println("}");
              client.println(".calibrate-btn:hover { ");
              client.println("  background: #45a049; ");
              client.println("  transform: translateY(-2px); ");
              client.println("  box-shadow: 0 5px 15px rgba(0,0,0,0.2); ");
              client.println("}");
              client.println(".alert { ");
              client.println("  background: linear-gradient(45deg, #ff416c, #ff4b2b); ");
              client.println("  color: white; ");
              client.println("  padding: 20px; ");
              client.println("  border-radius: 10px; ");
              client.println("  margin: 20px 0; ");
              client.println("  font-weight: bold; ");
              client.println("  font-size: 18px; ");
              client.println("  animation: blink 0.5s infinite; ");
              client.println("  display: none; ");
              client.println("  width: 100%; ");
              client.println("  max-width: 500px; ");
              client.println("  box-shadow: 0 5px 15px rgba(255,0,0,0.3);");
              client.println("}");
              client.println("@keyframes blink { ");
              client.println("  0% { opacity: 1; transform: scale(1); } ");
              client.println("  50% { opacity: 0.7; transform: scale(1.02); } ");
              client.println("  100% { opacity: 1; transform: scale(1); } ");
              client.println("}");
              client.println(".chart-container { ");
              client.println("  width: 100%; ");
              client.println("  max-width: 700px; ");
              client.println("  height: 400px; ");
              client.println("  margin: 20px 0; ");
              client.println("  background: rgba(255,255,255,0.9);");
              client.println("  border-radius: 15px;");
              client.println("  padding: 20px;");
              client.println("}");
              client.println(".buzzer-info { ");
              client.println("  background: rgba(255,255,255,0.2); ");
              client.println("  padding: 15px; ");
              client.println("  border-radius: 10px; ");
              client.println("  margin: 10px 0; ");
              client.println("  font-size: 14px; ");
              client.println("}");
              client.println("</style></head>");
              client.println("<body>");
              client.println("<div class='container'>");
              client.println("<h1>Monitor Inteligente de Gases</h1>");
              client.println("<div class='ip-info'>");
              client.println("<strong>Conectado a: http://" + WiFi.localIP().toString() + "</strong>");
              client.println("</div>");
              
              // Panel de información
              client.println("<div class='info-panel'>");
              client.println("<h3>Informacion del Sensor</h3>");
              client.println("<p>Valor actual: <strong><span id='currentValue'>0</span></strong></p>");
              client.println("<p>Valor calibrado: <strong><span id='calibratedValue'>" + String(calibratedValue) + "</span></strong></p>");
              client.println("<p>Frecuencia zumbador: <strong><span id='buzzerInfo'>-</span> ms</strong></p>");
              client.println("</div>");
              
              // Botón de calibrar
              client.println("<button class='calibrate-btn' onclick='calibrarSensor()'>Calibrar Sensor</button>");
              
              // Alerta
              client.println("<div class='alert' id='alert'>ALERTA! Nivel de gas critico</div>");
              
              // Gráfica
              client.println("<div class='chart-container'>");
              client.println("<canvas id='chart'></canvas>");
              client.println("</div>");

              // Script JavaScript
              client.println("<script>");
              client.println("const ctx = document.getElementById('chart').getContext('2d');");
              client.println("const chart = new Chart(ctx, {");
              client.println("  type: 'line',");
              client.println("  data: { labels: [], datasets: [{");
              client.println("    label: 'Valor del sensor',");
              client.println("    data: [], ");
              client.println("    borderColor: '#4CAF50', ");
              client.println("    backgroundColor: 'rgba(76,175,80,0.1)', ");
              client.println("    borderWidth: 3,");
              client.println("    fill: true, ");
              client.println("    tension: 0.3");
              client.println("  }]},");
              client.println("  options: { ");
              client.println("    responsive: true,");
              client.println("    maintainAspectRatio: false,");
              client.println("    scales: { ");
              client.println("      y: { ");
              client.println("        beginAtZero: true,");
              client.println("        grid: { color: 'rgba(255,255,255,0.1)' },");
              client.println("        ticks: { color: '#333' }");
              client.println("      },");
              client.println("      x: { ");
              client.println("        title: { display: true, text: 'Muestras', color: '#333' },");
              client.println("        grid: { color: 'rgba(255,255,255,0.1)' },");
              client.println("        ticks: { color: '#333' }");
              client.println("      }");
              client.println("    },");
              client.println("    plugins: {");
              client.println("      legend: { labels: { color: '#333', font: { size: 14 } } }");
              client.println("    }");
              client.println("  }");
              client.println("});");

              client.println("async function calibrarSensor() {");
              client.println("  try {");
              client.println("    const response = await fetch('/calibrate');");
              client.println("    const data = await response.json();");
              client.println("    document.getElementById('calibratedValue').textContent = data.calibrated_value;");
              client.println("    alert('Sensor calibrado correctamente. Valor: ' + data.calibrated_value);");
              client.println("  } catch (error) {");
              client.println("    console.error('Error calibrando:', error);");
              client.println("    alert('Error al calibrar el sensor');");
              client.println("  }");
              client.println("}");

              client.println("async function updateData() {");
              client.println("  try {");
              client.println("    const response = await fetch('/data');");
              client.println("    const json = await response.json();");
              client.println("    const values = json.values;");
              client.println("    ");
              client.println("    // Actualizar gráfica");
              client.println("    chart.data.labels = values.map((_, i) => i + 1);");
              client.println("    chart.data.datasets[0].data = values;");
              client.println("    chart.update();");
              client.println("    ");
              client.println("    // Actualizar valores");
              client.println("    document.getElementById('currentValue').textContent = json.current_value;");
              client.println("    document.getElementById('buzzerInfo').textContent = json.buzzer_interval;");
              client.println("    ");
              client.println("    // Manejar alerta");
              client.println("    const alertDiv = document.getElementById('alert');");
              client.println("    if (json.alert_active) {");
              client.println("      alertDiv.style.display = 'block';");
              client.println("    } else {");
              client.println("      alertDiv.style.display = 'none';");
              client.println("    }");
              client.println("  } catch (error) {");
              client.println("    console.error('Error fetching data:', error);");
              client.println("  }");
              client.println("}");
              
              client.println("// Actualizar cada 5 segundos");
              client.println("setInterval(updateData, 5000);");
              client.println("updateData();");
              client.println("</script>");

              client.println("</body></html>");
              client.println();
              lastClientActivity = millis();  // Actualizar actividad

              break;
            }
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }

    delay(10);
    client.stop();
    Serial.println("Cliente desconectado.");
  }
  
  // Verificar si el cliente ha estado inactivo por mucho tiempo
  if (clientConnected && (millis() - lastClientActivity > clientTimeout)) {
    Serial.println("Timeout de cliente - cerrando conexión");
    clientConnected = false;
  }
  
  // Lógica de sueño
  if (!clientConnected && !forceAwake) {
    lecturaSensor();
    enterLightSleep();
  } 
  else {
    lecturaSensor();
    // Si hay cliente o alarma, solo pequeño delay
    delay(1000);
  }
}

