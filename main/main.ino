#include "FS.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LinkedList.h>

#define button D3
#define INTERVAL_EVENT "00h00m15s"

String localTime; //Váriavel que armazenara o horario do NTP.
char interval[15] = "00h00m15s"; //Intervalo padrão para a verificação de conexão.
String connectionPublishingSchedule; //Horário para publicar o tópico de verificação de conexão.
boolean flagPublishingSchedule = true; //Flag para impedir que o horário de publicação seja realizado o tempo inteiro.

String eventTime; //Horário que os sensores detectaram um acidente.
String desativationTime; //Horário até que o motorista indique que está bem.

boolean alarmMode = false; //Modo do alarme (false = Acidente | true = Furto).
boolean flagAccident = false; //Flag para indicar que ocorreu um acidente.
boolean flagTheft = false; //Flag para indicar que ocorreu um furto
boolean flagMonitoring = false; //Flag para indicar que os valores dos sensores mudaram.

char character; //Caracteres do terminal.
char terminalInput[35]; //Valores dos sensores obtidos pelo terminal.
char delimiter[] = ","; //Delimitador para os valores dos sensores obtidos pelo terminal.
int j = 0, k = 0; //Variáveis acumuladoras.

/*Sensores: Acelerômetro (A) | Giroscópio (G)*/
String sensor[6]; // (0) -> Ax | (1) -> Ay | (2) -> Az | (3) -> Gx | (4) -> Gy | (5) -> Gz
int Ax, Ay, Az; //Valores do acelerômetro nos três eixos.
int Gx, Gy, Gz; //Valores do giroscópio nos três eixos.

WiFiUDP udp;//Cria um objeto "UDP".
NTPClient ntp(udp, "b.ntp.br", -3 * 3600, 60000);//Cria um objeto "NTP" com as configurações.

/*-- Credenciais do WiFi --*/
const char* ssid = "Santos"; /*Nome da Rede WiFi*/
const char* password = "salmos65"; /*Senha da Rede WiFi*/

/*-- Contatos de emergência previamente cadastrados pelo usuário. --*/
String police = "190";
String ambulance = "192";

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

  if(strcmp(topic, "intervalOutTopic") == 0 || strcmp(topic, "syncIntervalOutTopic") == 0) {
    /*Alocando dinâmicamente a matriz.*/
    data = allocateMatrix(1, 10);
    /*Salvando os dados enviados na publicação na matriz.*/
    data = readPublicationMQTT(payload, length, data);

    Serial.println(data[0]); //Apagar!

    if((strchr(data[0], 'h') != NULL && strchr(data[0], 'm') != NULL && strchr(data[0], 's') != NULL)){
      /*Alterando o valor do intervalo a partir do enviado pela publicação.*/
      strcpy(interval, data[0]);
      /*Mensagem de confimação de que o intervalo foi alterado.*/
      returnMessage("success-interval");
    } else {
      /*Mensagem de erro ao tentar alterar o intervalo.*/
      returnMessage("error-interval");
    }
    
    /*Liberando a matriz alocada dinamicamente.*/
    freeMatrix(data, 10); 
  } else if(strcmp(topic, "alarmOutTopic") == 0 || strcmp(topic, "syncAlarmOutTopic") == 0){
    /*Alocando dinâmicamente a matriz.*/
    data = allocateMatrix(1, 1);
    /*Salvando os dados enviados na publicação na matriz.*/
    data = readPublicationMQTT(payload, length, data);

    if(strchr(data[0], '0') != NULL || strchr(data[0], '1') != NULL){

      if(data[0][0] == '0'){
        Serial.println("Acidente");
        alarmMode = false;
      } else if(data[0][0] == '1') {
        Serial.println("Furto");
        alarmMode = true;
      }
      
      /*Mensagem de confimação de que o alarme foi alterado.*/
      returnMessage("success-alarm");
    } else {
      /*Mensagem de erro ao tentar alterar o alarme.*/
      returnMessage("error-alarm");
    }
   
    /*Liberando a matriz alocada dinamicamente.*/
    freeMatrix(data, 1); 
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
      client.subscribe("intervalOutTopic");
      client.subscribe("syncIntervalOutTopic");
      client.subscribe("alarmOutTopic");
      client.subscribe("syncAlarmOutTopic");
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

  Serial.println(localTime);

  
}

