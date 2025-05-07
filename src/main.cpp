#include <Arduino.h>
#include <CAN.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define REASSIGN_PINS
int sck = 18;
int miso = 19;
int mosi = 23;
int cs = 4;
int rx = 14;
int tx = 13;

/*
PINOS PARA TESTE NA PROTOBOARD
int sck = 18;
int miso = 19;
int mosi = 23;
int cs = 5;
int rx = 22;
int tx = 21;
*/

#define LED 2
#define BUFFER_SIZE 10

unsigned long lastSendTime = 0;
const unsigned long maxWaitTime = 1000;
char newFileName[32];

const char* ssid = "ESP32-Logger";
const char* password = "12345678";

//================== INÍCIO SERVER ==================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Formula SAE UFMG Dados FuelTech</title>
  <script>
    var ws;
  
    function startWebSocket() {
      ws = new WebSocket("ws://" + location.host + "/ws");
      ws.onmessage = function(event) {
        let msg = event.data;
        let parts = msg.split(":");
        let id = parts[0];
        let values = parts[1].split(",");
  
        // Adiciona unidades com base no ID
        let display = "";
        switch (id) {
          case "0":
            display = `${values[0]}%  ${values[1]}BAR  ${values[2]}°C  ${values[3]}°C`;
            break;
          case "1":
            display = `${values[0]}BAR  ${values[1]}BAR ${values[2]}BAR  ${values[3]}`;
            break;
          case "2":
            display = `${values[0]}ƛ  ${values[1]}rpm  ${values[2]}°C  ${values[3]}`;
            break;
          case "3":
            display = `${values[0]}Km/h  ${values[1]}Km/h  ${values[2]}Km/h  ${values[3]}Km/h`;
            break;
          case "4":
            display = `${values[0]}  ${values[1]}  ${values[2]}  ${values[3]}`;
            break;
          case "5":
            display = `${values[0]}  ${values[1]}  ${values[2]}  ${values[3]}`;
            break;
          case "6":
            display = `${values[0]}g  ${values[1]}g  ${values[2]}deg/s  ${values[3]}deg/s`;
            break;
          case "7":
            display = `${values[0]}  ${values[1]}L/min  ${values[2]}ms  ${values[3]}ms`;
            break;
          case "8":
            display = `${values[0]}°C  ${values[1]}°C  ${values[2]}L  ${values[3]}BAR`;
            break;
          default:
            display = values.join(" , ");
        }
  
        document.getElementById(id).innerText = display;
      };
    }
  
    window.onload = startWebSocket;
  </script>
<style>
    body {
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      margin: 0;
      font-family: Arial, sans-serif;
      background-color: #f9f9f9;
    }
    .container {
      border: 4px solid red;
      padding: 40px;
      text-align: center;
      border-radius: 15px;
      background-color: white;
    }
    h2 {
      color: red;
    }
  </style>
</head>
<body>
    <div class="container">
        <h2>Dados FuelTech Tempo Real</h2>
        <P>TPS | MAP | Air Temperature | Engine Temperature</p>
            <p id="0">N/A</p>
        <P>Oil Pressure | Fuel Pressure | Water Pressure | Gear</p>
            <p id="1">N/A</p>
        <P>Exhaust O2 | RPM | Oil Temperature | Pit Limit</p>
            <p id="2">N/A</p>    
        <P>Wheel Speed: FR | FL | RR | RL</p>
            <p id="3">N/A</p>
        <P>Traction Ctrl - Slip | Traction Ctrl - Retard | Traction Ctrl - Cut | Heading</p>
            <p id="4">N/A</p>
        <P>Shock Sensor: FR | FL | RR | RL</p>
            <p id="5">N/A</p>
        <P>G-force (accel) | G-force (lateral) | Yaw-rate (frontal) | Yaw-rate (lateral)</p>
            <p id="6">N/A</p>
        <P>Lambda Correction | Fuel Flow Total | Inj Time Bank A | Inj Time Bank B</p>
            <p id="7">N/A</p>
        <P>Oil Temperature |Transmission Temperature | Fuel Consumption | Brake Pressure</p>
            <p id="8">N/A</p>
    </div>
