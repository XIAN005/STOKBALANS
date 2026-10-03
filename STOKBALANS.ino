// =====================================================================
//  STOKBALANS - PROJET N°3 - BALANCE INTELLIGENTE DE GESTION D'INVENTAIRE  (v5 MQTT)
//  ESP32 + HX711 + cellule de charge + LCD I2C
//
//  1. Poids en temps réel sur le LCD
//  2. Chaque pesée est publiée en MQTT -> dashboard web (date et heure)
//  3. Total par période : calculé par le dashboard
//  4. Alerte si un poids dépasse ou descend sous un seuil
//     (seuils réglables depuis le dashboard)
//
//  Bibliothèque à ajouter dans Wokwi : PubSubClient
// =====================================================================
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "HX711.h"

// ---------- Matériel ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);
const int DOUT_PIN = 18;
const int SCK_PIN  = 19;
HX711 scale;

// ---------- Wi-Fi Wokwi ----------
const char* ssid = "Wokwi-GUEST";
const char* password = "";
bool wifiOK = false;

// ---------- MQTT (broker public gratuit, port 1883 non chiffré) ----------
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
// À RENDRE UNIQUE et IDENTIQUE dans dashboard_balance.html
const char* PREFIX = "eic3/groupe3/balance-x7k2";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
char T_POIDS[96], T_PESEE[96], T_SEUILS[96], T_CMD[96], T_STATUT[96], T_INFO[96];
Preferences prefs;   // mémoire permanente : seuils et calibration
unsigned long dernierEssaiMqtt = 0;
unsigned long delaiMqtt = 2000;     // délai entre deux essais, augmente si ça échoue (max 10 s)
unsigned long dernierLive = 0;
unsigned long debutConnexion = 0;   // pour mesurer la durée de vie de la connexion
bool etaitConnecte = false;

// ---------- Réglages ----------
float calibration_factor = 420.0;      // à recalculer avec un poids connu
float seuilMin = 5.0;                  // mis à jour depuis le dashboard
float seuilMax = 45.0;                 // mis à jour depuis le dashboard
const float SEUIL_PRESENCE   = 0.5;    // kg : en dessous = plateau vide
const float ZONE_MORTE       = 0.05;   // kg : bruit ignoré autour de zéro
const float STABLE_TOL       = 0.2;    // kg : variation max pour être "stable"
const float VARIATION_PESEE  = 1.0;    // kg : écart minimal avec la pesée précédente
const unsigned long DUREE_STABLE   = 600;   // ms de stabilité avant d'enregistrer
const unsigned long PERIODE_MESURE = 20;    // ms entre deux mesures

// ---------- File d'attente (si MQTT est coupé) ----------
struct Attente { float poids; unsigned long ms; };
const int MAX_ATTENTE = 10;
Attente fileAttente[MAX_ATTENTE];
int nbAttente = 0;

// ---------- État ----------
float poidsActuel = 0.0;
float poidsLisse = 0.0;          // poids filtré (lissage rapide)
bool  premiereLecture = true;
float dernierPoidsPublie = 0.0;  // dernier poids envoyé au dashboard
float derniereValeur = 0.0;
float refStable = 0.0;           // poids au début de la fenêtre de stabilité
float dernierPoidsEnregistre = 0.0;
float totalSession = 0.0;      // total des pesées depuis le démarrage (affichage LCD)
bool  capteurOK = true;
bool  demandeTare = false;
bool  demandeCal = false;        // calibration demandée depuis le dashboard
float poidsCal = 0.0;            // poids connu posé sur le plateau
unsigned long debutStable = 0;
unsigned long derniereLectureOK = 0;
unsigned long dernierCycle = 0;

// ---------- Utilitaires ----------
void ecrireLigne(uint8_t ligne, const char* texte) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", texte);
  lcd.setCursor(0, ligne);
  lcd.print(buf);
}

int statutPoids(float p) {          // 0 = normal, 1 = sous le seuil, 2 = au-dessus
  if (p > seuilMax) return 2;
  if (p < seuilMin) return 1;
  return 0;
}

bool poidsStable() {
  return debutStable != 0 && millis() - debutStable > DUREE_STABLE;
}

const char* etatPoids() {
  if (!capteurOK) return "err";
  if (poidsActuel < SEUIL_PRESENCE) return "vide";
  if (poidsActuel > seuilMax) return "haut";    // surcharge : alerte immédiate
  // Alerte "bas" seulement une fois le poids stabilisé (sinon elle clignote
  // à chaque pose ou retrait de sac, quand le poids traverse la zone basse)
  if (poidsActuel < seuilMin) return poidsStable() ? "bas" : "mesure";
  return "ok";
}

