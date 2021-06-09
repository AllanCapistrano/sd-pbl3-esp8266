#include "FS.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LinkedList.h>

#define button D3
#define INTERVAL_EVENT "00h00m15s"
#define Acelerometer_RANGE 4 /*Faixa: ± 2g*/
#define Gyroscope_RANGE 500 /*Faixa: ± 250º/s*/

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
boolean isToday; //Flag para indica se o evento está na data atual ou a data anterior à atual.

char character; //Caracteres do terminal.
int j = 0; //Variável acumuladora.

/*Sensores: Acelerômetro (A) | Giroscópio (G)*/
char sensor[6]; // (0) -> Ax | (1) -> Ay | (2) -> Az | (3) -> Gx | (4) -> Gy | (5) -> Gz

WiFiUDP udp;//Cria um objeto "UDP".
NTPClient ntp(udp, "b.ntp.br", -3 * 3600, 60000);//Cria um objeto "NTP" com as configurações.

/*-- Credenciais do WiFi --*/
const char* ssid = "sanbel"; /*Nome da Rede WiFi*/
const char* password = "sanbel09"; /*Senha da Rede WiFi*/

/*-- Contatos de emergência previamente cadastrados pelo usuário. --*/
String police = "190";
String ambulance = "192";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

const char* AWS_endpoint = "a1eb2rhzwlwckg-ats.iot.us-east-1.amazonaws.com"; //Endpoint do dispositivo na AWS.

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
  int btnVal = digitalRead(button); //Valor do botão.

  /*Horário atual.*/
  localTime = ntp.getFormattedTime();
  
  if (!client.connected()) {
    reconnect();
  }

  /*Recebendo os valores dos sensores via terminal.*/
  if(Serial.available()) {
    
    character = Serial.read();
    sensor[j] = character;
    j++;

    /* Após todos os caracteres. */
    if(j == 6){
      /*Chamando a função para verificar os valores dos sensores*/
      monitorSensors(sensor[0], sensor[1], sensor[2], sensor[3], sensor[4], sensor[5]);
      j = 0;  
    }
    
    /*Valores dos sensores mudaram.*/
    flagMonitoring = true;
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
  String currentDate; /*Data atual.*/
  char *splitData; /*Utilizada para separar os dados que estão no arquivo por um delimitador.*/
  LinkedList<String> dateList = LinkedList<String>(); /*Lista com as datas.*/
  LinkedList<String> scheduleList = LinkedList<String>(); /*Lista com os horários.*/
  LinkedList<String> eventList = LinkedList<String>(); /*Lista com os eventos.*/
  LinkedList<int> indexes = LinkedList<int>(); /*Lista com os índices que dos eventos válidos.*/
  int j = 0; /*Variável acumuladora.*/
  char delimit[] = "-"; /*Delimitador que irá separar os dados.*/
  char fileData[100]; /*Linha lida do arquivo.*/
  

  /*Obtendo a data atual.*/
  currentDate = getCurrentDate();

  file = SPIFFS.open("dailyHistoric.txt", "r");

  if(file){
    while(file.available()){
      /*Lendo a linha do arquivo e convertendo para um vetor de caracteres.*/
      file.readStringUntil('\n').toCharArray(fileData, 100);
      /*Dividindo a String.*/
      splitData = strtok(fileData, delimit);
      /*Adicionando cada dado lido na sua lista correspondente.*/
      while(splitData != NULL){
        if(j == 0){
          dateList.add(splitData); 
        } else if(j == 1){
          scheduleList.add(splitData);
        } else if(j == 2){
          eventList.add(splitData);
        }

        splitData = strtok(NULL, delimit);
        j++;
      }
      j = 0;
    }
    
    file.close();

    /*Adicionando os dados do evento atual nas suas listas correspondente.*/
    dateList.add(currentDate);
    scheduleList.add(schedule);
    eventList.add(event);
    
    file = SPIFFS.open("dailyHistoric.txt", "w");

    for(int i = 0; i < eventList.size(); i++){
      /*Removendo os dados que ultrapassaram o limite de 24 horas.*/
      if(compareDate(currentDate, dateList.get(i)) == true){
        if(isToday && schedule > scheduleList.get(i)){
          indexes.add(i);
        } else if(isToday && schedule <= scheduleList.get(i)){
          indexes.add(i);
        } else if(!isToday && schedule <= scheduleList.get(i)){
          indexes.add(i);
        }
      }
      
    }
    /*Escrevendo os dados das listas no arquivo.*/
    for(int i = 0; i < indexes.size(); i++){
      file.printf("%s-%s-%s\n", dateList.get(indexes.get(i)).c_str(), scheduleList.get(indexes.get(i)).c_str(), eventList.get(indexes.get(i)).c_str());
    }

    file.close();
  } else { /*Caso o arquivo não exista, ele é criado com os dados do evento atual.*/
    file = SPIFFS.open("dailyHistoric.txt", "w");

    file.printf("%s-%s-%s\n", currentDate.c_str(), schedule.c_str(), event.c_str());

    file.close();
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
  epochTime -= 10800; /*Adequando o fuso horário para UTC-3.*/

  struct tm *ptm = gmtime ((time_t *)&epochTime); 
  
  int currentDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  int currentYear = ptm->tm_year+1900;

  String currentDate = String(currentDay) + "/" + String(currentMonth) + "/" +  String(currentYear);
  
  return currentDate;
}

/*-- Função que recebe os valores dos sensores, e verifica se houve um furto ou um acidente. --*/
void monitorSensors(char Ax, char Ay, char Az, char Gx, char Gy, char Gz){
  int tempAccel[3]; /*Variável temporária para os valores dos sensores do acelerômetro.*/
  int tempGyrosc[3]; /*Variável temporária para os valores dos sensores do giroscópio.*/
  float Accel[3]; /*Valores dos sensores do acelerômetro representados em 8 bits.*/
  float Gyrosc[3]; /*Valores dos sensores do giroscópio representados em 8 bits.*/
  float AcelerometerSum; /*Soma dos 3 valores do acelerômetro.*/

  /*Convertendo os valores para base 10.*/
  tempAccel[0] = String(Ax, DEC).toInt();
  
  tempAccel[1] = String(Ay, DEC).toInt();
  
  tempAccel[2] = String(Az, DEC).toInt();
 
  tempGyrosc[0] = String(Gx, DEC).toInt();
 
  tempGyrosc[1] = String(Gy, DEC).toInt();
  
  tempGyrosc[2] = String(Gz, DEC).toInt();
  

  /*Atribuindo os valores convertidos da base 10 para uma representação de 8 bits.*/
  for(int x = 0; x < 3; x++){
    Accel[x] = convertToScaleAcelerometer(tempAccel[x]);
    Gyrosc[x] = convertToScaleGyroscope(tempGyrosc[x]);
  }

  if(alarmMode){ /*Caso esteja ativado o alarme de furto.*/
      if((Gyrosc[0] > 15.625 || Gyrosc[0] < -9.765625) || (Gyrosc[1] > 9.765625 || Gyrosc[1] < -9.765625) || (Gyrosc[2] > 99.609375 || Gyrosc[2] < 80.078125) || 
      (Accel[0] < -0.125 || Accel[0] > 0.125) || (Accel[1] < -0.125 || Accel[1] > 0.125) || (Accel[2] > 1.125  || Accel[2] < 0.875) && flagMonitoring){
        flagTheft = true;
        
        eventTime = localTime;
        desativationTime = (String) getScheduleWithInterval(INTERVAL_EVENT, eventTime);

      }    
  } else { /*Caso esteja ativado o alarme de acidente.*/
      AcelerometerSum = Accel[0] + Accel[1] + Accel[2];

      /*Caso o giroscópio identifique alguma variação brusca em algum dos eixos.*/
      if((Gyrosc[0] >= 46.875 || Gyrosc[0] <= -46.875 || Gyrosc[1] <= -50.78125 || Gyrosc[1] >= 70.3125 || Gyrosc[2] < 0) && flagMonitoring){
          flagAccident = true;

          eventTime = localTime;
          desativationTime = (String) getScheduleWithInterval(INTERVAL_EVENT, eventTime);
      }
      /*Caso o acelerômetro identifique alguma variação brusca em algum dos eixos.*/
      if((AcelerometerSum < -0.786944 || AcelerometerSum > 0.944182) && flagMonitoring){
          flagAccident = true;

          eventTime = localTime;
          desativationTime = (String) getScheduleWithInterval(INTERVAL_EVENT, eventTime);
      }
  }

}

/*Função que converte os valores do acelerômetro de base 10 em uma representação de 8 bits.*/
/*Faixa: ± 2g*/
float convertToScaleAcelerometer(int number){
  float delta = (float) Acelerometer_RANGE/256;
  float conversion = (number - 127) * delta;
  return conversion;
}

/*Função que converte os valores do giroscópio de base 10 em uma representação de 8 bits.*/
/*Faixa: ± 250°/s*/
float convertToScaleGyroscope(int number){
  float delta = (float) Gyroscope_RANGE/256;
  float conversion = (number - 127) * delta;
  return conversion;
}

/*Função que compara a diferença entre as datas, retornando true quando a diferença for 0 ou 1 dias.*/
boolean compareDate(String currentDate,String eventDate){
  int dayDiff, monthDiff, yearDiff;
  String temp = "",temp2 = "";
  int bar[2], bar2[2]; 
  int x = 0, index = 0;

  while(x < 2){
    if(currentDate[index] == '/'){
      bar[x] = index;
      x++;
    }
    index++;
  }

  x = 0;
  index = 0;
  while(x < 2){
    if(eventDate[index] == '/'){
      bar2[x] = index;
      x++;
    }
    index++;
  }

  if(bar2[1] == 3){ //   x/x/xxxx
    temp2 += eventDate[4];
    temp2 += eventDate[5]; 
    temp2 += eventDate[6];
    temp2 += eventDate[7]; 
  } else if(bar2[1] == 4 && (bar2[0] == 2 || bar2[0] == 1)){ //  xx/x/xxxx || x/xx/xxxx
    temp2 += eventDate[5];
    temp2 += eventDate[6]; 
    temp2 += eventDate[7];
    temp2 += eventDate[8]; 

  } else if(bar2[1] == 5 && bar2[0] == 2){  //  xx/xx/xxxx
    temp2 += eventDate[6];
    temp2 += eventDate[7]; 
    temp2 += eventDate[8];
    temp2 += eventDate[9];
  }
  
  if(bar[1] == 3){ //   x/x/xxxx
    temp += currentDate[4];
    temp += currentDate[5]; 
    temp += currentDate[6];
    temp += currentDate[7]; 
  } else if(bar[1] == 4 && (bar[0] == 2 || bar[0] == 1)){ //  xx/x/xxxx || x/xx/xxxx
    temp += currentDate[5];
    temp += currentDate[6]; 
    temp += currentDate[7];
    temp += currentDate[8]; 

  } else if(bar[1] == 5 && bar[0] == 2){  //  xx/xx/xxxx
    temp += currentDate[6];
    temp += currentDate[7]; 
    temp += currentDate[8];
    temp += currentDate[9];
  }
  
  yearDiff = temp.toInt() - temp2.toInt();
  if(yearDiff != 0){
    return false;
  }
  temp = "";
  temp2 = "";

  if(bar2[1] == 3){ //   x/x/xxxx
    temp2 += eventDate[2];
  } else if(bar2[1] == 4 && bar2[0] == 2){ //  xx/x/xxxx
    temp2 += eventDate[3];
  } else if(bar2[1] == 4 && bar2[0] == 1){ //  x/xx/xxxx
    temp2 += eventDate[2];
    temp2 += eventDate[3];
  } else if(bar2[1] == 5 && bar2[0] == 2){  //  xx/xx/xxxx
    temp2 += eventDate[3];
    temp2 += eventDate[4];
  }

  if(bar[1] == 3){ //   x/x/xxxx
    temp += currentDate[2];
  } else if(bar[1] == 4 && bar[0] == 2){ //  xx/x/xxxx
    temp += currentDate[3];
  } else if(bar[1] == 4 && bar[0] == 1){ //  x/xx/xxxx
    temp += currentDate[2];
    temp += currentDate[3];
  } else if(bar[1] == 5 && bar[0] == 2){  //  xx/xx/xxxx
    temp += currentDate[3];
    temp += currentDate[4];
  }
  
  monthDiff = temp.toInt() - temp2.toInt();
  if(monthDiff != 0){
    return false;
  }
  temp = "";
  temp2 = "";

  if(bar2[0] == 1){
    temp2 += eventDate[0];
  } else{
    temp2 += eventDate[0];
    temp2 += eventDate[1];
  } 
  
  if(bar[0] == 1){
    temp += currentDate[0];
  } else{
    temp += currentDate[0];
    temp += currentDate[1];
  } 

  dayDiff = temp.toInt() - temp2.toInt();
  if(dayDiff > 1 || dayDiff < 0){
    return false;
  }

  if(dayDiff == 0){
    isToday = true;
  } else if(dayDiff == 1){
    isToday = false;
  }
   
  return true;
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
