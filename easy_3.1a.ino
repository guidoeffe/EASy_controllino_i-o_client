// Aggiornare sempre il numero di versione software
String txtSwVersion = "3.1";

/*EASy Controllino Ver. 3.1
   - Assegna MAC Address e Attiva DHCP
   - Ottiene Epoch Timestamp da Server NTP
   - Aggiorna variabile epoch ogni secondo
   - Configura e legge i bus del terminale [contapezzi tramite interrupt 0 e 1]
   - Legge tag RFID con dispositivo standard Wiegand
   - Invia a cadenza predefinita i valori dei contapezzi
   - Invia in tempo reale lo stato di RUN/STOP del centro di lavoro
   - Gestisce led monitor connessione web e connessione server EASy
   developed by Guido Fiamozzi
*/

/*CREDITS
  Contiene parti di codice di pubblico dominio rielaborate:
  - Udp NTP Client
  Contiene librerie di terze parti di pubblico dominio
  - CONTROLLINO-PLC/CONTROLLINO_Library
  - csquared/arduino-restclient
  - ugge75/Wiegand-V3-Library-for-all-Arduino
*/

/*COLLEGAMENTI
 * Collegare segnale RUN / STOP su A0 (RUN: segnale continuo su HIGH)
 * Collegare segnale PEZZI BUONI su IN0 (Interrupt 0 Segnale da LOW a HIGH)
 * Collegare segnale PEZZI SCARTI su IN1 (Interrupt 1 Segnale da LOW a HIGH)
 * Collegare RFID Reader Standard WIEGAND DATA 0 (Verde) DATA 1 (Bianco) al connettore pin in corrispondenza D0 e D1
 */

 /*VARIABILI DA PERSONALIZZARE SU OGNI CONTROLLINO [Devono essere diverse da un terminale all'altro]
  * byte mac[]
  * IPAddress ip [Nel caso non fosse disponibile il servizio DHCP]
  * String easyTerminalID [E' l'ID con il quale il terminale si identifica sul server EASy]
  */
  
  /*VARIABILI DA PERSONALIZZARE PER OGNI SERVER EASy
  * RestClient client  // Il dominio del server EASy 
  */

#include <SPI.h>
#include <Controllino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include "RestClient.h"
#include <Wiegand.h>
#define FALSE 0      
#define TRUE  1 

// inizializza lettore RFID
WIEGAND wg;


// MAC ADDRESS - DHCP
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED //deve essere diverso per ogni Controllino installato
};
// Set the static IP address to use if the DHCP fails to assign
IPAddress ip(192, 168, 0, 177); //deve essere diverso per ogni Controllino installato

// variabili Controllino/ Terminale I/O

// setto interrupt per pezzi buoni e pezzi scarti
int Pin_PezziBuoni = digitalPinToInterrupt(CONTROLLINO_IN0);
volatile int pzBuoni =0;

int Pin_PezziScarti = digitalPinToInterrupt(CONTROLLINO_IN1);
volatile int pzScarti =0;

int pzBuoniInvio =0; // memorizza i pezzi buoni che sono stati inviati
int pzBuoniStatus =0;
int sendPzBuoniOk = 0;
int pzScartiInvio=0; // memorizza i pezzi buoni che sono stati inviati
int pzScartiStatus =0;
int centroLavoroStatus =0;
int busStartStop = 1; // questo parametro deve corrispondere con la configurazione di EASy
int busPzBuoni = 2 ; // questo parametro deve corrispondere con la configurazione di EASy
int busPzScarti = 3;// questo parametro deve corrispondere con la configurazione di EASy
int busRFID = 4;// questo parametro deve corrispondere con la configurazione di EASy
unsigned long timerSendUrlPzBuoni = 0;
unsigned long intervalSendUrlPzBuoni = 5000; // Setta l'intervallo di tempo (millisecondi) per l'invio dei Pezzi buoni
static unsigned long timeStartPzBuoni = 0;
static unsigned long latentPzBuoni = 200; // Setta l'intervallo di tempo (millisecondi) per la lettura dei pezzi buoni [evita false letture]
unsigned long timerSendUrlPzScarti = 0;
unsigned long intervalSendUrlPzScarti = 6000; // Setta l'intervallo di tempo (millisecondi) per l'invio dei Pezzi scarti 
static unsigned long timeStartPzScarti = 0; 
static unsigned long latentPzScarti = 200; // Setta l'intervallo di tempo (millisecondi) per la lettura dei pezzi scarti [evita false letture]
unsigned long timeStartRunStop = 0;
unsigned long latentRunStop = 100; // Setta l'intervallo di tempo (millisecondi) per la lettura Run/Stop [evita false letture]


// variabili per connessione al server
String easyServer = "demo.easymes.net"; //inserire lo stesso dominio in tutt'e due le variabili
RestClient client = RestClient("demo.easymes.net");
String response;

