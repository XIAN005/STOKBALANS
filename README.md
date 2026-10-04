# 📦 STOKBALANS — Balance intelligente de gestion d'inventaire

Projet final N°3 de la **Formation électronique et prototypage , EIC 3.0**.

Une balance connectée conçue pour les coopératives et les petits commerces (sacs de céréales, de café, etc.). Les 4 cellules de charge mesurent le poids, le module HX711 amplifie et numérise le signal, puis l'ESP32 affiche la pesée sur un écran LCD et la transmet à un tableau de bord web qui conserve le journal d'inventaire, calcule les totaux et émet des alertes.


<img width="1366" height="768" alt="2026-10-04_10-08" src="https://github.com/user-attachments/assets/9f200287-d41d-4376-826b-9a653c5d4f26" />


## Ce que fait le projet

| Fonction | Où |
|---|---|
| Poids en temps réel | Écran LCD et tableau de bord |
| Journal de chaque pesée avec **date et heure** | Tableau de bord |
| **Total pesé** sur une période (heure, jour, 7 jours, personnalisée) | Tableau de bord |
| **Alerte** si un poids dépasse ou descend sous un seuil réglable | LCD (message + rétroéclairage qui clignote) et tableau de bord |
| Tare, calibration et réglage des seuils à distance | Tableau de bord |
| Export du journal en CSV (Excel) | Tableau de bord |

### 🎓 Encadrement

* **Formateur / Superviseur :** Carlos Yfrazin

### 👥 Équipe du projet STOKBALANS

* **Nicolas Christian Toussaint** — *Développeur principal & Architecte matériel*
* **Pintro Marc-Kelly** — *Responsable communication & Pitch*
* **Obeus Boladinio** — *Support technique & Relecture*
* **Samuel Jefferson Pierre Louis** — *Support & Tests*

## Fichiers

| Fichier | Rôle |
|---|---|
| `STOKBALANS.ino` | Programme de l'ESP32 (lecture du capteur, LCD, envoi MQTT) |
| `dashboard_balance.html` | Tableau de bord web (page unique, à ouvrir dans un navigateur) |
| `diagram.json` | Schéma du circuit Wokwi voi  |
| `README.md` | Ce document |

## Comment ça marche

```
Sac posé → 4 cellules de charge → HX711 → ESP32 ──► LCD (poids, alertes)
                                            │
                                            │ Wi-Fi + MQTT (broker public gratuit)
                                            ▼
                                  broker.hivemq.com
                                            │
                                            ▼  (WebSocket sécurisé)
                              dashboard_balance.html (navigateur)
```

- **MQTT** est un système de messages : l'ESP32 *publie* ses pesées sur un « canal », et la page web, abonnée à ce canal, les reçoit aussitôt.
- Aucun serveur à installer, aucun compte à créer, à part un compte Wokwi gratuit pour la simulation.

---

## 🧪 Tester le projet en simulation (sans matériel)

### Ce qu'il vous faut

