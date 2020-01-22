#define INCLUDE_vTaskDelete    1
#define INCLUDE_vTaskSuspend   1
#define configTICK_RATE_HZ (5)

#include "FS.h"
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include "stdlib.h"
#include "time.h"

// wartosci pulsu i gsr są wspolne
unsigned long int GSRValue = 0;
unsigned int heartRate = 0;

// komendy kontrolne
const char* stop_string = "stop";
const char* start_string = "start";

//zmienne do raportowania o błędzie
bool TimingErrorCore0 = false;
bool TimingErrorCore1 = false;
unsigned int erroroperationnumbercore0 = 0;
unsigned int erroroperationnumbercore1 = 0;

//komendy sterujące
bool WiFiBegin = false;
bool FileClosed = false;

// zmienne kontrolne do sterowania 
bool measure_enabled = false;
bool space = false;

// bufory na dane serwera
char wifiname[41];
char wifipass[41];
char mqttserver[41];
char username[41];
char password[41];
char clientid[37];

//zmienne klienta WiFi i klienta mqtt
WiFiClient espClient;
PubSubClient client(espClient);

//do przechowywania fragmentów wiadomosci
long lastMsg = 0;
char conVal[20]; //bufor na wartość kontrolną
char conValForFile[21];// bufor na wartość numeru wrzucaną do pliku blędu
char sendVal[20]; //bufor na wartość pomiaru
char opValErr[20];
//char coreidbuffer[20];

char Voltagemsg[120]; // bufory na wiadomosci koncowe, wspólne dla obu procesów
char HRmsg[120];
char GSRmsg[120];
char Errmsg[120];

//blokady do wpisu - chodzi o to, by nie było możliwości wysyłki w trakcie wpisywania do buforów, 
bool VoltagemsgLock = false;
bool HRmsgLock = false;
bool GSRmsgLock = false;

// do kontroli wysłania danych - chodi o to, by nie wpisywać dwa razy tej samej wiadomosci
bool VoltageSent = false;
bool HRSent = false;
bool GSRSent = false;

//znaczniki zadań z FREERtos
TaskHandle_t DataAcquisition;
TaskHandle_t ServerConnection;
TaskHandle_t Main;

// znaczniki plików od GSR, HR i logu błędów systemu
File HRFile;
File GSRFile;
File ErrorFile;

// dane serwera NTP
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

// zmienne do przechowywania czasu
unsigned int seconds;
unsigned int minutes;
unsigned int hours;

// bufory na wartosci czasu
char secbuf[10];
char minbuf[10];
char hourbuf[10];
char timebuf[31];

void LocalTime()
{
  struct tm timeinfo;
  
  if(getLocalTime(&timeinfo))
  {
     seconds = timeinfo.tm_sec;
     minutes = timeinfo.tm_min;
     hours = timeinfo.tm_hour;
     
  }
  else
  {
    seconds = seconds + 4;
    if(seconds >= 60)
    {
      seconds = seconds - 60;
      minutes = minutes + 1;
    }

    if(minutes >= 60)
    {
      minutes = minutes - 60;
      hours = hours + 1;
    }

    if(hours >= 24)
    {
      hours = 0;
    }
  }
  return;
}


File openFile(fs::FS &fs, const char * path, unsigned int filemode) // do otwarcia pliku
{
    File plik;
    //Serial.println("Opening file for reading");

    if(filemode == 1)
    {
       plik = fs.open(path, FILE_WRITE);
    }
    else if(filemode == 2)
    {
      plik = fs.open(path, FILE_APPEND);
    }
    else
    {
      plik = fs.open(path, FILE_READ);
    }
   
    if(plik)
    {
      return plik;
    }
    else
    {
      Serial.println("opening file failed!");
      return plik;
    }
}

void writeHeaderToFile(File& plik, const char* columnname1, const char* columnname2, const char* columnname3, const char* columnname4)
{
  if(!plik)
  {
    Serial.println("File not found!");
    return;
  }

  if(plik.print(columnname1))
  {
     Serial.println("column 1 name written");
  } 
  else
  {
     Serial.println("column 1 name write failed");
  }

  if(plik.print(columnname2))
  {
    Serial.println("column 2 name written");
  }
  else
  {
    Serial.println("column 2 name write failed");
  }

  if(plik.print(columnname3))
  {
    Serial.println("column 3 name written");
  }
  else
  {
    Serial.println("column 3 name write failed");
  }

  if(plik.println(columnname4))
  {
    Serial.println("column 4 name written");
  }
  else
  {
    Serial.println("column 4 name write failed");
  }
  
  return;
}