// costruzione url
String easyTerminalID = "1122334455667788"; // ID del terminale I/O (deve essere inserito anche sul server EASy e diverso per ogni Controllino installato)
String easyUrlPage = "/terminal/runstop/";
String easyUrlParameters ;
String easyUrlPzBuoni;
String easyUrlPzScarti;
String easyUrlRunStop;
String easyUrlRFID;

// Variabili per i timer
unsigned long easyTimestamp = 0;  // Il timestamp Epoc Unix che viene trasmesso al server eEASy
unsigned long easyStartTimer = 0;
unsigned long easyEverySecond = 1000;  // Variabile che regola il timestamp [Non Modificare]
unsigned long easyStartNTP =0;
unsigned long easyRefreshNTP = 3600000;  // aggiorna il timestamp con il server NTP [1h = 3600000ms]
unsigned long startLedRFID = 0;

// NTP Server via UDP
unsigned int localPort = 8888;       // local port to listen for UDP packets 
char timeServer[] = "time.nist.gov"; // time.nist.gov NTP server
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
EthernetUDP Udp;

void setup() {
Serial.begin(9600);
Serial.println("\n*********************************************\n");
Serial.println("Easy Solutions di Paolo Fiamozzi");
Serial.println("CONTROLLINO EASy Versione " + String(txtSwVersion) +"");
Serial.println("ID Terminale: " + String(easyTerminalID) +  "");

// setta spie led 
// ATTENZIONE: Non utilizzare CONTROLLINO_D0 e CONTROLLINO_D1: sono riservati al lettore RFID
pinMode(CONTROLLINO_D10, OUTPUT); // led ethernet
digitalWrite(CONTROLLINO_D10, LOW); 
pinMode(CONTROLLINO_D11, OUTPUT); // led easy server communication
digitalWrite(CONTROLLINO_D11, LOW); 
  


  // Inizializza Ethernet e UDP
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // try to congifure using IP address instead of DHCP:
    Ethernet.begin(mac, ip);
  }
  Udp.begin(localPort);
  
easyTimestamp = getUnixTimestampFromNTP(timeServer);

easyStartNTP = millis();
easyStartTimer = millis();

// setta bus terminale Input 
  pinMode(CONTROLLINO_A0, INPUT); // Collegare segnale RUN / STOP su A0 (RUN: segnale continuo su HIGH)
  attachInterrupt(Pin_PezziBuoni, IRQcounter, RISING); // Collegare segnale PEZZI BUONI su IN0 (Interrupt 0 Segnale da LOW a HIGH)
  attachInterrupt(Pin_PezziScarti, IRQcounterPS, RISING); // Collegare segnale PEZZI BUONI su IN1 (Interrupt 1 Segnale da LOW a HIGH)


Serial.println("Timestamp Server UDP: " + String(easyTimestamp) + "");
Serial.print("Ip locale utilizzato: ");
Serial.print(Ethernet.localIP());
Serial.println("\nInvia i dati al server: http://" + String(easyServer) + "");
Serial.println("\nLettore RFID Wiegand\n");
/* Parametri lettore RFID 
 * PIN assigment and declaration for Arduino Mega 
*/
  //GATE A 
  wg.D0PinA =2;   
  wg.D1PinA =3;   

  //GATE B
  wg.D0PinB =18;  
  wg.D1PinB =19;   

  //GATE C
  wg.D0PinC =20;  
  wg.D1PinC =21;  

 // Reader enable
  wg.begin(TRUE, FALSE, FALSE);  // wg.begin(GateA , GateB, GateC)
Serial.println("\n*********************************************\n");  
} // void setup

