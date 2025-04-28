#include <CAN.h> //Biblioteca de comunicação CAN
//Pacote enviado deve ser do tipo estendido, mas sua configuração de dados presentes no ID deverá seguir 
//o padrão da FuelTech

void setup() {
  Serial.begin(115200);
  if(!CAN.begin(1E6)){ //Se mantém aqui até inicializar a comunicação CAN
    while(1);
  }

}

void loop() {
  uint8_t dado = 0;
  uint16_t productID = 0x2001;
  uint8_t dataFieldID = 0x2;
  uint16_t messageID = messageID = 0x0FF;
  
  /*
  Na EGT-4 eu acho que usaremos esse código 
  0x0900 FuelTech EGT-4 CAN (model A)
  */

 //3 bits
  /*
     0x00 indica que os dados sao do tipo: Standard CAN data field
  */

 //11 bits 
  /*
  Em message ID:
  0x0FF – Critical priority
  0x1FF – High priority
  0x2FF – Medium priority
  0x3FF – Low priority
  */

  uint32_t ID = ((productID<<14)|(dataFieldID<<11)|(messageID)) & 0x1FFFFFFF;
  //O ID é segmentado em partes, irei fazer a composição com operações de OR bit a bit, mais didatico (pra mim rs)
  //fazer deslocamento de bits a esquerda e fazer OR com o que se deseja inserir atrás
  Serial.println("Enviando---");
  Serial.println(ID, HEX);
  Serial.println(dataFieldID, HEX);
  Serial.println(messageID, HEX);
  Serial.println(productID, HEX);

  CAN.beginExtendedPacket(ID); //Começa o pacote informando o seu ID que terá 29bits
    
  CAN.write(dado);//1 byte
  CAN.write(dado++);
  CAN.write(dado++);
  CAN.write(dado++);
    
  CAN.endPacket();//envia pacote
    
  Serial.println("--->Pacote enviado");

  delay(2000); //Tempo entre envio de pacotes
}