void writeHeaderToFile(File& plik, const char* columnname1, const char* columnname2, const char* columnname3)
{
  if(!plik)
  {
    Serial.println("File not found!");
    return;
  }

  if(plik.print(columnname1))
  {
     Serial.println("column 1 name written");
  } 
  else
  {
     Serial.println("column 1 name write failed");
  }

  if(plik.print(columnname2))
  {
    Serial.println("column 2 name written");
  }
  else
  {
    Serial.println("column 2 name write failed");
  }

  if(plik.println(columnname3))
  {
    Serial.println("column 3 name written");
  }
  else
  {
    Serial.println("column 3 name write failed");
  }
  
  return;
}

void writeDataToFile(File& plik, char* string)
{
  if(!plik)
  {
    Serial.println("File not found!");
    return;
  }

  if(plik.println(string))
  {
     // tu zapis danych
  } 
  else
  {
    Serial.println("string not written");
  }
  
  return;
}


void closeFile(File& plik)
{
  if(!plik)
  {
    return;
  }

  plik.close();
  return;
}


void writeDataIntoDedicatedBuffer(unsigned int buffer_number, int counter, char sign_to_write) // do zapisu danych z karty SD w odpowiednie bufory
{
  if(buffer_number > 6 && buffer_number == 0)
  {
    Serial.println("Znak nie zostanie wpisany do żadnego z buforów!");
    return;
  }

  Serial.println(sign_to_write);
 
  if(buffer_number == 1)
  {
    wifiname[counter] = sign_to_write;
    return;
  }
  else if(buffer_number == 2)
  {
    wifipass[counter] = sign_to_write;
    return;
  }
  else if(buffer_number == 3)
  {
    mqttserver[counter] = sign_to_write;
    return;
  }
  else if(buffer_number == 4)
  {
    username[counter] = sign_to_write;
    return;
  }
  else if(buffer_number == 5)
  {
    password[counter] = sign_to_write;
    return;
  }
  else if(buffer_number == 6)
  {
    clientid[counter] = sign_to_write;
    return;
  }
}

