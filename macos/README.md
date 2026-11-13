# ESP Ronin Control 2 — application autonome

L’utilisateur n’a besoin que de `ESP Ronin Control.app` (macOS 13 ou supérieur, Intel ou Apple Silicon). Le bundle contient l’interface native, les trois images du firmware ESP32-C6, leurs empreintes SHA-256, les outils de flash Espressif 5.6.0 et leurs runtimes pour les deux architectures. Aucun projet, Python, ESP-IDF, Homebrew ou téléchargement complémentaire n’est nécessaire pour utiliser l’app.

## Installer ou mettre à jour un ESP

1. Brancher l’ESP32-C6 via son port USB natif USB-Serial/JTAG.
2. Ouvrir **Configurer l’ESP**, choisir le port, puis **Installer le firmware intégré**.
3. Le flash vérifie les empreintes avant écriture et vérifie les données écrites. Il écrit le bootloader à `0x0`, la table des partitions à `0x8000` et l’application à `0x10000`. La zone NVS à `0x9000` reste intacte : presets, cible BLE et Wi‑Fi existant sont conservés. Ne pas débrancher pendant l’écriture.
4. Les anciens firmwares doivent être mis à jour une première fois pour disposer du protocole de configuration USB.

## Mise à jour automatique du firmware

Dans **Configurer l’ESP → Firmware**, l’option *Récupérer automatiquement le dernier firmware* est activée par défaut. L’app interroge alors la dernière release GitHub du dépôt, au lancement de l’écran et avant chaque installation.

- **Internet disponible et version plus récente :** le firmware est téléchargé, vérifié puis conservé dans `~/Library/Application Support/ESP Ronin Control/Firmware/current`.
- **Pas d’Internet, release sans firmware, fichier invalide ou serveur injoignable :** l’app se replie sur le dernier firmware téléchargé, ou sur celui intégré s’il est plus récent. L’installation n’est jamais bloquée par le réseau.
- **Option désactivée :** aucune connexion réseau ; l’app utilise le plus récent des firmwares déjà présents sur le Mac. Le bouton *Vérifier maintenant* télécharge à la demande.

Sécurité : HTTPS uniquement, hôtes limités à `github.com` et `*.githubusercontent.com` (redirections comprises), tailles bornées, SHA-256 comparé au manifeste et à la somme annoncée par GitHub, puis contrôle des en-têtes d’image (magie ESP, puce ESP32-C6, table des partitions). Le fichier est revérifié juste avant l’écriture. La confiance repose sur le compte GitHub du dépôt : les fichiers ne sont pas signés cryptographiquement.

### Publier un firmware pour les apps installées — mainteneurs uniquement

Après le build standalone, joindre les fichiers à la release (le nom du tag, par exemple `v2.1.0`, sert de numéro de version) :

```sh
python3 macos/package_firmware_assets.py
gh release upload vX.Y.Z macos/dist/firmware-assets/* --repo tlacassi/ronin-c6
```

Une release sans ces fichiers est simplement ignorée par les apps.

## Régler le réseau sans reflasher

L’USB reste utilisable même si le réseau enregistré est absent ou incorrect. Saisir le SSID 2,4 GHz et le mot de passe, puis **Enregistrer le Wi‑Fi sur l’ESP**. Le réseau est enregistré dans la NVS par le SDK Wi-Fi et l’ESP essaie immédiatement de le rejoindre. L’app affiche l’adresse IP obtenue ou l’échec de connexion ; un mauvais mot de passe peut être corrigé en USB. **Lire la configuration** affiche le SSID, l’état et l’adresse IP, jamais le mot de passe.

Le mot de passe n’est enregistré ni dans l’app, ni dans ses préférences, ses arguments de processus ou ses logs. Il transite par le câble USB puis reste sur l’ESP. Un réseau ouvert utilise un mot de passe vide ; sinon, 8 à 63 octets sont acceptés. Un port USB déjà utilisé par un moniteur doit être libéré avant cette opération.

## Contrôler le Ronin

Le Mac et l’ESP doivent être sur le même réseau. L’onglet de contrôle charge `http://ronin-c6.local`, ou une adresse IP saisie manuellement. Le relais GameController conserve le pilotage en arrière-plan quand cet onglet reste ouvert : gâchette gauche + stick, boutons de face pour presets (court : rappel ; long : sauvegarde), haut/bas pour zoom, double appui court L2 avec stick centré pour recentrer. Après une reprise BLE, relâcher la gâchette et recentrer le stick avant de piloter.

## Reconstruire une version distribuable — mainteneurs uniquement

ESP-IDF 5.2 et les outils de compilation macOS sont nécessaires seulement pour fabriquer une nouvelle version :

```sh
idf.py -B build-standalone -D SDKCONFIG="$PWD/sdkconfig.standalone" -D SDKCONFIG_DEFAULTS="$PWD/sdkconfig.standalone.defaults" build
./macos/build_app.sh
```

Le build standalone est distinct du build local ; il utilise des identifiants Wi-Fi vides. Le packaging refuse toute configuration non générique. `prepare_resources.py` récupère les archives officielles esptool et vérifie leurs SHA-256 fixés dans le code avant copie. Les téléchargements vont dans `macos/vendor`, ignoré par Git. Les licences sont intégrées au bundle ; voir `THIRD_PARTY.md`.

Le script produit `macos/build/ESP Ronin Control.app`. Le bundle est signé ad hoc, sans notarisation Apple. Copier **tout** le `.app` pour le déplacer, y compris `Contents/Resources` ; les autres copies déjà présentes sur le Mac ne sont pas remplacées automatiquement.
