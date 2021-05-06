#include "FS.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LinkedList.h>

#define button D3

String localTime;//Váriavel que armazenara o horario do NTP.
char interval[15] = "00h00m15s,"; //
char connectionPublishingSchedule[10]; //
boolean flagPublishingSchedule = true; //

WiFiUDP udp;//Cria um objeto "UDP".
NTPClient ntp(udp, "b.ntp.br", -3 * 3600, 60000);//Cria um objeto "NTP" com as configurações.

/*-- Credenciais do WiFi --*/
const char* ssid = "Santos"; /*Nome da Rede WiFi*/
const char* password = "salmos65"; /*Senha da Rede WiFi*/

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

const char* AWS_endpoint = "agujconwx0aiu-ats.iot.us-east-1.amazonaws.com"; //Endpoint do dispositivo na AWS.

/*-- Função responsável pela comunicação da AWS IoT Core com a placa ESP8266 --*/
void callback(char* topic, byte* payload, unsigned int length) {
  char **data; /*Matriz para armazenar cada um dos dados enviados na publicação.*/

  Serial.println("");
  Serial.print("Tópico [");
  Serial.print(topic);
  Serial.print("] ");

  /*-- Exibir a mensagem publicada no Monitor Serial --*/
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }

  if(strcmp(topic, "Interval") == 0) {
    /*Alocando dinâmicamente a matriz.*/
    data = allocateMatrix(1, 10);
    /*Salvando os dados enviados na publicação na matriz.*/
    data = readPublicationMQTT(payload, length, data);

    Serial.println(data[0]); //Apagar!

    /*Alterando o valor do intervalo a partir do enviado pela publicação.*/
    strcpy(interval, data[0]);
    
    /*Liberando a matriz alocada dinamicamente.*/
    freeMatrix(data, 10); 
  } else {
    Serial.println("Erro! Tópico não encontrado.");
  }

}

WiFiClientSecure espClient;
PubSubClient client(AWS_endpoint, 8883, callback, espClient); //Realizando a comunicação MQTT da placa com a AWS, através da porta 8883

/*-- Função para realizar a conexão à rede WiFI. --*/
void setup_wifi() {
  delay(10);
  espClient.setBufferSizes(512, 512);
  Serial.println();
  Serial.print("Conectando em: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado com sucesso!");

  timeClient.begin();
  while(!timeClient.update()){
    timeClient.forceUpdate();
  }

  espClient.setX509Time(timeClient.getEpochTime());
}

/*-- Função para reconectar com o protocolo MQTT. --*/
void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentativa de conexão MQTT...");
    
    /*Tentativa de conexão MQTT*/
    if (client.connect("ESPthing")) {
      Serial.println("Conectado!");
      client.subscribe("Interval");
    } else {
      Serial.print("Falhou! Erro:");
      Serial.print(client.state());
      Serial.println(" Tentando novamente em 5 segundos");

      /*Verificação do certificado SSL*/
      char buf[256];
      espClient.getLastSSLError(buf,256);
      Serial.print("SSL erro: ");
      Serial.println(buf);

      /* Esperar 5 segundos antes de tentar novamente. */
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  Serial.setDebugOutput(true);
  
  /* Definindo o LED da placa como Output. */
  pinMode(LED_BUILTIN, OUTPUT);

  /* Definindo o botão da placa como Input. */
  pinMode(button, INPUT_PULLUP);
  
  setup_wifi();
  
  delay(1000);
  
  ntp.begin();
  ntp.forceUpdate();//Força o Update.
  
  if (!SPIFFS.begin()) {
    Serial.println("Falha na montagem do arquivo de sistema");
    return;
  }

  /*Iniciando a lâmpada como desligada.*/
  digitalWrite(LED_BUILTIN, HIGH);

  /*-- Carregando os certificados na placa --*/
  loadCertificates();

  localTime = ntp.getFormattedTime();
  
}

void loop() {
  char message[40];
  int btnVal = digitalRead(button);
  localTime = ntp.getFormattedTime();
  
  if (!client.connected()) {
    reconnect();
  }

  if(flagPublishingSchedule){
    setConnectionPublishingSchedule(interval, localTime);
    flagPublishingSchedule = false;
  }

  if(localTime == connectionPublishingSchedule){
    /*Formantando a mensagem do tópico.*/
    sprintf(message, "{\"interval\": %s, \"status\": true,}", interval);
    
    delay(100);
    client.publish("Connection", message);

    flagPublishingSchedule = true;
  }

  client.loop();
}

/*-- Função para leitura dos dados que foram publicados pelo protocolo MQTT. --*/
char **readPublicationMQTT(byte* payload, unsigned int length, char** response){
  int j = 0, l = 0, k = 0;
  
  for (int i = 0; i < length; i++) {
    if((char)payload[i] == ':'){
      k = i + 2;
      l = 0;
      
      while((char)payload[k] != ','){
        response[j][l] = (char)payload[k];
        k++;
        l++;
      }  
      j++;
    }
  }
  
  return response;
}

