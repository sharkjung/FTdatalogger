#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
extern "C"{
  #include "driver/twai.h"
}

#define SCK_PIN  18
#define MISO_PIN 19
#define MOSI_PIN 23
#define CS_PIN    4
#define RX_PIN   14
#define TX_PIN   13

/*PINOS PARA TESTE NA PROTOBOARD
#define SCK_PIN 18
#define MISO_PIN 19
#define MOSI_PIN 23
#define CS_PIN 5
#define RX_PIN 22
#define TX_PIN 21
*/
#define LED 2
#define BUFFER_SIZE 10
#define STRING_RESERVE_SIZE 100
#define POLLING_RATE_MS 1
#define MINIMUM_FREE_SPACE_MiB 10

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
      ws.onmessage = function (event) {
        //Separa as mensagens em listas para tratar os dados
        let msg = event.data;
        let parts = msg.split("\n");
        //A primeira linha carrega os IDs
        let idList = parts[0].split("-");

        // Adiciona unidades com base no ID
        for (let i = 0; i < (idList.length - 1); i++) { // A iteração ignora o último elemento, pois ele é vazio por causa de um "-" extra
          let values = parts[i + 1].split(":")[1].split(","); //podre
          let display = "";

          switch (idList[i]) {
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

          document.getElementById(idList[i]).innerText = display;
        };
      }
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
    // Os IDs dos parágrafos correspondem aos IDs dos pacotes simplificados
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

//Função chamada por ws para lidar com eventos 
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

  // Abre em root o diretório com o nome passado como argumento
  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    Serial.println("Erro ao abrir diretório.");
    return false;
  }

  File file = root.openNextFile(); // Abre o primeiro arquivo no filesystem
  while (file) {
    Serial.printf("Deletando arquivo: %s\n", file.name());
    if (fs.remove(file.name())) {
      Serial.println("Arquivo deletado");

      //Verifica se a remoção liberou o espaço necessário
      freeSpace = (fs.totalBytes() - fs.usedBytes()) / (1024 * 1024);
      if (freeSpace >= minFree) {
        file.close();
        root.close();
        return true;
      }
    } else {
      // Não há tratamento para falhas
      Serial.println("Falha ao deletar");
    }
    file.close();
    file = root.openNextFile(); //Se falhar, o loop não se repete
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

/* Função que retorna o nome, que deve ser númerico, do último arquivo criado
   Os arquivos são todos numerados sequencialmente com base na sua criação,
   isso permite que os arquivos de menor número (os mais antigos), sejam deletados
   primeiros do microSD quando não houver o espaço livre definido. */
uint32_t getLastFileNumber(fs::FS &fs, const char *dirname) {
  /*Essa função deve ser sempre chamada após a liberação de espaço,
    pois *.openNextFile() retornará um file vazio se chegar ao final 
    do sistema de arquivos.
  */
  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    Serial.println("Erro ao abrir diretório");
    return 0;
  }

  uint32_t lastNumber = 0;
  File file = root.openNextFile();
  //Itera por todos arquivos para achar o maior número
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
   e talvez nem ideal para ser feita no ESP32, já que provavelmente gastaria 
   processamento que deveria ser utilizado para apenas receber as mensagens não decodificadas.
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

// Quantidade de IDs que são processados pelo programa
const int num_ids = sizeof(ft550_ids) / sizeof(ft550_ids[0]);

/* Struct que facilita o processamento dos pacotes
   */
struct ft_can_message_t {
  unsigned long timeLog;
  uint32_t id;
  byte data[8];
  uint8_t dlc; //Atualmente, inutilizado
};

/* Buffer utilizado para mitigar problemas de sobrecarga, a intenção é diminuir overhead
de escrita e envio
*/
ft_can_message_t buffer[BUFFER_SIZE];
int bufferIndex = 0;

// Função que filtra os pacotes de interesse
bool is_ft_id(uint32_t &id) {
  // A iteração é rápida suficiente para a quantidade atual de ids
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
String messageFormatting(unsigned long time, uint8_t id, const uint8_t *data){ 
  static String msg;
  msg.reserve(STRING_RESERVE_SIZE);
  msg = String(time) + " - ";
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
      msg += String((int16_t)(data[0] << 8 | data[1])/ 10) + ",";
      msg += String((int16_t)(data[2] << 8 | data[3]) / 10) + ",";
      msg += String((int16_t)(data[4] << 8 | data[5])) + ",";
      msg += String((uint16_t)(data[6] << 8 | data[7])/ 1000.0);
      break;
    }
    return msg + "\n";
}

/* Função que realiza a transmissão/escrita do conteúdo processado dos pacotes
*/
void sendBufferData(fs::FS &fs, const char *fileName) {
  static String msg;
  static String idList; //lista com ids para a primeira linha da string enviada pelo servidor
  msg.reserve(BUFFER_SIZE * STRING_RESERVE_SIZE); //será feito apenas na 1a chamada
  idList.reserve(BUFFER_SIZE * 3); //será feito apenas na 1a chamada
  msg = "";
  idList = "";
  uint8_t id;
  //Lê e processa conteúdo das mensagens
  for (int i = 0; i < bufferIndex; i++) {
      id = buffer[i].id & 0x1F;
      msg += messageFormatting(buffer[i].timeLog, id, buffer[i].data);
      idList += String(id) + "-";
  }
  //Escreve string no microsd
  appendFile(fs, fileName, msg.c_str());
  //Envia string via websocket
  ws.textAll(idList + "\n" + msg);
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

  //handler para requisição get http
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", htmlPage);
  });

  //handler para websocket
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  server.begin();

  //================== SETUP SD ==================
  pinMode(LED, OUTPUT);
  // inicia conexão spi com o leitor do microSD
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
  if (!SD.begin(CS_PIN)) {
    Serial.println("A conexão com o cartão falhou");
    return;
  }

  //Reconhece tipo do cartão SD
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("Não há cartão SD inserido");
    return;
  }

  //Leitura do espaço no cartão
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  uint64_t freeSpaceMiB = (total - used) / (1024 * 1024);
  Serial.printf("Total: %llu MiB\n", total / (1024 * 1024));
  Serial.printf("Usado: %llu MiB\n", used / (1024 * 1024));
  Serial.printf("Livre: %llu MiB\n", freeSpaceMiB);

  //Libera o espaço livre mínimo, se necessário
  if(freeSpaceMiB < MINIMUM_FREE_SPACE_MiB){
    //O led se manterá aceso enquanto ESP32 permanecer ligado
    digitalWrite(LED, HIGH);
    freeSpace(SD, "/", MINIMUM_FREE_SPACE_MiB, freeSpaceMiB);
  }

  //formatação do nome do novo arquivo
  sprintf(newFileName, "/%04u.csv", getLastFileNumber(SD, "/") + 1);
  //escreve novo arquivo
  writeFile(SD, newFileName, "tempo, medida1, medida2, medida3, medida4\n");  
  
  
  //================== SETUP TWAI ==================
  //Configura pinos twai, modo listener e tamanho da fila 
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN, (gpio_num_t)RX_PIN, TWAI_MODE_LISTEN_ONLY);
  g_config.rx_queue_len = 250;
  //baud ft can
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS(); 
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  // Instala o driver TWAI
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.println("Driver instalado");
  } else {
    Serial.println("Falha ao instalar o driver");
    return;
  }

  // Inicia driver TWAI
  if (twai_start() == ESP_OK) {
    Serial.println("Driver iniciado");
  } else {
    Serial.println("Falha ao iniciar o driver");
    return;
  }

  // Reconfigura alertas
  uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_BUS_ERROR;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK) {
    Serial.println("Alertas CAN reconfigurados");
  } else {
    Serial.println("Falha ao reconfigurar alertas");
    return;
  }

}

