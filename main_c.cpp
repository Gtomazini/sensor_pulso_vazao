#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "censurado";
const char* password = "censurado";
const char* serverUrl = "censurado";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3 * 3600;  // testar brasil
const int daylightOffset_sec = 0; 

int sensorPin = 13;

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

volatile long pulse = 0;
volatile unsigned long lastPulseTime = 0;
float total_volume_m3 = 0.0;
unsigned long lastTime;
unsigned long lastPulseDetected = 0;
bool fluxoAtivo = false;
bool aguardandoAutorizacao = false;
bool autorizado = false;
const int codigo_autorizacao = 1234;

void IRAM_ATTR increase() {
  unsigned long now = micros();
  # sempre que passar 5 ms ele faz outra leitura para evitar ruidos ou pulsos muito rapidos
  if (now - lastPulseTime > 5000) {
    pulse++;
    lastPulseTime = now;
    lastPulseDetected = millis();
    fluxoAtivo = true;
  }
}

void checaAutorizacaoSerial() {
  if (Serial.available()) {
    String entrada = Serial.readStringUntil('\n');
    entrada.trim();
    if (entrada.toInt() == codigo_autorizacao) {
      autorizado = true;
      Serial.println("Autorizado! O próximo POST será enviado.");
    } else {
      Serial.println("Código incorreto. Tente novamente.");
    }
  }
}


String getDateTime();
bool sendData(float consumo_pontos, String datetime);
bool checkServerConnection();

void setup() {
  pinMode(sensorPin, INPUT);
  Serial.begin(9600);
  # chama o increase
  attachInterrupt(digitalPinToInterrupt(sensorPin), increase, RISING);
  
  Serial.println("\nIniciando conexão WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  

  Serial.println("Configurando servidor de tempo...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

 
  struct tm timeinfo;
  int tentativas = 0;
  while (!getLocalTime(&timeinfo) && tentativas < 20) {
    Serial.println("Aguardando sincronização NTP...");
    delay(500);
    tentativas++;
  }
  if (tentativas >= 20) {
    Serial.println("Falha ao sincronizar com o NTP!");
  } else {
    Serial.print("Data e hora atuais: ");
    char timeStringBuff[25];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    Serial.println(timeStringBuff);
  }
  

  if (checkServerConnection()) {
    Serial.println("Servidor API acessível!");
  } else {
    Serial.println("AVISO: Não foi possível conectar ao servidor API.");
    Serial.println("O monitoramento continuará, mas os envios podem falhar.");
  }
  
  Serial.println("Monitoramento de fluxo iniciado!");
  Serial.println("Aguardando detecção de fluxo...");
  
  lastTime = millis();
  lastPulseDetected = millis();
}

void loop() {
  checaAutorizacaoSerial();
  static long lastPulseCount = 0;
  long currentPulse = pulse;
  long deltaPulse = currentPulse - lastPulseCount;
  
  if (deltaPulse < 0) deltaPulse = 0; // segurança
  
  
  if (deltaPulse > 0) {
    float delta_volume_m3 = 2.663 * deltaPulse / 1000 * 30 / 1000.0;
    total_volume_m3 += delta_volume_m3;
    lastPulseCount = currentPulse;
    

    if (fluxoAtivo) {
      Serial.print("Pulsos: ");
      Serial.print(currentPulse);
      Serial.print(" | Volume acumulado: ");
      Serial.print(total_volume_m3, 6);
      Serial.println(" m³");
    }
  }
  
  
  if (fluxoAtivo && (millis() - lastPulseDetected > 2000)) {
    fluxoAtivo = false;  // Marca que o fluxo parou
    Serial.println("\n--- Fluxo finalizado ---");
    Serial.print("Volume total acumulado: ");
    Serial.print(total_volume_m3, 6);
    Serial.println(" m³");
    
    if (total_volume_m3 > 0.0) {
      Serial.println("Digite 1234 para autorizar o envio do POST.");
      aguardandoAutorizacao = true;
    }
  }
  

  if (!fluxoAtivo && total_volume_m3 > 0.0 && autorizado) {
    String datetime = getDateTime();
    bool enviado = false;
    

    for (int tentativa = 1; tentativa <= 3 && !enviado; tentativa++) {
      if (tentativa > 1) {
        Serial.print("Tentativa ");
        Serial.print(tentativa);
        Serial.println(" de envio...");
        delay(2000); 
      }
      
      enviado = sendData(total_volume_m3, datetime);
    }
    
    if (enviado) {
      Serial.println("POST enviado com sucesso!");
      total_volume_m3 = 0.0;
      lastPulseCount = currentPulse;  
    } else {
      Serial.println("ERRO no envio do POST após múltiplas tentativas.");
      Serial.println("Os dados serão preservados para envio posterior.");
    }
    
    autorizado = false;
    aguardandoAutorizacao = false;
    Serial.println("\nAguardando novo fluxo...");
  }
  
  
  if (!fluxoAtivo && total_volume_m3 > 0.0 && !autorizado && !aguardandoAutorizacao) {
    Serial.println("Digite 1234 para autorizar o envio do POST.");
    aguardandoAutorizacao = true;
  }
  
  delay(100);
}


bool checkServerConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    Serial.println("Verificando conexão com o servidor API...");
    
   
    if (http.begin(serverUrl)) {
      http.setTimeout(5000);  
      
      int httpCode = http.GET();
      http.end();
      // se o get deu certo
      if (httpCode > 0) {
        return true;  
      } else {
        Serial.print("Código de erro: ");
        Serial.println(httpCode);
      }
    }
  }
  return false;
}


String getDateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Falha ao obter o tempo, usando valor padrão");
    return "2025-05-21T12:00:00";  
  }
  
  char timeStringBuff[25];  
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  
  return String(timeStringBuff);
}

bool sendData(float consumo_pontos, String datetime) {
  bool sucesso = false;
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    Serial.println("[HTTP] Iniciando conexão...");
    
    if (http.begin(serverUrl)) {
      
      http.setTimeout(15000);
      
      
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      
      http.addHeader("Content-Type", "application/json");
      
      String payload = "{";
      payload += "\"id_device_vinc\": 1,";
      payload += "\"consumo_pontos\": ";
      payload += String(consumo_pontos, 6);
      payload += ",\"datetime\": \"" + datetime + "\"}";
      
      Serial.println("Enviando payload:");
      Serial.println(payload);
      
      int httpResponseCode = http.POST(payload);
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      
      if (httpResponseCode > 0) {
        if (httpResponseCode == 200 || httpResponseCode == 201 || httpResponseCode == 202) {
          // Códigos de sucesso
          String response = http.getString();
          Serial.println("Resposta do servidor:");
          Serial.println(response);
          sucesso = true;
        } else if (httpResponseCode == 301 || httpResponseCode == 302 || httpResponseCode == 307 || httpResponseCode == 308) {
         
          String newUrl = http.getLocation();
          Serial.print("Redirecionado para: ");
          Serial.println(newUrl);
          
          
          http.end();
          
          
          if (newUrl.length() > 0) {
            Serial.println("Tentando com o novo URL...");
            if (http.begin(newUrl)) {
              http.setTimeout(15000);
              http.addHeader("Content-Type", "application/json");
              int newResponseCode = http.POST(payload);
              
              if (newResponseCode > 0 && newResponseCode < 400) {
                Serial.print("Nova resposta: ");
                Serial.println(newResponseCode);
                String response = http.getString();
                Serial.println(response);
                sucesso = true;
              }
            }
          }
        } else {
          
          String response = http.getString();
          Serial.println("Resposta do servidor:");
          Serial.println(response);
        }
      } else {
        Serial.print("Erro na requisição HTTP: ");
        Serial.println(http.errorToString(httpResponseCode));
      }
      
      http.end();
    } else {
      Serial.println("[HTTP] Não foi possível iniciar a conexão");
    }
  } else {
    Serial.println("WiFi not connected");
  }
  
  return sucesso;
}