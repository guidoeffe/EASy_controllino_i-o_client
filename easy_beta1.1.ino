/*EASy Controllino Ver.0.1.1
   - Assegna MAC Address e Attiva DHCP
   - Ottiene Epoch Timestamp da Server NTP
   - Aggiorna variabile epoch ogni secondo
   - Configura e legge i bus del terminale
   - Invia a cadenza predefinita i valori dei contapezzi
   - Invia in tempo reale lo stato di RUN/STOP del centro di lavoro
   developed by Guido Fiamozzi
*/

/*CREDITS
  Contiene parti di codice di pubblico dominio rielaborate:
  - Udp NTP Client
  Contiene librerie di terze parti di pubblico dominio
*/

#include <SPI.h>
#include <Controllino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

// MAC ADDRESS - DHCP
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED //deve essere diverso per ogni Controllino installato
};
// Set the static IP address to use if the DHCP fails to assign
IPAddress ip(192, 168, 0, 177); //deve essere diverso per ogni Controllino installato

// variabili Controllino/ Terminale I/O
int pz_buoni =0;
int pz_buoni_status =0;
int pz_scarti=0;
int pz_scarti_status =0;
int cl_status =0;
int busStartStop = 1; // questo parametro deve corrispondere con la configurazione di EASy
int busPzBuoni = 2 ; // questo parametro deve corrispondere con la configurazione di EASy
int busPzScarti = 3;// questo parametro deve corrispondere con la configurazione di EASy
unsigned long timerSendUrlPZ = 0;
unsigned long intervalSendUrlPZ = 5000; // Setta l'intervallo di tempo (millisecondi) per l'invio dei contapezzi 
unsigned long timeStartPzBuoni = 0;
unsigned long latentPzBuoni = 30; // Setta l'intervallo di tempo (millisecondi) per la lettura dei pezzi buoni [evita false letture]
unsigned long timeStartPzScarti = 0; 
unsigned long latentPzScarti = 30; // Setta l'intervallo di tempo (millisecondi) per la lettura dei pezzi scarti [evita false letture]
unsigned long timeStartRunStop = 0;
unsigned long latentRunStop = 30; // Setta l'intervallo di tempo (millisecondi) per la lettura Run/Stop [evita false letture]


// variabili server
//IPAddress server(74,125,232,128);  // IP del server (no DNS)
char server[] = "www.google.com";    // dominio derver (con DNS)
EthernetClient client;

// costruzione url
String easyTerminalID = "1122334455667788"; // ID del terminale I/O (deve essere inserito anche sul server EASy e diverso per ogni Controllino installato)
String easyUrlPage = "/terminal/runstop/";
String easyUrlParameters ;
String easyUrlPzBuoni;
String easyUrlPzScarti;
String easyUrlRunStop;

// Variabili per i timer
unsigned long easyTimestamp = 0;  // Il timestamp Epoc Unix che viene trasmesso al server eEASy
unsigned long easyStartTimer = 0;
unsigned long easyEverySecond = 1000;  // Variabile che regola il timestamp [Non Modificare]
unsigned long easyStartNTP =0;
unsigned long easyRefreshNTP = 3600000;  // aggiorna il timestamp con il server NTP ogni ora [1h = 3600000ms]

// NTP Server via UDP
unsigned int localPort = 8888;       // local port to listen for UDP packets 
char timeServer[] = "time.nist.gov"; // time.nist.gov NTP server
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
EthernetUDP Udp;

void setup() {
  Serial.begin(9600);

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
  pinMode(CONTROLLINO_A0, INPUT); // Collegare segnale Pezzi buoni su A0
  pinMode(CONTROLLINO_A1, INPUT); // Collegare segnale Pezzi scarti su A1
  pinMode(CONTROLLINO_A2, INPUT); // Collegare segnale RUN / STOP su A2 (RUN: segnale continuo su HIGH)
}

void loop() {
  unsigned long currentMillis = millis(); 
  
  //pezzi buoni
  if (currentMillis - timeStartPzBuoni > latentPzBuoni){

  if (digitalRead(CONTROLLINO_A0)== 1){
  if (pz_buoni_status == 0) {
  pz_buoni= pz_buoni + 1;
  easyUrlPzBuoni = String(easyUrlPage + easyTerminalID + "/" + busPzBuoni + "/" + pz_buoni + "/" + easyTimestamp);
  printUrl(easyUrlPzBuoni); 
  }
  }
  pz_buoni_status = digitalRead(CONTROLLINO_A0);
  timeStartPzBuoni = currentMillis;
  }

  //pezzi scarti
  if (currentMillis - timeStartPzScarti > latentPzScarti){
  if (digitalRead(CONTROLLINO_A1)== 1){
  if (pz_scarti_status == 0) {
  pz_scarti= pz_scarti + 1;
  easyUrlPzScarti = String(easyUrlPage + easyTerminalID + "/" + busPzScarti + "/" + pz_scarti + "/" + easyTimestamp);
  printUrl(easyUrlPzScarti); 
  }
  }
  pz_scarti_status = digitalRead(CONTROLLINO_A1);
  timeStartPzScarti = currentMillis;
  }

   //centro di lavoro RUN STOP

  if (currentMillis - timeStartRunStop > latentRunStop){
  if (cl_status == digitalRead(CONTROLLINO_A2)) {
  } else {
    cl_status = digitalRead(CONTROLLINO_A2);
    easyUrlRunStop = String(easyUrlPage + easyTerminalID + "/" + busStartStop + "/" + cl_status + "/" + easyTimestamp);
    printUrl(easyUrlRunStop);   
    } 
  timeStartRunStop = currentMillis;
  }

// aggiorna Timestamp  
if (currentMillis - easyStartTimer > easyEverySecond){
  easyTimestamp++ ;
  easyStartTimer = currentMillis;
  }

// invia pezzi contati
if (currentMillis - timerSendUrlPZ  > intervalSendUrlPZ){
//  sendUrl();
  timerSendUrlPZ  = currentMillis;
  }  

  Ethernet.maintain();
}


// FUNZIONE: stampa variabile su seriale
void printUrl (String easyUrl){
  Serial.println(easyUrl);  
  }


// FUNZIONE: invia url con i parametri impostati

void sendUrl (String easyUrl){
  // if you get a connection, report back via serial:
  if (client.connect(server, 80)) {
    Serial.println("connected");
    // Make a HTTP request:
    client.println("GET /search?q=arduino HTTP/1.1");
    client.println("Host: www.google.com");
    client.println("Connection: close");
    client.println();
  } else {
    // if you didn't get a connection to the server:
    Serial.println("connection failed");
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
    return epoch;
  }
  else {
    return 0;
    }
}