// Messages reçus : seuils ("min;max") et commandes ("tare")
void recevoirMessage(char* topic, byte* payload, unsigned int len) {
  char msg[64];
  if (len >= sizeof(msg)) len = sizeof(msg) - 1;
  memcpy(msg, payload, len);
  msg[len] = 0;

  if (strcmp(topic, T_SEUILS) == 0) {
    float mn, mx;
    if (sscanf(msg, "%f;%f", &mn, &mx) == 2 && mn >= 0 && mx > mn && mx <= 1000) {
      if (mn != seuilMin || mx != seuilMax) {   // on n'écrit en mémoire que si ça change
        seuilMin = mn;
        seuilMax = mx;
        prefs.putFloat("min", seuilMin);
        prefs.putFloat("max", seuilMax);
      }
      Serial.printf("[MQTT] Nouveaux seuils : min %.1f kg | max %.1f kg\n", seuilMin, seuilMax);
    }
  } else if (strcmp(topic, T_CMD) == 0) {
    if (strcmp(msg, "tare") == 0) demandeTare = true;
    else if (strncmp(msg, "cal:", 4) == 0) {
      float w = atof(msg + 4);
      if (w > 0.1f && w <= 1000.0f) { poidsCal = w; demandeCal = true; }
    }
  }
}

void connecterMqtt() {
  if (!wifiOK || mqtt.connected()) return;
  if (dernierEssaiMqtt != 0 && millis() - dernierEssaiMqtt < delaiMqtt) return;
  dernierEssaiMqtt = millis();

  wifiClient.stop();   // referme proprement l'ancien socket avant de reconnecter

  char id[32];
  snprintf(id, sizeof(id), "balance-%08X", (unsigned int)esp_random());
  Serial.print("MQTT : connexion... ");

  // Message "offline" automatique si la balance disparaît
  if (mqtt.connect(id, NULL, NULL, T_STATUT, 0, true, "offline")) {
    Serial.println("OK");
    debutConnexion = millis();
    etaitConnecte = true;
    delaiMqtt = 2000;
    mqtt.publish(T_STATUT, "online", true);
    mqtt.subscribe(T_SEUILS);
    mqtt.subscribe(T_CMD);
  } else {
    Serial.printf("echec (code %d, WiFi %d)\n", mqtt.state(), WiFi.status());
    delaiMqtt = (delaiMqtt * 2 > 10000) ? 10000 : delaiMqtt * 2;
  }
}

// Publie les pesées en attente, dans l'ordre
void envoyerFile() {
  while (nbAttente > 0 && mqtt.connected()) {
    char msg[96];
    unsigned long age = (millis() - fileAttente[0].ms) / 1000;   // ancienneté de la pesée
    snprintf(msg, sizeof(msg), "{\"poids\":%.2f,\"statut\":%d,\"age\":%lu}",
             fileAttente[0].poids, statutPoids(fileAttente[0].poids), age);

    if (!mqtt.publish(T_PESEE, msg)) return;
    Serial.printf("[MQTT] Pesee %.2f kg publiee\n", fileAttente[0].poids);

    for (int i = 1; i < nbAttente; i++) fileAttente[i - 1] = fileAttente[i];
    nbAttente--;
  }
}

void enregistrerPesee(float p) {
  totalSession += p;
  Serial.printf("[PESEE] %.2f kg (%s) | Total session : %.2f kg\n", p,
                p > seuilMax ? "AU-DESSUS DU SEUIL" : (p < seuilMin ? "SOUS LE SEUIL" : "normal"),
                totalSession);

  if (nbAttente >= MAX_ATTENTE) {            // file pleine : on retire la plus ancienne
    for (int i = 1; i < MAX_ATTENTE; i++) fileAttente[i - 1] = fileAttente[i];
    nbAttente = MAX_ATTENTE - 1;
  }
  fileAttente[nbAttente].poids = p;
  fileAttente[nbAttente].ms = millis();
  nbAttente++;
  envoyerFile();
}