void loop() {
  unsigned long currentMillis = millis();

  // aggiorna Timestamp  
  if (currentMillis - easyStartTimer > easyEverySecond){
  easyTimestamp++ ;
  easyStartTimer = currentMillis;
  }

  //centro di lavoro RUN STOP
  if (currentMillis - timeStartRunStop  > latentRunStop){
  if (centroLavoroStatus == digitalRead(CONTROLLINO_A0)) {
  } else {
    centroLavoroStatus = digitalRead(CONTROLLINO_A0);
    easyUrlRunStop = String(easyUrlPage + easyTerminalID + "/" + busStartStop + "/" + centroLavoroStatus + "/" + easyTimestamp);
    printUrl(easyUrlRunStop);
    int sendRunStopOk = sendUrl(easyUrlRunStop);
    if (sendRunStopOk == 200) {
  } else {
    }  
    } 
  timeStartRunStop = currentMillis;
  }

// Lettore presenze RFID

  if(wg.available())
  {
    easyUrlRFID = String(easyUrlPage + easyTerminalID + "/" + busRFID + "/" + wg.getCode() + "/" + easyTimestamp);
    printUrl(easyUrlRFID);
    int sendRunStopOk = sendUrl(easyUrlRFID);
    if (sendRunStopOk == 200) {
    startLedRFID = currentMillis;  
  } else {
    startLedRFID = 0;
    }
  }
 if (currentMillis - startLedRFID < 1000){
    digitalWrite(CONTROLLINO_D4, HIGH);
  } else {
    digitalWrite(CONTROLLINO_D4, LOW); 
    }  


// invia pezzi buoni
  if (currentMillis - timerSendUrlPzBuoni  > intervalSendUrlPzBuoni){
  cli();//disable interrupts
  pzBuoniInvio= pzBuoni;
  sei();//enable interrupts
 
  if (pzBuoniInvio > 0){
  easyUrlPzBuoni = String(easyUrlPage + easyTerminalID + "/" + busPzBuoni + "/" + pzBuoniInvio + "/" + easyTimestamp);
  printUrl(easyUrlPzBuoni);
  sendPzBuoniOk = sendUrl(easyUrlPzBuoni);

  if (sendPzBuoniOk == 200) {
  cli();//disable interrupts
  pzBuoni = pzBuoni - pzBuoniInvio;
  sei();//enable interrupts
  pzBuoniInvio = 0;
  sendPzBuoniOk = 0;
  } else {
  } 
    }
        else {
  Serial.println("Non ci sono pezzi buoni da inviare");
      }
  timerSendUrlPzBuoni  = currentMillis;
  }
    

// invia pezzi scarti
  if (currentMillis - timerSendUrlPzScarti  > intervalSendUrlPzScarti){
  cli();//disable interrupts
  pzScartiInvio= pzScarti;
  sei();//enable interrupts
  if (pzScartiInvio > 0){
    easyUrlPzScarti = String(easyUrlPage + easyTerminalID + "/" + busPzScarti + "/" + pzScartiInvio + "/" + easyTimestamp);
    printUrl(easyUrlPzScarti); 
    int sendPzScartiOk = sendUrl(easyUrlPzScarti);
   
  if (sendPzScartiOk == 200) {
  cli();//disable interrupts
  pzScarti = pzScarti - pzScartiInvio;
  sei();//enable interrupts
  pzScartiInvio = 0;
  sendPzScartiOk = 0;
  } else {
    } 
    }
    else {
  Serial.println("Non ci sono pezzi scarti da inviare");
      }
  timerSendUrlPzScarti  = currentMillis;
  }  

  Ethernet.maintain();
}  // Fine void loop

// FUNZIONE: conta pezzi buoni via interrupt [CONTROLLINO_IN0]
  void IRQcounter() {
  unsigned long currentMillisPzB = millis(); 
  if (currentMillisPzB - timeStartPzBuoni > latentPzBuoni){
  pzBuoni++;
  timeStartPzBuoni = currentMillisPzB;
  }
}

// FUNZIONE: conta pezzi scarti via interrupt [CONTROLLINO_IN1]
  void IRQcounterPS() {
  unsigned long currentMillisPzS = millis(); 
  if (currentMillisPzS - timeStartPzScarti > latentPzScarti){
  pzScarti++;
  timeStartPzScarti = currentMillisPzS;
  }
}


// FUNZIONE: Interroga il server NTP e ottiene un timestamp in formato UNIX
  unsigned long getUnixTimestampFromNTP(char* address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  Udp.beginPacket(address, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
  delay(1000);
  
  if (Udp.parsePacket()) {
    Udp.read(packetBuffer, NTP_PACKET_SIZE);
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    const unsigned long seventyYears = 2208988800UL;
    unsigned long epoch = secsSince1900 - seventyYears;
  digitalWrite(CONTROLLINO_D10, HIGH); // ok internet connection
    return epoch;
  }
  else {
    digitalWrite(CONTROLLINO_D10, LOW); // ko internet connection
    return 0;
    }
}


// FUNZIONE: stampa variabile su seriale
  void printUrl (String easyUrl){
  Serial.println(easyUrl);  
  }


// FUNZIONE: invia url con i parametri impostati e restituisce status del server [200 = trasmissione OK]

  int sendUrl (String easyUrl) {
  // se la connessione è stata impostata invia esito a seriale
  response = "";
  int statusCodePost = client.post(easyUrl.c_str(), "POSTDATA", &response);
  Serial.print("Status code from server: ");
  Serial.println(statusCodePost);
  Serial.print("Response body from server: ");
if (statusCodePost== 200) {
digitalWrite(CONTROLLINO_D10, HIGH); // ok connessione web
digitalWrite(CONTROLLINO_D11, HIGH); // ok connessione EASy
  } else {
digitalWrite(CONTROLLINO_D10, LOW); // ko connessione web
digitalWrite(CONTROLLINO_D11, LOW); // ko connessione EaASy
    
// Test connessione al web
response = ""; 
RestClient client = RestClient("httpbin.org");
if (client.get("/anything", &response)) {
    Serial.println("Connessione web presente");
    digitalWrite(CONTROLLINO_D10, HIGH); // ok connessione web
    } else {
    Serial.println("Connessione web mancante");
    digitalWrite(CONTROLLINO_D10, LOW); // ko connessione web
    }
  }
  Serial.println(response);
  return statusCodePost;
  }
