#include <WiFi.h>
#include <string>

const char* ssid = "ESP-AP"; //Nome da rede do ESP
const char* pass = "senha123"; //Senha para se conectar na rede na qual o ESP fornecerá uma página com dados

String data =""; //Informação a ser recebida pela função de exibição de dados em página HTML

//Função que remove os | por \n para poder exibir os dados em formatação adequada 
String trata_dados(String str) { 
  String str_format, str_var="", str_dados=""; //Strings de formatação
  str_format.reserve(128);  
  int i=0;
  str_var = str.substring(0, str.indexOf(':'));
  str_dados = str.substring(str.indexOf(':') + 1);

 while (i < 4) { //Modelada para 4 pares de dados separados por | e ,
    str_format += str_var.substring(0, str_var.indexOf('|'));
    str_var = str_var.substring(str_var.indexOf('|') + 1, str_var.length());

    str_format += ": ";
    str_format += str_dados.substring(0, str_dados.indexOf(','));
    str_dados = str_dados.substring(str_dados.indexOf(',') + 1, str_dados.length());

    str_format += "<br>"; //Quebra de linha em HTML
    i++;
  }
  return str_format;
}

WiFiServer servidor(80); //Inicializa o servidor na porta 80 (HTML) do ESP

void setup() {
  Serial.begin(9600); //Começa comunicação serial

  WiFi.softAP(ssid, pass); //Inizializa acess point

  IPAddress ip = WiFi.softAPIP();  //Pega IP da página do cliente no acces point

  Serial.println("Endereco IP: ");
  Serial.println(ip); //Exibe o IP do cliente que deve ser digitado no navegador
  servidor.begin();
}

void loop() {
  WiFiClient client = servidor.available(); // Aceita conexões
  if (client) { //Se o cliente se conectar
    Serial.println("Cliente conectado.");
    while (client.connected()) { //Enquanto o cliente estiver conectado 
      if (client.available()) { //Enquanto o cliente estiver disponível para comunicação

        //Código de exibição de dados HTML
        // Envia cabeçalhos HTTP
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:text/html");
        client.println();

        // Envia página HTML com atualização automática
        client.println("<!DOCTYPE html><html>");
        
        client.println("<head><title>Dados fueltech</title>");
        client.println("<meta http-equiv='refresh'>"); //Recarega a página uma única vez
        client.println("<style>"); 
        client.println("body {");
        client.println("  display: flex;");
        client.println("  justify-content: center;");
        client.println("  align-items: center;");
        client.println("  height: 100vh;");
        client.println("  margin: 0;");
        client.println("  font-family: Arial, sans-serif;");
        client.println("  background-color: #f9f9f9;");
        client.println("}");
        client.println(".container {");
        client.println("  border: 4px solid red;");
        client.println("  padding: 40px;");
        client.println("  text-align: center;");
        client.println("  border-radius: 15px;");
        client.println("  background-color: white;");
        client.println("}");
        client.println("h2 {");
        client.println("  color: red;");
        client.println("}");
        client.println("</style>");
        client.println("</head>");
        
        client.println("<body>");
        client.println("<div class='container'>");
        client.println("<h2>Real time data fueltech</h2>");
        client.print("<p>Data received:");

        String str = trata_dados(data); //Faz tratamento dos dados recebidos pela função 
       
        client.print(str);  // Dado recebido enviado a pagina formatado

        client.println("</p>");
        client.println("</div>");
        client.println("</body>");
        
        client.println("</html>");

        //Fim do código de exibição de dados HTML

        delay(10); // Garante que a resposta seja enviada
        client.stop(); // Fecha a conexão HTTP mas não é perdida a conexão WiFi
        Serial.println("Cliente desconectado.");
        break;
      }
    }
  }
}