void readDataFromFile(fs::FS &fs, const char * path) // do odczytu danych z karty SD
{
    char sign;
    int counter = 0;
    unsigned int buffer_number = 1;
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    
    if(!file)
    {
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.print("Read from file: ");
    while(file.available())
    {
       Serial.println(counter);
       sign = file.read();
       if((int)sign != 32 && (int)sign != 13 && (int)sign != 9)
       {
          space = false;
          writeDataIntoDedicatedBuffer(buffer_number, counter, sign);
          counter++;
       }
       else if(((int)sign == 32 || (int)sign == 13 || (int)sign == 9) && space == false)
       {
          space = true;
          writeDataIntoDedicatedBuffer(buffer_number, counter, NULL);
          counter = 0;
          buffer_number++;
       }
       else if(((int)sign == 32 || (int)sign == 13 || (int)sign == 9) && space == true)
       {
          continue;
       }
    }
    
    writeDataIntoDedicatedBuffer(buffer_number, counter, NULL);
    file.close();
    
    Serial.print("Nazwa wifi: ");
    Serial.println(wifiname);
    Serial.println(strlen(wifiname));
    Serial.print("Haslo wifi: ");
    Serial.println(wifipass);
    Serial.println(strlen(wifipass));
    Serial.print("Serwer: ");
    Serial.println(mqttserver);
    Serial.println(strlen(mqttserver));
    Serial.print("Nazwa uzytkownika: ");
    Serial.println(username);
    Serial.println(strlen(username));
    Serial.print("Haslo uzytkownika: ");
    Serial.println(password);
    Serial.println(strlen(password));
    Serial.print("Identyfikator klienta: ");
    Serial.println(clientid);
    Serial.println(strlen(clientid));
    return;
}

void setup_wifi() 
{
  Serial.println();
  Serial.print("Establishing connection with: ");
  Serial.println(wifiname);

  WiFi.begin(wifiname, wifipass);
  WiFiBegin = true;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}


void callback(char* topic, byte* payload, unsigned int length)
{
  char message_buffer[length];
  Serial.print("MQTT message on topic[");
  Serial.print(topic);
  Serial.print("]");
  for (int i = 0; i < length; i++)
  {
    message_buffer[i] = (char)payload[i];
    Serial.print((char)payload[i]);
   
  }
  message_buffer[length] = '\0';
  Serial.println();

  
  if(strcmp(message_buffer, stop_string) == 0)
  {
    Serial.println("stop message has come");
    measure_enabled = false;
  }  
  else if(strcmp(message_buffer, start_string) == 0)
  {
      measure_enabled = true;
  }
  else
  {
    Serial.println("message normal");
  }

}

void reconnect() 
{
    if(client.connect(clientid, username, password))
    {
      //ponowna subskrybcja po połączeniu
      client.subscribe("inTopic");
    }  
}


void waitTo10ms(unsigned long int timebegin, unsigned int loopcounter)
{
  unsigned long int current_time;
  unsigned long int time_of_operation;
  current_time = micros();
  
  if(current_time > timebegin)
  {
    time_of_operation = current_time - timebegin;
    if(time_of_operation < 10000)
    {
      delayMicroseconds(10000 - time_of_operation);
    }
    else
    {
      Serial.println("over 10ms!");
      if(xPortGetCoreID() == 0)
      {
        erroroperationnumbercore0=loopcounter;
        TimingErrorCore0 = true;
      }
      else
      {
        erroroperationnumbercore1=loopcounter;
        TimingErrorCore1 = true;
      } 
    }
  }
  else 
  {  
    time_of_operation = current_time + (4294967295 - timebegin);
    if(time_of_operation < 10000)
    {
      delayMicroseconds(10000 - time_of_operation);
    }
    else
    {
      if(xPortGetCoreID() == 0)
      {
        erroroperationnumbercore0=loopcounter;
        TimingErrorCore0 = true;
      }
      else
      {
        erroroperationnumbercore1=loopcounter;
        TimingErrorCore1 = true;
      } 
    }
  }
}

void MeasureSetup()
{
  Serial.begin(115200);
  SPI.begin(18, 19, 23);

  pinMode(16, OUTPUT);
  pinMode(4, INPUT_PULLUP);
   
  while(!SD.begin(5))
  {
     Serial.println("Card Mount Failed");
     delay(100);
  }    
  readDataFromFile(SD, "/name.txt");

  setup_wifi();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); 
  
  HRFile = openFile(SD, "/HR.csv", 1);
  delay(100);
  writeHeaderToFile(HRFile, "numer;", "czas;", "wartosc");
  delay(100);
  //closeFile(HRFile);

  delay(200);

  GSRFile = openFile(SD, "/GSR.csv", 1);
  delay(100);
  writeHeaderToFile(GSRFile, "numer;", "czas;", "wartosc");
  delay(100);
  //closeFile(GSRFile);

  delay(200);
  ErrorFile = openFile(SD, "/Err.csv", 1);
  delay(100);
  writeHeaderToFile(ErrorFile, "numer pomiaru;", "czas;", "numer operacji;", "rdzen");
  delay(100);
  //closeFile(ErrorFile); 

  client.setServer(mqttserver, 1883);
  client.setCallback(callback);
  client.connect(clientid, username, password);
  Serial.println(client.connected());

  digitalWrite(16, HIGH);
  client.subscribe("inTopic");
  client.loop();
  
  
  while(!measure_enabled)
  {
      Serial.println(client.connected());
      Serial.println("waiting");

      client.subscribe("inTopic");
      client.loop();
      delay(1000);
  }

  Serial.println(measure_enabled);

}