void loop() {
  char message[100]; //Mensagem que será publicada.
  char temp[10]; //Variável temporária para a publicação.
  char *sensorValue; //Valor do sensor.
  int btnVal = digitalRead(button); //Valor do botão.

  /*Horário atual.*/
  localTime = ntp.getFormattedTime();
  
  if (!client.connected()) {
    reconnect();
  }

  /*Recebendo os valores dos sensores via terminal.*/
  if(Serial.available()) {
    character = Serial.read();
    /*Valores dos sensores mudaram.*/
    flagMonitoring = true;

    if(character == 10){ /*Quando chegar no final da String*/
      k = 0;
      /*Dividindo a String.*/
      sensorValue = strtok(terminalInput, delimiter);

      while(sensorValue != NULL){
        sensor[j] = sensorValue;
        sensorValue = strtok(NULL, delimiter);
        j++;
      }

      /*Chamando a função para verificar os valores dos sensores*/
      monitorSensors(sensor[0].toInt(), sensor[1].toInt(), sensor[2].toInt(), sensor[3].toInt(), sensor[4].toInt(), sensor[5].toInt());
      j = 0;
      
      /*Apagando os dados lidos do terminal.*/
      for(int i = 0; i < 25; i++) {
        terminalInput[i] = NULL;
      }
      
    } else { /*Caso não seja o final da String, concatena os caracteres.*/
      terminalInput[k] += character;
      k++;
    }
  }

  /*Salvar o horário para a publicação da verificação de conexão, quando a flag for verdadeira.*/
  if(flagPublishingSchedule){
    connectionPublishingSchedule = (String) getScheduleWithInterval(interval, localTime);
    flagPublishingSchedule = false;
  }

  /*Publicação do tópico de verificação de conexão.*/
  if(localTime == connectionPublishingSchedule){
    delay(100);
    client.publish("connectionInTopic", "{\"status\": true}");

    flagPublishingSchedule = true;
  }

  /*Quando apertar o botão, altera o modo do alarme.*/
  if(btnVal == 0 && !flagAccident && !flagTheft){
    delay(250);
    alarmMode = !alarmMode;
    
    if(alarmMode){
      Serial.println("Alarme no modo furto.");
      client.publish("alarmModeInTopic", "{\"mode\": true}");
    } else {
      Serial.println("Alarme no modo acidente.");
      client.publish("alarmModeInTopic", "{\"mode\": false}");
    }
  }

  /*Caso os sensores detectem um acidente.*/
  if(flagAccident && !alarmMode) {
    /*Emitindo o alarme.*/
    digitalWrite(LED_BUILTIN, LOW);
    delay(50);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(250);

    /*Caso pressione o botão "Estou bem", o alarme é desativado.*/
    if(btnVal == 0){
      delay(250);
      digitalWrite(LED_BUILTIN, HIGH);

      /*Desligando o Led*/
      flagAccident = false;

      /*Publicar um tópico indicando que o usuário pressionou o botão Estou Bem, para salvar no histórico.*/
      strcpy(temp, eventTime.c_str());
      sprintf(message, "{\"event\": \"Pressionou o botão Estou Bem\", \"schedule\": \"%s\"}", temp);
      delay(100);
      client.publish("dailyHistoricInTopic", message); 

      /*Salvando o evento na placa*/
      storageHistoric(temp, "Pressionou o botão Estou Bem");

      /*Leitura do arquivo de histórico diário.*/
      readDailyHistoricFile();

      flagMonitoring = false;
      // Estou bem!
    }

    /*Caso passe o horário permitido para indicar que está bem, a emergência é acionada.*/
    if(localTime == desativationTime){
      flagAccident = false;
      
      digitalWrite(LED_BUILTIN, HIGH);

      /*Publicar um tópico indicando que houve um acidente, para salvar no histórico.*/
      strcpy(temp, eventTime.c_str());
      sprintf(message, "{\"event\": \"Houve um acidente\", \"schedule\": \"%s\"}", temp);
      delay(100);
      client.publish("dailyHistoricInTopic", message);

      /*Salvando o evento na placa*/
      storageHistoric(temp, "Houve um acidente");
      
      //Chamar a emergência!
      Serial.println();
      Serial.print("Ligando para a emergência através do número: ");
      Serial.print(ambulance);
      Serial.println();

      /*Leitura do arquivo de histórico diário.*/
      readDailyHistoricFile();

      flagMonitoring = false;
    }

  }

  /*Caso os sensores detectem um furto.*/
  if(flagTheft && alarmMode) {
    /*Emitindo o alarme.*/
    digitalWrite(LED_BUILTIN, LOW);
    delay(250);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(250);

    /*Caso pressione o botão "Estou bem", o alarme é desativado.*/
    if(btnVal == 0){
      delay(250);
      digitalWrite(LED_BUILTIN, HIGH);

      /*Desligando o Led*/
      flagTheft = false;

      /*Publicar um tópico indicando que o usuário desativou o alrme de furto, para salvar no histórico.*/
      strcpy(temp, eventTime.c_str());
      sprintf(message, "{\"event\": \"Alarme falso para tentativa de furto\", \"schedule\": \"%s\"}", temp);
      delay(100);
      client.publish("dailyHistoricInTopic", message);

      /*Salvando o evento na placa*/
      storageHistoric(temp, "Alarme falso para tentativa de furto");

      /*Leitura do arquivo de histórico diário.*/
      readDailyHistoricFile();

      flagMonitoring = false;
      // Alarme falso!
    }

    /*Caso passe o horário permitido para indicar que foi um alarme falso.*/
    if(localTime == desativationTime){
      flagTheft = false;
      
      digitalWrite(LED_BUILTIN, HIGH);

      /*Publicar um tópico indicando que houve uma tentativa de furto, para salvar no histórico.*/
      strcpy(temp, eventTime.c_str());
      sprintf(message, "{\"event\": \"Houve uma tentativa de furto\", \"schedule\": \"%s\"}", temp);
      delay(100);
      client.publish("dailyHistoricInTopic", message);

      /*Salvando o evento na placa*/
      storageHistoric(temp, "Houve uma tentativa de furto");
      
      //Chamar a emergência!
      Serial.println();
      Serial.print("Ligando para a emergência através do número: ");
      Serial.print(police);
      Serial.println();

      /*Leitura do arquivo de histórico diário.*/
      readDailyHistoricFile();

      flagMonitoring = false;
    }
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
char* getScheduleWithInterval(String data, String localTime){
  char timeEnd[10];
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
    sprintf(timeEnd, "0%d", hourEnd);
  } else {
    sprintf(timeEnd, "%d", hourEnd);
  }

  if(minutesEnd < 10){
    sprintf(timeEnd, "%s:0%d", timeEnd, minutesEnd);
  } else {
    sprintf(timeEnd, "%s:%d", timeEnd, minutesEnd);
  }

  if(secondsEnd < 10){
    sprintf(timeEnd, "%s:0%d", timeEnd, secondsEnd);
  } else {
    sprintf(timeEnd, "%s:%d", timeEnd, secondsEnd);
  }

  return timeEnd;
}

/*-- Função para retornar a mensagem de erro ou sucesso ao publicar pelo serviço web. --*/
void returnMessage(String message){
  delay(100);
  
  if(message == "success-interval")
    client.publish("intervalInTopic", "success");
  else if(message == "error-lamp")
    client.publish("intervalInTopic", "error");
  else if(message == "success-alarm")
    client.publish("alarmInTopic", "success");
  else if(message == "error-alarm")
    client.publish("alarmInTopic", "error");
}

/*-- Função para armazenar os eventos diários na placa --*/
void storageHistoric(String schedule, String event){
  File file;
  String currentDate;
  String fileDate;

  currentDate = getCurrentDate();

  file = SPIFFS.open("dailyHistoric.txt", "r");

  if(file){
    fileDate = file.readStringUntil('\n');
    file.close();
  } else {
    file = SPIFFS.open("dailyHistoric.txt", "w");

    file.printf("%s\n\n", currentDate.c_str());

    file.close();
  }

  if(fileDate == currentDate){
    file = SPIFFS.open("dailyHistoric.txt", "a");

    if(file){
      file.printf("%s - %s\n", schedule.c_str(), event.c_str());

      file.close();
    }
  } else {
    file = SPIFFS.open("dailyHistoric.txt", "w");

    if(file){
      file.printf("%s\n\n", currentDate.c_str());
      file.printf("%s - %s\n", schedule.c_str(), event.c_str());

      file.close();
    }
  }
}

/*-- Função para realizar a leitura do arquivo de histórico diário. --*/
void readDailyHistoricFile(){
  File file;
  String fileData;
  
  file = SPIFFS.open("dailyHistoric.txt", "r");

  if(file){
    Serial.println("------------------ Histórico --------------------");
    Serial.println();
    while(file.available()){
      fileData = file.readStringUntil('\n');
      Serial.println(fileData);
    }
    Serial.println();
    Serial.println("-------------------------------------------------");

    file.close();
  }
}

/*-- Função que retorna a data atual. --*/
String getCurrentDate() {
  /*Retorna número de segundos decorridos desde 1º de janeiro de 1970*/
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime); 
  
  int currentDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  int currentYear = ptm->tm_year+1900;

  String currentDate = String(currentDay) + "/" + String(currentMonth) + "/" +  String(currentYear);
  
  return currentDate;
}