</body>
</html>
)rawliteral";

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  //Nenhum comportamento específico necessário no momento
}
//================== FIM SERVER ==================

//================== INÍCIO SD ==================

/* Liberação de espaço no SD; sendo o espaço a ser liberado passado como um argumento.
   Função é chamada no setup() quando não há um quantidade pré-determinada 
   (10MiB, atualmente) de espaço livre no SD */
bool freeSpace(fs::SDFS &fs, const char *dirname, uint8_t minFree, uint64_t &freeSpace) {
  if (freeSpace >= minFree) {
    return true;
  }

  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    Serial.println("Erro ao abrir diretório.");
    return false;
  }

  File file = root.openNextFile();
  while (file) {
    Serial.printf("Deletando arquivo: %s\n", file.name());
    if (fs.remove(file.name())) {
      Serial.println("Arquivo deletado");

      freeSpace = (fs.totalBytes() - fs.usedBytes()) / (1024 * 1024);
      if (freeSpace >= minFree) {
        file.close();
        root.close();
        return true;
      }
    } else {
      Serial.println("Falha ao deletar");
    }
    file.close();
    file = root.openNextFile();
  }

  root.close();
  return freeSpace >= minFree;
}

/* Função de criação e escrita de um arquivo */
void writeFile(fs::FS &fs, const char *path, const char *message) {
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    return;
  }
  file.print(message);
  file.close();
}

/* Função de escrita a um arquivo já existente */
void appendFile(fs::FS &fs, const char *path, const char *message) {
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    return;
  }
  file.print(message);
  file.close();
}

/* Função que retorna o nome que deve ser númerico do último arquivo criado
   Os arquivos são todos numerados sequencialmente com base na sua criação,
   isso permite que os arquivos de menor número (os mais antigos), sejam deletados
   primeiros do microSD quando não houver o espaço livre definido. */
uint32_t getLastFileNumber(fs::FS &fs, const char *dirname) {
  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    Serial.println("Erro ao abrir diretório");
    return 0;
  }

  uint32_t lastNumber = 0;
  File file = root.openNextFile();
  while (file) {
      String name = String(file.name());
      name = name.substring(0, name.indexOf('.'));
      uint32_t number = name.toInt();
      if (number > lastNumber) {
        lastNumber = number;
      }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  
  return lastNumber;
}
//================== FIM SD ==================


//================== INÍCIO CAN ==================

/* Atualmente o programa só decodifica pacotes com esses IDs, que são os pacotes
   simplificados da FT, já que a decodificação do protocolo CAN FT é muito complexa
   e talvez nem ideal para um ESP32, já que provavelmente gastaria processamento que
   deveria ser utilizado para receber mensagens brutas.
   Apesar da especificidade, o programa está suficientemente versátil para uma implementação, 
   sem muito atrito, de lógica de decodificação para outros tipos de pacotes.
   */
const uint32_t ft550_ids[] = {
  0x14080600,
  0x14080601,
  0x14080602,
  0x14080603,
  0x14080604,
  0x14080605,
  0x14080606,
  0x14080607,
  0x14080608
};

const int num_ids = sizeof(ft550_ids) / sizeof(ft550_ids[0]);

/* Struct que facilita o processamento dos pacotes
   */
struct CANFrame {
  unsigned long timeLog;
  uint32_t id;
  byte data[8];
  uint8_t dlc; //Atualmente, inutilizado
};

/* Buffer utilizado para mitigar problemas de sobrecarga, a ideia é que o envio/escrita
   de um frame utilize mais processamento relativo ao envio/escrita de um conjunto de
   frames ao mesmo tempo.
*/
CANFrame buffer[BUFFER_SIZE];
int bufferIndex = 0;