void DataGathering( void * pvParameters )
{ 

  // zmienne do pomiaru pulsu oraz GSR
  int how_many_growing_samples = 0; //zliczanie próbek rosnących
  unsigned int heartRate; //puls
  bool slope = false; // wykrycie zbocza
  unsigned int peak_counter; // ilość szczytów na zboczu
  int local_max = 0; // ilosc lokalnych maksimow
  unsigned long int first_peak_time;//czas pierwszego szczytu
  unsigned long int next_peak_time; // czas kolejnego szczytu
  unsigned long int all_peaks_time; // czas wszystkich szczytów
  int heartRateSensorValue = 0; // wartość próbki z sensora
  int previousHeartRateSensorValue = 0; // wartość poprzedniej próbki z sensora
  unsigned int GSRsensorValue = 0; // wartość próbki z sensora GSR
  unsigned long int GSRBuffer = 0; // warto
  unsigned long int GSRAverage = 0;
  unsigned long int GSRValue = 0;

  //zmienne do pomiaru napięcia
  unsigned int VoltageSample = 0;
  double Voltage = 8; // domyślne napięcie znacznie powyżej progu odcięcia
  unsigned long int VoltageBuffer = 0;
  unsigned long int VoltageAverage = 0;

  unsigned int controlValue = 0; // do przechowywania wartosci kontrolnej w wiadomosci

  unsigned long int time_begin_DataGathering = 0;
  unsigned int LoopCounterDataGathering = 1;


  for(;;)
  {
    while(measure_enabled)
    {
      time_begin_DataGathering = micros();
    
      heartRateSensorValue = analogRead(34); //read the input on analog pin 35

      if(LoopCounterDataGathering % 10 == 0) // do odczytu czujnika GSR i napięcia 
      {
        GSRsensorValue = analogRead(35);
        GSRBuffer += GSRsensorValue;

        VoltageSample = analogRead(32);
        VoltageBuffer += VoltageSample;
      }

      if((heartRateSensorValue-previousHeartRateSensorValue) >= 30 && slope == false) // do wykrywania zbocza narastającego
      {
        ++how_many_growing_samples;
      }
      else if((heartRateSensorValue-previousHeartRateSensorValue) < 30 && slope == false)
      {
        how_many_growing_samples = 0;
      }

      if(how_many_growing_samples == 5) // do oznaczenia zbocza narastającego
      {
        //Serial.println("wykryto zbocze narastajace");
        slope = true;
        how_many_growing_samples = 0;
      }

      if((heartRateSensorValue - previousHeartRateSensorValue) >= 0 && slope == true) // do wykrycia maksimum lokalnego
      {
        local_max = heartRateSensorValue;
      }

      if(heartRateSensorValue < local_max && slope == true)
      {
        slope = false;
        ++peak_counter;
        if(peak_counter == 1)
        {
          first_peak_time = micros();
          all_peaks_time = first_peak_time;
        }
        else
        { 
          next_peak_time = micros() - first_peak_time;
          all_peaks_time = next_peak_time;
        }
      }
 
      previousHeartRateSensorValue = heartRateSensorValue;

      if(digitalRead(4) == LOW)
      {
        Serial.println("wyjscie z taska 0");
        measure_enabled = false;
      }

      if(LoopCounterDataGathering == 1) // budowa informacji o czasie
      {
        LocalTime();
        itoa(hours, hourbuf, 10);
        itoa(minutes, minbuf, 10);
        itoa(seconds, secbuf, 10);
        if(hours < 10)
        {
          strcat(timebuf, "0");
        }
        strcat(timebuf, hourbuf);
        strcat(timebuf, ":");

        if(minutes < 10)
        {
          strcat(timebuf, "0");
        }
        strcat(timebuf, minbuf);
        strcat(timebuf, ":");
      
        if(seconds < 10)
        {
          strcat(timebuf, "0");
        }
      
        strcat(timebuf, secbuf);
        strcat(timebuf, ";");
      }

      if(LoopCounterDataGathering == 2) //budowa informacji o HR i o wartości konrolnej
      {
        HRmsgLock = true;
        HRmsg[0] = '\0';
      
        ++controlValue;
        itoa(controlValue, conVal, 10);
        itoa(heartRate, sendVal, 10);

        strcat(HRmsg, conVal);
        strcat(HRmsg, ";");
        strcat(HRmsg, timebuf);
        strcat(HRmsg, sendVal);
        strcat(HRmsg, ";");
    
        sendVal[0] = '\0';

        HRmsgLock = false;
        HRSent = false;
      }

      if(LoopCounterDataGathering == 3)// budowa informacji o GSR
      {
        GSRmsgLock = true;
        GSRmsg[0] = '\0';
        itoa(GSRValue, sendVal, 10);
     
        strcat(GSRmsg, conVal);
        strcat(GSRmsg, ";");
        strcat(GSRmsg, timebuf);
        strcat(GSRmsg, sendVal);
        strcat(GSRmsg, ";");
        sendVal[0] = '\0';
      
        GSRmsgLock = false;
        GSRSent = false;
      }

      if(LoopCounterDataGathering == 4) //budowa informacji o napięciu
      {
        VoltagemsgLock = true;
        Voltagemsg[0] = '\0';
      
        dtostrf(Voltage, 5, 2, sendVal);
        strcat(Voltagemsg, conVal);
        strcat(Voltagemsg, ";");
        strcat(Voltagemsg, timebuf);
        strcat(Voltagemsg, sendVal);
        strcat(Voltagemsg, ";");

        sendVal[0] = '\0';      
        secbuf[0] = '\0';
        minbuf[0] = '\0';
        hourbuf[0] = '\0';
        VoltagemsgLock = false;
        VoltageSent = false;
      }

      if(LoopCounterDataGathering == 5 && Voltage <= 6.66)
      {
        Serial.println("Odcięcie zasilania");
        measure_enabled = false;
      }

      if(LoopCounterDataGathering == 7) // zapis do pliku HR
      {
        HRmsgLock = true;
        writeDataToFile(HRFile, HRmsg);
        HRmsgLock = false;
      }

      if(LoopCounterDataGathering == 10) // zapis do pliku GSR
      {
        GSRmsgLock = true;
        writeDataToFile(GSRFile, GSRmsg);
        GSRmsgLock = false;
      }

      if(LoopCounterDataGathering == 200 && TimingErrorCore0 == true)
      {
        itoa(erroroperationnumbercore0, opValErr, 10);
        strcat(Errmsg, conVal);
        strcat(Errmsg, ";");       
      }

      if(LoopCounterDataGathering == 201 && TimingErrorCore0 == true)
      {
        strcat(Errmsg, timebuf);
        strcat(Errmsg, opValErr);
        strcat(Errmsg, ";"); 
      }

      if(LoopCounterDataGathering == 202 && TimingErrorCore0 == true)
      {
        //itoa(errorcoreid, coreidbuffer, 10);
        strcat(Errmsg, "0;");  
      }


      if(LoopCounterDataGathering == 203 && TimingErrorCore0 == true)
      {
        writeDataToFile(ErrorFile, Errmsg);
      }
      
      if(LoopCounterDataGathering == 204 && TimingErrorCore0 == true)
      {
        TimingErrorCore0 = false;
        Errmsg[0] = '\0';
        opValErr[0] = '\0';
      }

      if(LoopCounterDataGathering == 300 && TimingErrorCore1 == true)
      {
        itoa(erroroperationnumbercore1, opValErr, 10);
        strcat(Errmsg, conVal);
        strcat(Errmsg, ";"); 
      }

      if(LoopCounterDataGathering == 301 && TimingErrorCore1 == true)
      {
        strcat(Errmsg, timebuf);
        strcat(Errmsg, opValErr);
        strcat(Errmsg, ";"); 
      }

      if(LoopCounterDataGathering == 302 && TimingErrorCore1 == true)
      {
        strcat(Errmsg, "1;");
      }

      if(LoopCounterDataGathering == 303 && TimingErrorCore1 == true)
      {
        writeDataToFile(ErrorFile, Errmsg);
      }

      if(LoopCounterDataGathering == 304 && TimingErrorCore1 == true)
      {
        TimingErrorCore1 = false;
        Errmsg[0] = '\0';
        opValErr[0] = '\0';
      }

      if(LoopCounterDataGathering == 305)
      {
        timebuf[0] = '\0';
        conVal[0] = '\0';
      }

      
      if(LoopCounterDataGathering == 396) // test polaczenia WiFi
      {
        if(WiFi.status() == WL_CONNECTED && WiFiBegin == true)
        {
          //Serial.println("Connected to wifi");
        }
        else if((WiFi.status() == WL_DISCONNECTED) && WiFiBegin == true)
        {
          WiFi.begin(wifiname, wifipass);
        }
        else if((WiFi.status() == WL_CONNECTION_LOST || WiFi.status() == WL_DISCONNECTED) && WiFiBegin == false)
        {
          WiFi.begin(wifiname, wifipass);
          WiFiBegin = true;
        }
        else if(WiFi.status() == WL_IDLE_STATUS && WiFiBegin == true)
        {
          Serial.println("reconnecting in progress");
        }
      }
    
      if(LoopCounterDataGathering == 400) // do wyliczania wartości i wrzucenia danych 
      {
        Serial.println(all_peaks_time);
        Serial.println(peak_counter);
        heartRate = double((double)(peak_counter-1)/(double)all_peaks_time) * 60000000;
        GSRAverage = GSRBuffer/40;
        GSRValue = (4096 + 2*GSRAverage)*10000/(2048 - GSRAverage);

        VoltageAverage = VoltageBuffer/40;
        Voltage = 6*(((double)VoltageAverage/4096)*3.3+0.1);
      
        LoopCounterDataGathering = 0;  
        first_peak_time = 0;
        next_peak_time = 0;
        all_peaks_time = 0;
        peak_counter = 0;
        GSRBuffer = 0;
        GSRAverage = 0;
        VoltageBuffer = 0;
        VoltageSample = 0;
        VoltageAverage = 0;
      }

      waitTo10ms(time_begin_DataGathering, LoopCounterDataGathering);
      LoopCounterDataGathering++;
    }

    if(FileClosed == false)
    {
        closeFile(GSRFile);
        delay(100);
        closeFile(HRFile);
        delay(100);
        closeFile(ErrorFile);
        delay(100);
        FileClosed = true;
    }

    if(FileClosed == true)
    {
      digitalWrite(16, LOW);
    }
      
  }
  
  Serial.print("Data Gathering finished");
}