- Un navigateur récent (Chrome, Firefox, Edge) et une connexion internet.
- Un compte gratuit sur [wokwi.com](https://wokwi.com).

### Étape 1 — Créer le projet Wokwi

1. Sur Wokwi, créez un nouveau projet **ESP32**.
2. Ouvrez l'onglet **`diagram.json`**, supprimez son contenu et collez celui du fichier `diagram.json` du dépôt. Le circuit apparaît.
   *Si vous n'avez pas ce fichier, recréez le circuit avec le tableau « Câblage » plus bas.*
3. Ouvrez l'onglet **`libraries.txt`** et remplacez son contenu par :
   ```
   HX711
   LiquidCrystal I2C
   PubSubClient
   ```
4. Ouvrez **`sketch.ino`**, faites **Ctrl+A**, puis collez tout le contenu de `STOKBALANS.ino`.

### Étape 2 — Choisir votre nom de canal (important)

Le serveur MQTT est **public et partagé**. Si deux personnes utilisent le même nom de canal, leurs balances se mélangent.

1. Dans `sketch.ino`, repérez la ligne :
   ```cpp
   const char* PREFIX = "eic3/groupe3/balance-x7k2";
   ```
2. Remplacez-la par un nom que vous seul utilisez, par exemple `"stokbalans/prenom-4821"`.
3. Dans `dashboard_balance.html`, ouvrez le fichier avec un éditeur de texte et modifiez la ligne :
   ```js
   const PREFIX = 'eic3/groupe3/balance-x7k2';
   ```
   avec **exactement le même nom**.

### Étape 3 — Lancer

1. Ouvrez `dashboard_balance.html` en double-cliquant dessus (il s'ouvre dans votre navigateur).
2. Dans Wokwi, cliquez sur le bouton vert **▶**.
3. **Placez les deux fenêtres côte à côte** (deux fenêtres, pas deux onglets). Wokwi ralentit quand sa fenêtre n'est pas visible, ce qui coupe la connexion.
4. Dans le moniteur série de Wokwi, vous devez voir :
   ```
   WiFi connecte !
   MQTT : connexion... OK
   ```
   Sur la page, les deux pastilles passent au vert : **Serveur MQTT : connecté** et **Balance : en ligne**.

### Étape 4 — Scénario de test

Le capteur de poids de Wokwi a un curseur **Pressure** : il simule un sac posé sur la balance. Cliquez sur le capteur pour le faire apparaître.

| # | Action | Résultat attendu |
|---|---|---|
| 1 | Curseur à **0** | LCD « Plateau vide », page « Plateau vide » |
| 2 | Curseur à **20 kg**, attendez 1 seconde | Une pesée de 20 kg apparaît dans le journal (statut **Normal**), avec une notification |
| 3 | Remettez à **0** | Le plateau est de nouveau prêt |
| 4 | Curseur à **50 kg** | **Alerte** « poids au-dessus du seuil max », le LCD clignote, la pesée est marquée **Au-dessus du seuil** |
| 5 | Remettez à 0, puis **2 kg** | **Alerte** « poids sous le seuil min » |
| 6 | Changez les seuils (ex. min 10, max 30), cliquez **Enregistrer** | Les alertes suivantes utilisent les nouvelles valeurs |
| 7 | Choisissez **Aujourd'hui** dans « Total pesé » | Le total et le nombre de pesées se mettent à jour |
| 8 | Cliquez **Exporter (Excel/CSV)** | Un fichier `stokbalans_journal.csv` est téléchargé |
| 9 | Plateau vide, cliquez **Tare** | La balance se remet à zéro |

> **Comment une pesée est enregistrée :** le poids doit être **stable pendant 0,6 s**, supérieur à **0,5 kg**, et différer de plus de **1 kg** de la pesée précédente. Si vous ajoutez un 2ᵉ sac sans vider le plateau, seul le poids **ajouté** est compté.

### Calibration

Le facteur de calibration par défaut (420) vient d'un exemple Wokwi. Pour le recalculer :

1. Posez un poids connu (en simulation, réglez le curseur sur une valeur, par exemple 10 kg).
2. Attendez que le poids soit stable.
3. Saisissez ce poids dans « **Poids connu (kg)** » sur la page, puis cliquez **Calibrer**.
4. Une notification confirme le nouveau facteur, qui est conservé dans la mémoire de l'ESP32.

---

## 🖥️ Tester seulement le tableau de bord (sans Wokwi)

Vous pouvez essayer la page sans ESP32, avec un client MQTT en ligne.

1. Ouvrez `dashboard_balance.html`.
2. Ouvrez le client web de HiveMQ (`hivemq.com/demos/websocket-client`) et connectez-vous à `broker.hivemq.com`.
3. Publiez ces messages (en remplaçant le préfixe par le vôtre) :

| Topic | Message |
|---|---|
| `<PREFIX>/poids` | `{"poids":12.3,"etat":"ok"}` |
| `<PREFIX>/pesee` | `{"poids":25.5,"statut":0,"age":0}` |
| `<PREFIX>/pesee` | `{"poids":60,"statut":2,"age":0}` |

Le poids en direct, le journal, le graphique et le total se mettent à jour.

---

## 🔌 Tester avec le vrai matériel

### Composants

- 1 carte **ESP32**
- 1 module **HX711** (amplificateur pour cellules de charge)
- **4 cellules de charge** (plateforme de pesée)
- 1 écran **LCD 16×2 avec module I2C** (adresse `0x27`)
- Fils de connexion
  

<img width="603" height="438" alt="2026-10-03_20-38" src="https://github.com/user-attachments/assets/2207072a-e1cf-491c-a571-8197a8cc7c92" />


### Câblage

| Module | Broche module | Broche ESP32 |
|---|---|---|
| HX711 | `DT` | GPIO 18 |
| HX711 | `SCK` | GPIO 19 |
| HX711 | `VCC` / `GND` | 3,3 V / GND |
| LCD I2C | `SDA` | GPIO 21 |
| LCD I2C | `SCL` | GPIO 22 |
| LCD I2C | `VCC` / `GND` | 5 V / GND |
| Cellules de charge | combinées en pont complet | bornes `E+`, `E−`, `A+`, `A−` du HX711 |

Le canal B du HX711 n'est pas utilisé. Respectez le schéma de votre type de cellules (à 3 ou 4 fils) pour le pont complet.

### Adaptations du code

1. **Wi-Fi** : remplacez `ssid` et `password` par ceux de votre réseau.
2. **Canal Wi-Fi** : changez `WiFi.begin(ssid, password, 6);` en `WiFi.begin(ssid, password);`. Le « 6 » ne sert qu'à accélérer la connexion sous Wokwi.
3. **Calibration** : faites-la avec un poids connu (voir plus haut). Le facteur 420 n'est qu'un exemple.
4. Téléversez avec l'IDE Arduino (carte ESP32 ; bibliothèques `HX711`, `LiquidCrystal I2C` et `PubSubClient`).

---

## ⚙️ Paramètres modifiables dans `STOKBALANS.ino`

| Paramètre | Valeur par défaut | Effet |
|---|---|---|
| `PREFIX` | `eic3/groupe3/balance-x7k2` | Nom du canal MQTT (à rendre unique, identique dans la page) |
| `MQTT_HOST` | `broker.hivemq.com` | Serveur MQTT public |
| `seuilMin` / `seuilMax` | 5 / 45 kg | Seuils d'alerte (réglables depuis la page) |
| `SEUIL_PRESENCE` | 0,5 kg | En dessous, le plateau est considéré vide |
| `VARIATION_PESEE` | 1 kg | Écart minimal avec la pesée précédente |
| `DUREE_STABLE` | 600 ms | Temps de stabilité avant d'enregistrer une pesée |
| `calibration_factor` | 420 | Facteur de calibration (réglable depuis la page) |

## 📡 Messages MQTT

Tous les topics commencent par votre `PREFIX`.

| Topic | Sens | Contenu |
|---|---|---|
| `…/poids` | balance → page | `{"poids":18.5,"etat":"ok"}` (état : `ok`, `bas`, `haut`, `vide`, `mesure`, `err`) |
| `…/pesee` | balance → page | `{"poids":25.5,"statut":0,"age":0}` (statut : 0 normal, 1 sous le seuil, 2 au-dessus) |
| `…/seuils` | page → balance | `min;max` (conservé par le serveur) |
| `…/cmd` | page → balance | `tare` ou `cal:<poids en kg>` |
| `…/statut` | balance → page | `online` / `offline` |
| `…/info` | balance → page | Messages de confirmation (calibration) |

## 🛠️ Dépannage

| Problème | Cause probable et solution |
|---|---|
| La page affiche « Balance : hors ligne » | La simulation est en pause ou la fenêtre Wokwi est cachée : placez les deux fenêtres côte à côte |
| Le moniteur série affiche `MQTT : connexion... echec (code -2)` | Le réseau ou le serveur MQTT est indisponible : relancez, ou essayez `broker.emqx.io` (voir ci-dessous) |
| La page reste vide | Le `PREFIX` n'est pas identique dans le `.ino` et dans la page |
| Aucune pesée enregistrée | Poids sous 0,5 kg, variation inférieure à 1 kg, ou poids encore instable : remettez à 0 puis recommencez |
| « Tare refusée » dans le moniteur série | La tare n'est acceptée que plateau vide |
| « Calibration refusée » | Aucun poids n'est posé sur le capteur |
| LCD « Erreur HX711 » | Vérifiez le câblage `DT` / `SCK` (GPIO 18 / 19) |
| Simulation lente | Normal : Wokwi et le serveur public sont moins rapides qu'un vrai ESP32 |

**Changer de serveur MQTT** (deux lignes) :
- dans le `.ino` : `MQTT_HOST = "broker.emqx.io"`
- dans la page : `BROKER = 'wss://broker.emqx.io:8084/mqtt'`

## ⚠️ Limites connues

- **Serveur public** : tout le monde peut lire et écrire sur un canal si l'on en connaît le nom. Utilisez un nom long et aléatoire, et n'y envoyez rien de confidentiel.
- **Journal** : il est conservé dans le navigateur et ne reçoit que les pesées arrivées **page ouverte**. Changer de navigateur ou d'ordinateur donne un journal vide.
- **Wokwi gratuit** : le serveur web intégré de l'ESP32 n'est pas accessible depuis le navigateur sans abonnement. C'est pour cela que le tableau de bord passe par MQTT.
- **Calibration par défaut** : elle ne correspond à aucun capteur réel.

## 🚀 Pistes d'évolution

- Reconnaître le type de sac (céréales, café) et totaliser par produit.
- Conserver le journal côté serveur pour ne perdre aucune pesée.
- Buzzer ou LED d'alerte sur le matériel réel.
- Broker MQTT privé avec identifiants.

## Lien du projet Wokwi

 `https://wokwi.com/projects/476531704012050433`.

---

*Formation électronique et prototypage , EIC 3.0 — Projet final N°3 — STOKBALANS*
