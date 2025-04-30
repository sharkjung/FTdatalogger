#include <Arduino.h>
#include <CAN.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define REASSIGN_PINS
int sck = 18;
int miso = 19;
int mosi = 23;
int cs = 5;
int rx = 22;
int tx = 21;

#define LED 2
#define BUFFER_SIZE 10

unsigned long lastSendTime = 0;
const unsigned long maxWaitTime = 1000;
char newFileName[32];

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
    
  switch (id) {
    case 0:
      msg += "TPS(%)|MAP(BAR)|AirTemp|EngineTemp(C): ";
      msg += String((data[0] << 8 | data[1]) / 10.0) + ",";
      msg += String(uint16_t(data[2] << 8 | data[3]) / 1000.0) + ",";
      msg += String((int16_t)(data[4] << 8 | data[5]) / 10.0) + ",";
      msg += String((int16_t)(data[6] << 8 | data[7]) / 10.0);
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
      msg += String((int16_t)(data[4] << 8 | data[5]) / 10.0) + ",";
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
    return msg += "\n";
}

/* Função que realiza o envio/escrita do conteúdo processado dos pacotes
*/
void sendBufferData(fs::FS &fs, const char *fileName) {
  String msg;
  msg.reserve(128);
  for (int i = 0; i < bufferIndex; i++) {    
      msg = messageFormatting(buffer[i].id & 0x0000001F, buffer[i].data);
      appendFile(fs, fileName, (buffer[i].timeLog + " - " +msg).c_str());
    }
    bufferIndex = 0;
}

//================== FIM CAN ==================

void setup() {
  Serial.begin(115200);
  delay(1000);

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
  
  //================== SETUP SD ==================
  
  
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