void DataSending( void * pvParameters )
{
  unsigned long int time_begin_DataSending = 0;
  unsigned int LoopCounterDataSending = 1;
  

  for(;;)
  {   
    while(measure_enabled)
    {
      time_begin_DataSending = micros();
    
      if(LoopCounterDataSending == 100) // wlaczane w razie potrzeby, do przywracania polaczenia z serwerem
      {
        if(!client.connected() && WiFi.status() == WL_CONNECTED)
        {
          reconnect();
        }
      }

      if(LoopCounterDataSending == 110 && HRSent == false)
      {
        while(HRmsgLock == true)
        {
           // pusta instrukcja oczekiwania na zwolnienie w razie konieczności
        }

        if(client.connected() && WiFi.status() == WL_CONNECTED)
        {
          client.publish("HRTopic", HRmsg);
          HRSent = true;
        }
      }

      if(LoopCounterDataSending == 111 && GSRSent == false)
      {

        while(GSRmsgLock == true)
        {
          // pusta instrukcja oczekiwania na zwolnienie w razie konieczności
        }

        if(client.connected() && WiFi.status() == WL_CONNECTED)
        {
          client.publish("GSRTopic", GSRmsg);
          GSRSent = true;
        }
      }

      if(LoopCounterDataSending == 112 && VoltageSent == false)
      {

        while(VoltagemsgLock == true)
        {
          // pusta instrukcja oczekiwania na zwolnienie w razie konieczności
        }
        
        if(client.connected() && WiFi.status() == WL_CONNECTED)
        {
          client.publish("VoltageTopic", Voltagemsg);
          VoltageSent = true;
        }
      }

      if(LoopCounterDataSending == 113 && client.connected()) // do obslugi polaczenia z serwerem
      {
        client.loop();
      }

      if(LoopCounterDataSending == 397) // do sprawdzania komend
      {
        if(client.connected() && WiFi.status() == WL_CONNECTED)
        { 
          //Serial.println("sub");
          Serial.println(client.subscribe("inTopic"));
        }
      }

      if(LoopCounterDataSending == 399 && client.connected())
      {
        client.loop();
      }

      if(LoopCounterDataSending == 400) // do wyliczania wartości i wrzucenia danych 
      {
        LoopCounterDataSending = 0;   
      }
    
      waitTo10ms(time_begin_DataSending, LoopCounterDataSending);
      LoopCounterDataSending++;
    }
  }
  
  Serial.print("Data Sending finished");
}

void setup()
{
    MeasureSetup();

    disableCore0WDT();
    disableCore1WDT();
    
    if(measure_enabled)
    {
      
      Serial.println("w tworzeniu taskow");
      
      xTaskCreatePinnedToCore(DataGathering,
                            "Task1",
                            35000,
                            NULL,
                            1,
                            &DataAcquisition,
                            0
                            );

      Serial.println("Task1 done");

      xTaskCreatePinnedToCore(DataSending,
                            "Task2",
                            35000,
                            NULL,
                            1,
                            &ServerConnection,
                            1
                            );

      Serial.println("Task2 done");
    
    }
}

void loop() 
{
}
