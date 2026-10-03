# STOKBALANS ⚖️📶

**STOKBALANS** est une solution IoT d'inventaire intelligent et de pesée connectée. Conçue pour les coopératives agricoles et les petits commerces (sacs de céréales, de café, etc.), elle permet d'automatiser l'enregistrement des pesées, de contrôler les seuils de tolérance et de visualiser les données en temps réel sur un tableau de bord web.

---

## 🌟 Fonctionnalités Principales

* **Pesée Haute Précision :** Échantillonnage à 100 ms avec filtrage du bruit et tarage automatique au démarrage.
* **Détection Intelligente de Pesée :** Validation automatique d'un poids stable (stabilisation de 1 s, seuil minimal de 0,5 kg et variation > 1 kg) pour éviter les fausses mesures.
* **Alertes en Temps Réel :** Comparaison continue avec des seuils configurables (par défaut : 5 kg min / 45 kg max) avec indicateur visuel et clignotement du rétroéclairage LCD.
* **Résilience Réseau (Mode Hors-Ligne) :** File d'attente locale pouvant stocker jusqu'à 10 pesées non envoyées en cas de coupure Wi-Fi, avec resynchronisation automatique au retour de la connexion.
* **Tableau de Bord Web Interactif :**
  * Visualisation du poids en direct et historique des mesures.
  * Graphiques temps réel et calcul du cumul sur période.
  * Configuration à distance des seuils d'alerte et fonction Tare à distance.
  * Exportation des données au format CSV.

---

## 🛠️ Architecture Matérielle

| Composant | Rôle | Broches / Connexion (ESP32) |
| :--- | :--- | :--- |
| **ESP32** | Microcontrôleur principal ("Cerveau") | — |
| **4 Cellules de charge** | Capteurs sous la plateforme | Pont de Wheatstone $\rightarrow$ Bornes `E+`, `E-`, `A+`, `A-` du HX711 |
| **Module HX711** | Amplificateur et convertisseur CAN 24 bits | `DT` $\rightarrow$ GPIO 18 <br> `SCK` $\rightarrow$ GPIO 19 |
| **Écran LCD 16x2 I2C** | Affichage local du poids et des alertes | `SDA` $\rightarrow$ GPIO 21 <br> `SCL` $\rightarrow$ GPIO 22 <br> Adresse I2C : `0x27` |

---

## 📂 Structure du Projet

```text
├── balance_smart_v5.ino     # Code source C++ / Arduino pour l'ESP32
├── dashboard_balance.html   # Interface web (Dashboard client MQTT / JavaScript)
└── README.md                # Documentation du projet
```

---

## 🚀 Fonctionnement du Système

```
[ Sac posé sur la plateforme ]
             │
             ▼
[ 4 Cellules de charge (déformation) ]
             │
             ▼
[ HX711 (amplification + conversion 24 bits) ]
             │
             ▼
[ ESP32 (filtrage du bruit & validation de stabilité 1s) ]
             │
      ┌──────┴─────────────────────────────────┐
      ▼                                        ▼
[ Affichage local LCD ]              [ Publication MQTT ]
(Poids, statut, alerte)                        │
                                     ┌─────────┴─────────┐
                                     ▼                   ▼
                           [ Wi-Fi connecté ]   [ Hors ligne ]
                                     │                   │
                                     ▼                   ▼
                           [ Transmis au Dashboard ] [ Sauvegarde dans buffer ]
                                     │                   │
                                     ▼                   ▼
                           [ Mise à jour UI /  [ Auto-sync au rétablissement ]
                             Export CSV ]
```

---

## 🔧 Protocole et Communications

* **Protocole de communication :** MQTT
* **Fréquence de rafraîchissement :**
  * Poids en direct : 1 fois par seconde (`/telemetry`)
  * Événement de pesée validée : Immédiat à la stabilisation (`/weigh_in`)

---

## 📝 Configuration et Utilisation

1. **Microcontrôleur (ESP32) :**
   * Ouvrir `balance_smart_v5.ino` dans l'IDE Arduino ou PlatformIO.
   * Renseigner vos identifiants Wi-Fi et les coordonnées de votre broker MQTT.
   * Téléverser le code sur la carte ESP32.

2. **Tableau de bord Web :**
   * Ouvrir simplement `dashboard_balance.html` dans n'importe quel navigateur web.
   * Configurer l'adresse du broker MQTT dans l'interface si nécessaire pour recevoir les événements en direct.

---

## ⚡ Remarque sur les Performances (PoC vs Production)

Ce projet inclut une preuve de concept (PoC) testée sur le simulateur **Wokwi** avec un broker MQTT public gratuit. La latence observée lors des tests est principalement due à l'environnement de simulation et aux délais de routage des serveurs publics gratuits. 

> **Passage en Production :** L'utilisation de composants ESP32 physiques couplés à un broker MQTT privé ou dédié (ex: AWS IoT Core, HiveMQ Cloud ou Mosquitto local) permet d'obtenir une réactivité quasi-instantanée (<100 ms).