/*-- Função para definir o horário que será publicado o tópico para verificar a conexão. --*/
void setConnectionPublishingSchedule(String data, String localTime){
  String interval;
  String temp = "", temp2 = "";
  int intervalSecondsInt, localTimeSecondsInt;
  int intervalMinutesInt, localTimeMinutesInt;
  int intervalHourInt, localTimeHourInt;
  int hourEnd = 0, minutesEnd = 0, secondsEnd = 0;
  
  interval = data;

  interval.replace("h", ":");
  interval.replace("m", ":");
  interval.remove(8, 1);

  temp += interval[6];
  temp += interval[7];

  temp2 += localTime[6];
  temp2 += localTime[7];
  
  intervalSecondsInt = temp.toInt();
  localTimeSecondsInt = temp2.toInt();

  temp = "";
  temp2 = "";

  temp = interval[3];
  temp += interval[4];

  temp2 = localTime[3];
  temp2 += localTime[4];
  
  intervalMinutesInt = temp.toInt();
  localTimeMinutesInt = temp2.toInt();

  temp = "";
  temp2 = "";

  temp = interval[0];
  temp += interval[1];

  temp2 = localTime[0];
  temp2 += localTime[1];
  
  intervalHourInt = temp.toInt();
  localTimeHourInt = temp2.toInt();

  secondsEnd = intervalSecondsInt + localTimeSecondsInt;
  
  if (secondsEnd > 59){
    minutesEnd = 1;
    secondsEnd = secondsEnd - 60;
  }

  minutesEnd = minutesEnd + intervalMinutesInt + localTimeMinutesInt;

  if (minutesEnd > 59){
    hourEnd = 1;
    minutesEnd = minutesEnd - 60;
  }

  hourEnd = hourEnd + intervalHourInt + localTimeHourInt;

  if (hourEnd > 23){
    hourEnd = hourEnd - 24;
  }

  if(hourEnd < 10){
    sprintf(connectionPublishingSchedule, "0%d", hourEnd);
  } else {
    sprintf(connectionPublishingSchedule, "%d", hourEnd);
  }

  if(minutesEnd < 10){
    sprintf(connectionPublishingSchedule, "%s:0%d", connectionPublishingSchedule, minutesEnd);
  } else {
    sprintf(connectionPublishingSchedule, "%s:%d", connectionPublishingSchedule, minutesEnd);
  }

  if(secondsEnd < 10){
    sprintf(connectionPublishingSchedule, "%s:0%d", connectionPublishingSchedule, secondsEnd);
  } else {
    sprintf(connectionPublishingSchedule, "%s:%d", connectionPublishingSchedule, secondsEnd);
  }
}

/*-- Função para alocar dinamicamente o tamanho da matriz --*/
char **allocateMatrix(int row, int col){
  char **matrix;
  int i, j;

  matrix = (char**)malloc(sizeof(char*) * row);
  
  for(i = 0; i < col; i++){
    matrix[i] = (char *)malloc(sizeof(char) * col);
  }

  for (i = 0; i < row; i++){
    for(j = 0; j < col; j++){
      matrix[i][j] = '\0';
    }
  }
  
  return matrix;
}

/*-- Função para liberar o espaço na memória que foi alocado dinamicamente --*/
void freeMatrix(char **matrix, int col){
  int i;
  
  for(i = 0; i < col; i++)
    free(matrix[i]);
    
  free(matrix);
}

/*-- Função para abrir e carregar todos os certificados. --*/
void loadCertificates() {
  /*-- Carregando cert.der --*/
  File cert = SPIFFS.open("/cert.der", "r"); 
  
  if (!cert)
    Serial.println("Falha ao tentar abrir o arquivo cert.der");
  else
    Serial.println("Sucesso ao abrir o arquivo cert.der");

  delay(1000);

  if (espClient.loadCertificate(cert))
    Serial.println("Sucesso ao carregar arquivo cert.der");
  else
    Serial.println("Erro ao carregar arquivo cert.der");

  /*-- Carregando private.der --*/
  File private_key = SPIFFS.open("/private.der", "r");
  
  if (!private_key)
    Serial.println("Falha ao tentar abrir o arquivo private.der");
  else
    Serial.println("Sucesso ao abrir o arquivo private.der");

  delay(1000);

  if (espClient.loadPrivateKey(private_key))
    Serial.println("Sucesso ao carregar arquivo private.der");
  else
    Serial.println("Erro ao carregar arquivo private.der");
    
  /*-- Carregando car.der --*/
  File ca = SPIFFS.open("/ca.der", "r");
  
  if (!ca)
    Serial.println("Falha ao tentar abrir o arquivo ca.der");
  else
    Serial.println("Sucesso ao abrir o arquivo ca.der");

  delay(1000);

  if(espClient.loadCACert(ca))
   Serial.println("Sucesso ao carregar arquivo ca.der");
  else
    Serial.println("Erro ao carregar arquivo ca.der");
}