/* Função que filtra os pacotes de interesse
*/
bool is_ft550_id(uint32_t id) {
  return true;
  for (int i = 0; i < num_ids; i++) {
    if (id == ft550_ids[i]) return true;
  }
  return false;
}
/* Função de decodificação dos payloads; é utilizado um objeto do tipo String de tamanho
   pré-definido, para que não haja a realocação e o redimensionamento deste objeto durante
   a decodificação. É essencial minimizar o tempo gasto na decodificação e envio/escrita, 
   pois novos pacotes que são recebidos pelo controlador podem ser perdidos nesse meio-tempo.
*/
String messageFormatting(uint8_t id, const uint8_t *data){ 
  String msg;
  msg.reserve(128);
  msg = "";
  switch (id) {
    case 0:
      msg += "TPS(%)|MAP(BAR)|AirTemp|EngineTemp(C): ";
      msg += String((data[0] << 8 | data[1]) / 10.0) + ",";
      msg += String(uint16_t(data[2] << 8 | data[3]) / 1000.0) + ",";
      msg += String((int16_t)(data[4] << 8 | data[5]) / 10) + ",";
      msg += String((int16_t)(data[6] << 8 | data[7]) / 10);
      break;
    
    case 1:
      msg += "OilPressure|FuelPressure|WaterPressure(BAR)|Gear: ";
      msg += String(uint16_t(data[0] << 8 | data[1]) / 1000.0) + ",";
      msg += String(uint16_t(data[2] << 8 | data[3]) / 1000.0) + ",";
      msg += String(uint16_t(data[4] << 8 | data[5]) / 1000.0) + ",";
      msg += String(data[6] << 8 | data[7]);
      break;
    
    case 2:
      msg += "ExhaustO2(ƛ)|RPM|OilTemp(C)|PitLimit: "; 
      msg += String((uint16_t)(data[0] << 8 | data[1]) / 1000.0) + ",";
      msg += String((uint16_t)(data[2] << 8 | data[3])) + ",";
      msg += String((int16_t)(data[4] << 8 | data[5]) / 10) + ",";
      msg += String((uint16_t)data[6] << 8 | data[7]);
      break;

    case 3:
      msg += "Wheel Speed(Km/h) FR|FL|RR|RL: ";
      msg += String((data[0] << 8 | data[1])) + ",";
      msg += String((data[2] << 8 | data[3])) + ",";
      msg += String((data[4] << 8 | data[5])) + ",";
      msg += String((data[6] << 8 | data[7]));
      break;

    case 4:
      msg += "Traction Ctrl - Slip|Retard|Cut|Heading: ";
      msg += String((data[0] << 8 | data[1])) + ",";
      msg += String((data[2] << 8 | data[3])) + ",";
      msg += String((data[4] << 8 | data[5])) + ",";
      msg += String((data[6] << 8 | data[7]));
      break;

    case 5:
      msg += "Shock Sensor FR|FL|RR|RL: ";
      msg += String((int16_t)(data[0] << 8 | data[1]) / 1000.0) + ",";
      msg += String((int16_t)(data[2] << 8 | data[3]) / 1000.0) + ",";
      msg += String((int16_t)(data[4] << 8 | data[5]) / 1000.0) + ",";
      msg += String((int16_t)(data[6] << 8 | data[7]) / 1000.0);
      break;

    case 6:
      msg += "G-force(accel)|(lateral)|Yaw-rate(frontal)|(lateral): ";
      msg += String((int16_t)(data[0] << 8 | data[1]) / 1000.0) + ",";
      msg += String((int16_t)(data[2] << 8 | data[3]) / 1000.0) + ",";
      msg += String((int16_t)(data[4] << 8 | data[5])) + ",";
      msg += String((int16_t)(data[6] << 8 | data[7]));
      break;

    case 7:
      msg += "Lambda Correction|Fuel Flow Total(L/min)|Inj Time Bank(ms) A|B: ";
      msg += String((data[0] << 8 | data[1])) + ",";
      msg += String((uint16_t)(data[2] << 8 | data[3]) / 100.0) + ",";
      msg += String((uint16_t)(data[4] << 8 | data[5])/ 100.0) + ",";
      msg += String((uint16_t)(data[6] << 8 | data[7])/ 100.0);
      break;

    case 8:
      msg += "Oil Temp|Transmission Temp(C)|Fuel Consumption(L)""Brake Pressure(Bar): ";
      msg += String((int16_t)(data[0] << 8 | data[1])/ 10.0) + ",";
      msg += String((int16_t)(data[2] << 8 | data[3]) / 10.0) + ",";
      msg += String((int16_t)(data[4] << 8 | data[5])) + ",";
      msg += String((uint16_t)(data[6] << 8 | data[7])/ 1000.0);
      break;
    }
    return msg;
}

