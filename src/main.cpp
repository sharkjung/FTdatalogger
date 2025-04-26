#include <Arduino.h>
#include <CAN.h>

//ids de interesse da FT
//uint32_t é um tipo de 32 bits, o pacote extendido de ID de 29 bits
//"signed integer type with width of exactly 8, 16, 32 and 64 bits respectively
// with no padding bits and using 2's complement for negative values"
const uint32_t ft550_ids[] = {
  0x0CFF0500,
  0x0CFF0600,
  0x0CFF0700
  //esses ids são de exemplo
};
// num_ids = tamanho de ft550_ids[]
const int num_ids = sizeof(ft550_ids) / sizeof(ft550_ids[0]);

#define BUFFER_SIZE 10  // Tamanho do buffer, não pensei em um critério pra esse número
struct CANData { //struct para um pacote, não utilizei o nome "pacote" por ambiguidade com a biblioteca
  uint32_t id;
  byte data[8]; //conteúdo
  uint8_t dlc; //tamanho do conteúdo (Data Length Code)
};

//Buffer é uma array que salva os pacotes e depois os envia todos de uma vez, ainda vou explicar o porquê disso
CANData buffer[BUFFER_SIZE];
int bufferIndex = 0;

//Função que verifica se os pacotes são de interesse, talvez isso seja um 
//pouco desnecessário e atrase um pouco a execução
bool is_ft550_id(uint32_t id) {
  for (int i = 0; i < num_ids; i++) {
    if (id == ft550_ids[i]) return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!CAN.begin(500E3)) {
    Serial.println("Erro ao iniciar CAN");
    while(1);
  }

  //CAN.setPins();  //RX = GPIO4, TX = GPIO5, por padrão
  Serial.println("Logger CAN (FT550) iniciado");
}

void loop() {
  //tenta pegar um pacote
  int packetSize = CAN.parsePacket();

  if (packetSize) { //se não recebeu um pacote, o valor de packetsize seria zero
    uint32_t id = CAN.packetId();

    //salva pacote
    if (is_ft550_id(id)) {
      CANData newData;
      newData.id = id;
      newData.dlc = packetSize;

      int i = 0;
      // only print packet data for non-RTR packets
      // acho que nunca é RTR
      // available retorna true até que os 8 bytes do conteúdo sejam todos iterados
      while (CAN.available()) {
        newData.data[i++] = CAN.read(); //testar em código separado, não lembro se 
                                        //isso dá certo com i = 0 ao invés de i = -1
      }

      //adiciona o pacote ao buffer
      if (bufferIndex < BUFFER_SIZE) {
        buffer[bufferIndex++] = newData; //testar
      } else {
        //buffer cheio, envia e reinicia; também vou colocar um critério de tempo, 
        //para que o buffer não guarde menos de BUFFER_SIZE pacotes por tempo indefinido
        sendBufferData();
        //envia e depois guarda o pacote q não coube
        buffer[0] = newData;
        bufferIndex = 1;
      }
    }
  }
}

//função para enviar os dados do buffer
//eu acho que enviar um pacote de cada vez leva mais tempo do que enviar vários de uma só vez.
//com mais pacotes enviados de uma só vez acho q maior o tempo, daí talvez um critério para BUFFER_SIZE
//o problema é que quando o programa está enviando para o monitor ou escrevendo no cartão sd um pacote
//podem chegar outros pacotes no buffer do tansceiver, que se não me engano aguenta no máximo dois pacotes,
//e se vierem mais do que dois durante esse tempo um dos pacotes sera sobrescrito e haverá perca de dados.
//essa questão do tempo eu não sei se é essencial para o projeto e é algo que carece de testes cronometrados
void sendBufferData() {
  for (int i = 0; i < bufferIndex; i++) {
    //millis é de qnd estava sem o buffer, aqui ele fica assíncrono (na vdd, eu acredito q não tenha
    //como ao certo saber o tempo exato q os pacotes chegam, mas claro q estaria em um desvio aceitável)
    //se for essencial o tempo, existem soluções
    Serial.print(millis());
    Serial.print(" ms | ID: 0x"); Serial.print(buffer[i].id, HEX);
    Serial.print(" | Data:");
    //itera pelo buffer e pela mensagem do pacote
    for (int j = 0; j < buffer[i].dlc; j++) {
      Serial.print(" "); Serial.print(buffer[i].data[j], HEX);
      //eu preciso ler o protocolo da FT, pra deixar essas mensagens inteligíveis, isso vai ser um saco de implementar 
    }
    Serial.println();
  }
  bufferIndex = 0;  //reseta o índice do buffer
}