void afficherLCD() {
  char l[17];
  char p[15];
  snprintf(p, sizeof(p), "Poids:%.2fkg", poidsActuel);
  snprintf(l, sizeof(l), "%-14s %c", p, mqtt.connected() ? '*' : '-');   // * = connecté au dashboard
  ecrireLigne(0, l);

  const char* e = etatPoids();

  // En cas d'alerte, le rétroéclairage clignote
  static bool retro = true;
  bool alerte = (strcmp(e, "haut") == 0 || strcmp(e, "bas") == 0);
  bool voulu = !alerte || ((millis() / 500) % 2 == 0);
  if (voulu != retro) {
    retro = voulu;
    if (retro) lcd.backlight(); else lcd.noBacklight();
  }

  if (strcmp(e, "haut") == 0)      ecrireLigne(1, "ALERTE: Max!");
  else if (strcmp(e, "bas") == 0)  ecrireLigne(1, "ALERTE: Bas!");
  else if (nbAttente > 0) {
    snprintf(l, sizeof(l), "A envoyer: %d", nbAttente);
    ecrireLigne(1, l);
  } else if ((millis() / 3000) % 2 == 1) {
    if (mqtt.connected()) snprintf(l, sizeof(l), "Session:%.1f kg", totalSession);
    else                  snprintf(l, sizeof(l), "Hors ligne");
    ecrireLigne(1, l);
  } else {
    ecrireLigne(1, strcmp(e, "ok") == 0 ? "Poids OK"
                 : (strcmp(e, "mesure") == 0 ? "Pesee en cours" : "Plateau vide"));
  }
}

void initMqtt() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(recevoirMessage);
  mqtt.setBufferSize(512);
  mqtt.setSocketTimeout(2);
  mqtt.setKeepAlive(30);
}

// Attente qui continue de servir MQTT (évite que le broker coupe la connexion)
void attendre(unsigned long ms) {
  unsigned long t = millis();
  while (millis() - t < ms) {
    mqtt.loop();
    delay(10);
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  // Valeurs conservées d'une session à l'autre (sinon valeurs par défaut)
  prefs.begin("stokbalans", false);
  seuilMin = prefs.getFloat("min", seuilMin);
  seuilMax = prefs.getFloat("max", seuilMax);
  calibration_factor = prefs.getFloat("cal", calibration_factor);

  lcd.init();
  lcd.backlight();
  ecrireLigne(0, "Connexion WiFi..");

  snprintf(T_POIDS,  sizeof(T_POIDS),  "%s/poids",  PREFIX);
  snprintf(T_PESEE,  sizeof(T_PESEE),  "%s/pesee",  PREFIX);
  snprintf(T_SEUILS, sizeof(T_SEUILS), "%s/seuils", PREFIX);
  snprintf(T_CMD,    sizeof(T_CMD),    "%s/cmd",    PREFIX);
  snprintf(T_STATUT, sizeof(T_STATUT), "%s/statut", PREFIX);
  snprintf(T_INFO,   sizeof(T_INFO),   "%s/info",   PREFIX);

  WiFi.begin(ssid, password, 6);   // canal 6 = connexion rapide sous Wokwi
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(250);
    Serial.print(".");
  }
  wifiOK = (WiFi.status() == WL_CONNECTED);

  if (wifiOK) {
    Serial.println("\nWiFi connecte !");
    initMqtt();

    ecrireLigne(0, "Connexion MQTT..");
    connecterMqtt();
    unsigned long t2 = millis();                 // laisse arriver les seuils enregistrés
    while (millis() - t2 < 1500) { mqtt.loop(); delay(20); }
  } else {
    Serial.println("\nWiFi indisponible : mode local (LCD seul)");
  }

  scale.begin(DOUT_PIN, SCK_PIN);
  scale.set_scale(calibration_factor);

  ecrireLigne(0, "Init balance...");
  ecrireLigne(1, "Plateau vide !");
  unsigned long t1 = millis();
  while (!scale.is_ready() && millis() - t1 < 2000) delay(50);
  if (scale.is_ready()) scale.tare(10);
  derniereLectureOK = millis();

  lcd.clear();
  ecrireLigne(0, "   STOKBALANS");
  ecrireLigne(1, mqtt.connected() ? " Systeme pret !" : " Mode local");
  attendre(1500);
  lcd.clear();
}

