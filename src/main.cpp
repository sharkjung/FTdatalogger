/* Funcionamento SD: 
   cria um arquivo .csv toda vez que setup é chamado
   o nome do arquivo é um valor numérico sequencial em relação aos arquivos anteriores
   quando o espaço disponível é de uma certa quantidade pequena, o esp32 manterá seu led
   azul acesso
   no caso que o disco fique cheio, os arquivos de menor valor numérico serão sobrescritos 
   como isso seria feito? não há como o esp32 deletar os arquivos csv on demand enquanto 
   expande o arquivo no qual está escrevendo
   acho que corromper e deletar os arquivos não é ideal, o FAT não deve suportar bem isso
   é melhor ir liberando espaço de cada vez
   o algoritmo pra isso parecer se um pouco complicado, pq não seria ideal excluir arquivos
   grandes para conseguir pouco armazenamento. Por exemplo, se devem ser liberados 5MiB e existem
   4 arquivos em sequência que ao todo possuem 4.5MiB e logo em seguida um com 7MiB, não faz sentido
   deletâ-lo. Talvez colocar como condição para a exclusão do arquivo o seu tamanho em relação ao espaço necessário
   Um arquivo de 7MiB > 0.5MiB, então ele é ignorado para aquela limpeza; mas e se ele fosse o primeiro?
   e quando houver um arquivo de 1Mib e outro em seguida de 5MiB?    
*/
#include <Arduino.h>
#include <CAN.h>

//ids de interesse da FT
const uint32_t ft550_ids[] = {
  0x0CFF0500,
  0x0CFF0600,
  0x0CFF0700
  //esses ids são de exemplo
};
// num_ids = tamanho de ft550_ids[]
const int num_ids = sizeof(ft550_ids) / sizeof(ft550_ids[0]);

#define BUFFER_SIZE 10  // Tamanho do buffer, ainda não há um critério para esse número
struct CANFrame { //struct para um frame
  unsigned long timeLog;
  // divisões id protocolo ft
  uint16_t productID;
  uint8_t dataFieldID;
  uint16_t messageID;
  byte data[8]; //mensagem
  uint8_t dlc; //tamanho do conteúdo (Data Length Code)
};

// BUFFER para que os pacotes sejam escritos ao mesmo tempo reduzindo sobrecarga
CANFrame buffer[BUFFER_SIZE];
int bufferIndex = 0;

//Função que verifica se os pacotes são de interessem, inutilizado
bool is_ft550_id(uint32_t id) {
  return true;
  /*for (int i = 0; i < num_ids; i++) {
    if (id == ft550_ids[i]) return true;
  }
  return false;*/
}


//o controlador can até lida com sobrecarga, mas isso atrasa a rede, a questão é que a rede pelo o que eu entendo
//é apenas a ft e o esp, será que realmente é um problema atrasar a rede?
void sendBufferData() {
  for (int i = 0; i < bufferIndex; i++) {
    Serial.print(buffer[i].timeLog); Serial.print(", ");
    Serial.print(buffer[i].productID, HEX); Serial.print(", ");
    Serial.print(buffer[i].dataFieldID, HEX); Serial.print(", ");
    Serial.print(buffer[i].messageID, HEX); Serial.print(", ");

    for (int j = 0; j < buffer[i].dlc; j++) {
      Serial.print(" "); Serial.print(buffer[i].data[j], HEX);
    }
    Serial.println();
  }
  bufferIndex = 0;  //reseta o índice do buffer
}

unsigned long lastSendTime = 0; //problemático
const unsigned long maxWaitTime = 1000;

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!CAN.begin(1E6)) {
    Serial.println("Erro ao iniciar CAN");
    while(1);
  }

  //CAN.setPins();  //RX = GPIO4, TX = GPIO5, por padrão
  Serial.println("Logger CAN (FT550) iniciado");
}

void loop() {
  int packetSize = CAN.parsePacket();

  if (packetSize) {
    uint32_t id = CAN.packetId();
    //salva pacote
    if (is_ft550_id(id)) {
      CANFrame newFrame;
    
      newFrame.timeLog = millis();
      newFrame.productID = id >> 14;
      newFrame.dataFieldID = (id >> 11) & 0x06;
      newFrame.messageID = id & 0x07FF;
      newFrame.dlc = packetSize;
      
      int i = 0;
      while (CAN.available()) {
        newFrame.data[i++] = CAN.read();
      }

      //adiciona o pacote ao buffer
      if (bufferIndex < BUFFER_SIZE & (millis() - lastSendTime) < maxWaitTime) {
        buffer[bufferIndex++] = newFrame;
      
      } else {
        sendBufferData();
        lastSendTime = millis();
        //envia e depois guarda o pacote q não coube
        buffer[0] = newFrame;
        bufferIndex = 1;
      }
    }
  }
}
