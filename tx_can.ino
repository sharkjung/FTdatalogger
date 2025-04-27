#include <CAN.h> //Biblioteca de comunicação CAN
//Pacote enviado deve ser do tipo estendido, mas sua configuração de dados presentes no ID deverá seguir 
//o padrão da FuelTech

void setup() {
  Serial.begin(9600);
  if(!CAN.begin(500E3)){ //Se mantém aqui até inicializar a comunicação CAN
    while(1);
  }

}

void loop() {
  int dado=10,
  productID = 0x0001, //15 bits 
  /*
  Na EGT-4 eu acho que usaremos esse código 
  0x0900 FuelTech EGT-4 CAN (model A)
  */

  datafield = 0x00, //3 bits
  /*
     0x00 indica que os dados sao do tipo: Standard CAN data field
  */

  messageID = 0x0FF, //11 bits 
  /*
  Em message ID:
  0x0FF – Critical priority
  0x1FF – High priority
  0x2FF – Medium priority
  0x3FF – Low priority
  */

  ID = (productID<<14)|(datafield<<11)|(messageID);
   //O ID é segmentado em partes, irei fazer a composição com operações de OR bit a bit, mais didatico (pra mim rs)
  //fazer deslocamento de bits a esquerda e fazer OR com o que se deseja inserir atrás
  Serial.println("Enviando---");

  CAN.beginExtendedPacket(ID); //Começa o pacote informando o seu ID que terá 29bits
  
  CAN.write(dado);//1 byte
  
  CAN.endPacket();//informa que pacote foi finalizado
  
  Serial.println("--->Pacote enviado");

  delay(1000); //Tempo entre envio de pacotes
}
