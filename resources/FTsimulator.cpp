#include <CAN.h> //Biblioteca de comunicação CAN
//Pacote enviado deve ser do tipo estendido, mas sua configuração de dados presentes no ID deverá seguir 
//o padrão da FuelTech

void setup() {
  Serial.begin(115200);
  if(!CAN.begin(1E6)){
    while(1);
  }
  randomSeed(analogRead(0)); 

}

void loop() {
  int r = random(0, 9);
  uint32_t id = 0x14080600 + r;

  Serial.println("Enviando---");
  Serial.println(id, HEX);

  CAN.beginExtendedPacket(id); 
    

  for(int i = 0; i < 8; i++){
    byte data = random(0, 255);
    CAN.write(data);
    Serial.print(data);
    Serial.print(" ");
  }

  if(CAN.endPacket()){
  Serial.println("\n--->Pacote enviado");
  }
  delay(10); //~100Hz
}
