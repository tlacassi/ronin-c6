# Ronin C6 — prototype V1

Pont expérimental pour **DJI RS 3** : navigateur → Wi-Fi local → ESP32-C6 → Bluetooth LE → stabilisateur. ESP-IDF 5.2, C et FreeRTOS ; pas Arduino. À ouvrir dans VS Code avec l'extension officielle Espressif IDF. Aucun service PC n'est requis. La coexistence Wi-Fi station et BLE est activée ; les deux radios partagent le temps d'antenne 2,4 GHz.

## Statut réel

Le protocole DUML, la caractéristique `fff5` et la commande de vitesse proviennent du projet MIT [DJI-Ronin-Bluetooth-Connector](https://github.com/aronesztergomi/DJI-Ronin-Bluetooth-Connector), dont l'auteur rapporte un essai sur RS 3. L'ordre et le signe physiques des axes ne sont pas confirmés dans cette source. Ce port ESP n'a pas été essayé sur carte ni sur Ronin. Le BLE prêt confirme uniquement la découverte de la caractéristique, pas l'exécution d'une commande. Les commandes n'ont pas d'accusé de mouvement.

Joystick pan/tilt expérimental : commandes bornées à ±300 unités, réémises toutes les 100 ms ; délai de sécurité de 400 ms sans renouvellement puis demande neutre. Le Ronin est censé arrêter le mouvement à l'expiration de ses propres commandes si la liaison est perdue : comportement à vérifier impérativement. La communication Wi-Fi ou BLE peut subir des délais. L'arrêt logiciel n'est pas un arrêt d'urgence matériel.

La batterie est affichée comme valeur **expérimentale** : le décodage est issu d’un RS 3 Mini et doit être comparé à l’écran du RS 3. La donnée disparaît après 10 secondes sans mise à jour ou à la déconnexion. Les erreurs internes du Ronin ne sont pas décodées et sont indiquées indisponibles.

Focus, positions absolues et capture de position sont indisponibles : aucune commande de focus BLE fiable identifiée ; commande de position de la source non vérifiée et pas de lecture fiable des angles. Ne pas supposer que le pignon ou moteur de focus est contrôlable. Le point « pignon bas » reste à clarifier.

## Configuration et lancement

1. Installer ESP-IDF 5.2 et son extension VS Code. Ouvrir ce dossier et sélectionner la cible ESP32-C6. La carte doit disposer d'une alimentation adaptée et d'une flash suffisante (vérifier selon la carte).
2. Dans un terminal ESP-IDF : `idf.py menuconfig`. Sous **Ronin bridge**, saisir localement le SSID et le mot de passe Wi-Fi. Le nom BLE dans menuconfig reste facultatif : la sélection peut se faire depuis la page web. Ne pas partager `sdkconfig` ni les binaires compilés : ils contiennent les identifiants Wi-Fi. `sdkconfig` est ignoré par Git.
3. `idf.py build`, puis `idf.py -p /dev/cu.usbmodem... flash monitor` avec le port réel. Débrancher les autres clients BLE de l'application DJI. Le journal affiche `Open http://<adresse>/`. Ouvrir cette adresse depuis le même réseau ; à défaut, consulter la liste DHCP du routeur. Dans la page, choisir le Ronin détecté puis cliquer sur Connecter : le nom est mémorisé en NVS et utilisé à la reconnexion. La liste contient au maximum huit noms DJI/RS 3 observés depuis le démarrage, qui peuvent ne plus être à portée. Pour sélectionner un autre appareil, éteindre ou éloigner le Ronin connecté, attendre sa déconnexion, puis choisir le nouveau. Le nom local `http://ronin-c6.local/` est annoncé en mDNS : ouvrir la page avec ce nom permet au navigateur de réessayer après une coupure, y compris si l’adresse IP change, lorsque le réseau et le système prennent mDNS en charge. Sinon, retrouver l’adresse dans le routeur ou le moniteur série. Un seul ESP avec ce nom doit être présent sur le réseau.
La sélection et la découverte de la caractéristique BLE ne prouvent pas un appairage DJI. Si le Ronin demande une authentification ou un mot de passe, cette version ne la prend pas encore en charge ; ne pas modifier ou réinitialiser ses paramètres pour contourner cela. L’activation initiale reste à effectuer avec l’application DJI.

4. Premier essai : stabilisateur dégagé et retenu, vitesse réduite, prêt à couper son alimentation ; tester séparément les axes, sens, relâchement, perte de page, Wi-Fi et BLE. Ne pas monter de charge sensible avant validation.

L'interface HTTP est sans authentification ni chiffrement. Tout appareil du réseau pouvant atteindre le pont peut envoyer des commandes ; utiliser uniquement un réseau local de confiance, isolé et sans exposition Internet. Plusieurs utilisateurs ne sont pas arbitrés : une seule page de commande à la fois. Aucune confirmation de sécurité n'est apportée par l'absence de réponse BLE.

## Manette optionnelle

Le joystick virtuel est actif par défaut. Pour une DualSense connectée au Mac, cocher **Activer la manette** dans la page, appuyer sur un bouton si le navigateur ne l'a pas encore détectée, relâcher L2 et centrer le stick gauche pour armer. Maintenir L2 pour piloter le panoramique et l'inclinaison avec le stick gauche ; relâcher L2 arrête la demande. Le joystick virtuel reprend la priorité. Après perte de page, de manette ou de liaison, il faut relâcher L2 et centrer à nouveau. Les manettes sans correspondance Gamepad « standard » sont refusées.

Certains navigateurs exigent un contexte sécurisé pour la manette. Si la page HTTP directe ne détecte pas la manette, lancer sur le Mac le relais facultatif (Python 3, sans dépendance) :

```sh
python3 tools/ronin_proxy.py
```

Puis ouvrir **http://127.0.0.1:8765/** et laisser le terminal ouvert. Si le nom mDNS ne fonctionne pas : `python3 tools/ronin_proxy.py --esp http://192.168.1.50` en remplaçant l'adresse. Le relais écoute uniquement sur le Mac ; il relaie les échanges vers l'ESP. Le joystick virtuel reste utilisable directement sur la page de l'ESP. Ne pas exposer le relais ni le port de l'ESP sur Internet.

## Vérification hôte du protocole

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_duml.c main/duml.c -o /tmp/ronin-test
/tmp/ronin-test
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_motion_parse.c main/motion_parse.c -o /tmp/ronin-parse-test
/tmp/ronin-parse-test
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_telemetry.c main/telemetry.c main/duml.c -o /tmp/ronin-telemetry-test
/tmp/ronin-telemetry-test
```

La trame capturée de référence valide les CRC ; le test couvre également l'encodage des valeurs signées. Cela ne valide ni le BLE ni le mouvement. Voir `NOTICE` et `LICENSE.upstream` pour l'attribution.

La reconnexion BLE relance la recherche dès la déconnexion et vérifie périodiquement que le scan est actif. Une coupure efface la consigne côté ESP et invalide la session des commandes. La page doit recevoir la nouvelle session avant de piloter de nouveau. Un ordre déjà transmis au contrôleur BLE au moment d’une coupure ne peut pas être rétracté ; les arrêts et reprises restent à vérifier sur le matériel. Le comportement réel, y compris les pertes Wi-Fi/BLE simultanées et la résolution mDNS, reste à vérifier sur le matériel.

Simulation manette et relais (sur macOS avec JavaScriptCore et Python 3) :

```sh
/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc tests/test_gamepad.js
/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc tests/test_web_sim.js
python3 -m unittest tests/test_proxy.py
```

La simulation utilise une manette et un serveur ESP fictifs ; elle ne valide pas la compatibilité du navigateur, la manette physique, les arrêts radio ou les mouvements du Ronin.