//salva mensagem em ft_can_message_t
static void handle_rx_message(twai_message_t &message) {
  ft_can_message_t ftMessage;
  ftMessage.id = message.identifier;
  ftMessage.timeLog = millis();
  for(int i = 0; i < message.data_length_code; i++){
    ftMessage.data[i] = message.data[i];
  }
  buffer[bufferIndex++] = ftMessage;
}

void loop() {
  uint32_t alerts_triggered;
  // espera por alertas
  twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(POLLING_RATE_MS));
  twai_status_info_t twaistatus;
  twai_get_status_info(&twaistatus);

  //tratamento de alertas
  if (alerts_triggered & TWAI_ALERT_BUS_ERROR) {
    Serial.println("Alert: A (Bit, Stuff, CRC, Form, ACK) error has occurred on the bus.");
    Serial.printf("Bus error count: %lu\n", twaistatus.bus_error_count);
  }
  if (alerts_triggered & TWAI_ALERT_RX_QUEUE_FULL) {
    Serial.println("Alert: The RX queue is full causing a received frame to be lost.");
    Serial.printf("RX buffered: %lu\t", twaistatus.msgs_to_rx);
    Serial.printf("RX missed: %lu\t", twaistatus.rx_missed_count);
    Serial.printf("RX overrun %lu\n", twaistatus.rx_overrun_count);
  }

  if (alerts_triggered & TWAI_ALERT_RX_DATA) {
    // Uma ou mais mensagens recebidas, processa todas
    twai_message_t message;
    while (twai_receive(&message, 0) == ESP_OK && bufferIndex < BUFFER_SIZE) {
      if(is_ft_id(message.identifier) && !message.rtr){ 
        handle_rx_message(message);
      }
    }
  }
  //esvazia buffer se estiver cheio
  if (bufferIndex >= BUFFER_SIZE) {
    sendBufferData(SD, newFileName);
  }
}