/*-- Função que recebe os valores dos sensores, e verifica se houve um furto ou um acidente. --*/
void monitorSensors(int Ax, int Ay, int Az, int Gx, int Gy, int Gz){
  if(alarmMode){ /*Caso esteja ativado o alarme de furto.*/
      if((Ax < 270 || Ax > 380) && (Ay < 270 || Ay > 380) && (Az < 360) && (Gx >= 10 || Gx <= -10) && (Gy < 0 || Gy >= 5) && flagMonitoring){ /*Caso ocorra uma tentativa de furto.*/
        flagTheft = true;
        eventTime = localTime;
        desativationTime = (String) getScheduleWithInterval(INTERVAL_EVENT, eventTime);
      }
        
  } else { /*Caso esteja ativado o alarme de acidente.*/
    if(Ay < 270 && Az < 360 && Gy > 20 && flagMonitoring){
    flagAccident = true;

    eventTime = localTime;
    desativationTime = (String) getScheduleWithInterval(INTERVAL_EVENT, eventTime);
    
    //Tombou para a esquerda
    } else if(Ay > 380 && Az < 360 && Gy < -20 && flagMonitoring){
      flagAccident = true;

      eventTime = localTime;
      desativationTime = (String) getScheduleWithInterval(INTERVAL_EVENT, eventTime);
  
      //Tombou para a direita
    } else if(Ax > 380 && Az < 360 && Gx > 30 && Gz < 0 && flagMonitoring){
      flagAccident = true;

      eventTime = localTime;
      desativationTime = (String) getScheduleWithInterval(INTERVAL_EVENT, eventTime);
  
      //Tombou para trás
    } else if(Ax < 270 && Az <360 && Gx < -20 && Gz < 0 && flagMonitoring){
      flagAccident = true;

      eventTime = localTime;
      desativationTime = (String) getScheduleWithInterval(INTERVAL_EVENT, eventTime);
  
      //Tombou para frente
    } else if(Az < 300 && Gz < 0 && flagMonitoring){
      flagAccident = true;

      eventTime = localTime;
      desativationTime = (String) getScheduleWithInterval(INTERVAL_EVENT, eventTime);
  
      //Capotado
    } 
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
