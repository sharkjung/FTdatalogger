#include <CAN.h>

void setup() {
  Serial.begin(115200);
  if(!CAN.begin(1E6)){ // 1Mbps
    while(1);
  }
  randomSeed(analogRead(0)); 

}

/* A função loop() está repetidamente enviando pacotes simples de dados e IDs permitidos aleatórios,
   na tentativa de simular parcialmente o comportamento real da ECU. 
*/
void loop() {
  int r = random(0, 8);
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
