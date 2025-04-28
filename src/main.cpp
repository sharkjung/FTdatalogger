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
struct CANFrame { //struct para um pacote, não utilizei o nome "pacote" por ambiguidade com a biblioteca
  unsigned long timeLog;
  uint32_t id;
  byte data[8]; //conteúdo
  uint8_t dlc; //tamanho do conteúdo (Data Length Code)
};

//Buffer é uma array que salva os pacotes e depois os envia todos de uma vez, ainda vou explicar o porquê disso
CANFrame buffer[BUFFER_SIZE];
int bufferIndex = 0;

//Função que verifica se os pacotes são de interesse, talvez isso seja um 
//pouco desnecessário e atrase um pouco a execução
bool is_ft550_id(uint32_t id) {
  return true;
  /*for (int i = 0; i < num_ids; i++) {
    if (id == ft550_ids[i]) return true;
  }
  return false;*/
}

//função para enviar os dados do buffer
//eu acho que enviar um pacote de cada vez leva mais tempo do que enviar vários de uma só vez.
//com mais pacotes enviados de uma só vez acho q maior o tempo, daí talvez um critério para BUFFER_SIZE
//o problema é que quando o programa está enviando para o monitor ou escrevendo no cartão sd um pacote
//podem chegar outros pacotes no buffer do tansceiver, que se não me engano aguenta no máximo dois pacotes,
//e se vierem mais do que dois durante esse tempo um dos pacotes sera sobrescrito e haverá perca de dados.
//essa questão do tempo eu não sei se é essencial para o projeto e é algo que carece de testes cronometrados
//o controlador can até lida com sobrecarga, mas isso atrasa a rede, a questão é que a rede pelo o que eu entendo
//é apenas a ft e o esp, será que realmente é um problema atrasar a rede?
void sendBufferData() {
  for (int i = 0; i < bufferIndex; i++) {
    Serial.print(" ms | ID(0x) | Data");
    //millis é de qnd estava sem o buffer, aqui ele fica assíncrono (na vdd, eu acredito q não tenha
    //como ao certo saber o tempo exato q os pacotes chegam, mas claro q estaria em um desvio aceitável)
    //se for essencial o tempo, existem soluções
    Serial.print(buffer[i].timeLog); Serial.print(", ");
    Serial.print(buffer[i].id, HEX); Serial.print(", ");
    //itera pelo buffer e pela mensagem do pacote
    for (int j = 0; j < buffer[i].dlc; j++) {
      Serial.print(" "); Serial.print(buffer[i].data[j], HEX);
      //eu preciso ler o protocolo da FT, pra deixar essas mensagens inteligíveis, isso vai ser um saco de implementar 
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
  //tenta pegar um pacote
  int packetSize = CAN.parsePacket();

  if (packetSize) { //se não recebeu um pacote, o valor de packetsize seria zero
    uint32_t id = CAN.packetId();
    //salva pacote
    if (is_ft550_id(id)) {
      CANFrame newFrame;
      newFrame.timeLog = millis();
      newFrame.id = id;
      newFrame.dlc = packetSize;
      
      // only print packet data for non-RTR packets
      // acho que nunca é RTR
      // available retorna true até que os 8 bytes do conteúdo sejam todos iterados
      int i = 0;
      while (CAN.available()) {
        newFrame.data[i++] = CAN.read();
      }

      //adiciona o pacote ao buffer
      if (bufferIndex < BUFFER_SIZE || (millis() - lastSendTime) < maxWaitTime) {
        buffer[bufferIndex++] = newFrame;
      } else {
        //buffer cheio, envia e reinicia; também vou colocar um critério de tempo, 
        //para que o buffer não guarde menos de BUFFER_SIZE pacotes por tempo indefinido
        //se o buffer estiver vazio após
        sendBufferData();
        lastSendTime = millis();
        //envia e depois guarda o pacote q não coube
        buffer[0] = newFrame;
        bufferIndex = 1;
      }
    }
  }
}