/* Função que realiza o envio/escrita do conteúdo processado dos pacotes
*/
void sendBufferData(fs::FS &fs, const char *fileName) {
  String msg;
  msg.reserve(128);
  uint8_t id; //unsigned char?
  for (int i = 0; i < bufferIndex; i++) {    
      id = buffer[i].id & 0x1F;
      msg = messageFormatting(id & 0x0000001F, buffer[i].data);
      appendFile(fs, fileName, (String(buffer[i].timeLog) + " - " + msg + "\n").c_str());
      //Parece que o envio não consegue acompanhar a escrita, os últimos dados que são exibidos na página
      //HTML não são os últimos registrados no SD, com diferença de milisegundos.  
      ws.textAll(String(id) + ":" + msg.substring(msg.indexOf(":") + 1));

    }
    bufferIndex = 0;
}

//================== FIM CAN ==================

void setup() {
  Serial.begin(115200);
  delay(1000);
  //================== SETUP SERVER ==================
  WiFi.softAP(ssid, password);
  Serial.println("Access Point criado");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", htmlPage);
  });

  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  server.begin();

  //================== SETUP SD ==================
  pinMode(LED, OUTPUT);

#ifdef REASSIGN_PINS
  SPI.begin(sck, miso, mosi, cs);
  if (!SD.begin(cs)) {
#else
  if (!SD.begin()) {
#endif
    Serial.println("A conexão com o cartão falhou");
    return;
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("Não há cartão SD inserido");
    return;
  }
  
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  uint64_t freeSpaceMiB = (total - used) / (1024 * 1024);
  Serial.printf("Total: %llu MiB\n", total / (1024 * 1024));
  Serial.printf("Usado: %llu MiB\n", used / (1024 * 1024));
  Serial.printf("Livre: %llu MiB\n", freeSpaceMiB);

  if(freeSpaceMiB < 10){
    digitalWrite(LED, HIGH);
    freeSpace(SD, "/", 10, freeSpaceMiB);
  }

  sprintf(newFileName, "/%04u.csv", getLastFileNumber(SD, "/") + 1); // nomeia o próximo
  
  writeFile(SD, newFileName, "tempo, medida1, medida2, medida3, medida4\n");  
  
  
  //================== SETUP CAN ==================
#ifdef REASSIGN_PINS
  CAN.setPins(rx, tx);
#endif
  if (!CAN.begin(1E6)) { // Taxa de comunicação da FT550
    Serial.println("Erro ao iniciar CAN");
    return;
  } else
  Serial.println("Logger CAN (FT550) iniciado");
}

void loop() {
  int packetSize = CAN.parsePacket();

  if (packetSize) {
    uint32_t id = CAN.packetId();
    if (is_ft550_id(id)) {
      CANFrame newFrame;
      newFrame.timeLog = millis();
      newFrame.id = id;
      newFrame.dlc = packetSize;
      
      int i = 0;
      while (CAN.available()) {
        newFrame.data[i++] = CAN.read();
      }
      // Há, além do critério de espaço, um tempo limite para que um Buffer envie 
      // o seu conteúdo, para que ele não guarde dados indefinidamente.
      if (bufferIndex < BUFFER_SIZE & (millis() - lastSendTime) < maxWaitTime) {
        buffer[bufferIndex++] = newFrame;
      
      } else {
        sendBufferData(SD ,newFileName);
        lastSendTime = millis();
        buffer[0] = newFrame;
        bufferIndex = 1;
      }
    }
  }
}