// ---------- Boucle ----------
void loop() {
  // Wi-Fi arrivé après le démarrage : on active MQTT sans redémarrer
  if (!wifiOK && WiFi.status() == WL_CONNECTED) {
    wifiOK = true;
    initMqtt();
    Serial.println("WiFi connecte (tardivement) : MQTT active");
  }

  // Détecte et journalise une coupure de la connexion MQTT
  bool connecteMaintenant = mqtt.connected();
  if (etaitConnecte && !connecteMaintenant) {
    Serial.printf("[MQTT] Connexion perdue apres %lu s (etat %d, WiFi %d)\n",
                  (millis() - debutConnexion) / 1000, mqtt.state(), WiFi.status());
  }
  etaitConnecte = connecteMaintenant;

  connecterMqtt();
  if (mqtt.connected()) {
    mqtt.loop();
    if (nbAttente > 0) envoyerFile();

    // Poids en direct : dès qu'il change (environ 6 fois/s max), sinon 1 fois par seconde
    bool change = fabs(poidsActuel - dernierPoidsPublie) >= 0.05f && millis() - dernierLive > 150;
    if (change || millis() - dernierLive > 1000) {
      dernierLive = millis();
      dernierPoidsPublie = poidsActuel;
      char msg[80];
      snprintf(msg, sizeof(msg), "{\"poids\":%.2f,\"etat\":\"%s\"}", poidsActuel, etatPoids());
      mqtt.publish(T_POIDS, msg);
    }
  }

  // Tare demandée depuis le dashboard (plateau vide)
  if (demandeTare) {
    demandeTare = false;
    if (poidsActuel >= SEUIL_PRESENCE) {
      // Protège contre une tare faite par erreur (ou par un tiers sur le broker public)
      Serial.println("[TARE] Refusee : le plateau n'est pas vide");
    } else {
      ecrireLigne(1, "Tare...");
      scale.tare(10);
      premiereLecture = true;   // le lissage repart de la nouvelle valeur zéro
      derniereValeur = 0.0;
      dernierPoidsEnregistre = 0.0;
      debutStable = 0;
    }
  }

  // Calibration demandée depuis le dashboard (poids connu posé sur le plateau)
  if (demandeCal) {
    demandeCal = false;
    char m[96];
    float v = scale.get_value(10);   // mesure brute moins la tare
    if (fabs(v) < 100) {
      snprintf(m, sizeof(m), "Calibration refusee : poser d'abord le poids connu");
    } else {
      calibration_factor = v / poidsCal;
      scale.set_scale(calibration_factor);
      prefs.putFloat("cal", calibration_factor);
      premiereLecture = true;
      snprintf(m, sizeof(m), "Calibration OK : facteur %.1f pour %.2f kg", calibration_factor, poidsCal);
    }
    Serial.println(m);
    if (mqtt.connected()) mqtt.publish(T_INFO, m);
  }

  if (millis() - dernierCycle < PERIODE_MESURE) return;
  dernierCycle = millis();

  if (scale.is_ready()) {
    derniereLectureOK = millis();
    capteurOK = true;

    float lecture = scale.get_units(1);   // 1 seule mesure : la boucle ne bloque plus
    if (premiereLecture || fabs(lecture - poidsLisse) > 2.0f) {
      poidsLisse = lecture;               // gros changement : réaction immédiate
      premiereLecture = false;
    } else {
      poidsLisse += 0.5f * (lecture - poidsLisse);   // petit bruit : lissé
    }
    poidsActuel = (fabs(poidsLisse) < ZONE_MORTE) ? 0.0f : poidsLisse;
    if (poidsActuel < 0) poidsActuel = 0.0f;

    // Une pesée est enregistrée quand le poids est stable, présent,
    // et différent de la pesée précédente
    // Fenêtre de stabilité : le poids doit rester proche de sa valeur de DÉPART
    // (et non de la mesure précédente, sinon une montée lente passerait pour stable)
    if (debutStable != 0 && fabs(poidsActuel - refStable) < STABLE_TOL) {
      if (millis() - debutStable > DUREE_STABLE &&
          poidsActuel >= SEUIL_PRESENCE &&
          fabs(poidsActuel - dernierPoidsEnregistre) > VARIATION_PESEE) {
        // Si le plateau n'a pas été vidé, seul le poids AJOUTÉ est une nouvelle pesée
        // (un retrait met simplement à jour la référence, sans pesée)
        float ajout = (dernierPoidsEnregistre > 0) ? poidsActuel - dernierPoidsEnregistre
                                                   : poidsActuel;
        if (ajout > 0) enregistrerPesee(ajout);
        dernierPoidsEnregistre = poidsActuel;
      }
    } else {
      debutStable = millis();      // nouvelle fenêtre de stabilité
      refStable = poidsActuel;
    }
    derniereValeur = poidsActuel;

    // Plateau vidé : prêt pour la pesée suivante
    if (poidsActuel < SEUIL_PRESENCE) dernierPoidsEnregistre = 0.0;

    static unsigned long dernierLcd = 0;
    if (millis() - dernierLcd > 150) {      // le LCD est lent : inutile de le réécrire à chaque mesure
      dernierLcd = millis();
      afficherLCD();
    }

  } else if (millis() - derniereLectureOK > 2000) {
    capteurOK = false;
    ecrireLigne(0, "Erreur HX711");
    ecrireLigne(1, "Verifier cablage");
  }
}
