# Ronin C6 — prototype V1

Pont expérimental pour **DJI RS 3** : navigateur → Wi-Fi local → ESP32-C6 → Bluetooth LE → stabilisateur. ESP-IDF 5.2, C et FreeRTOS ; pas Arduino. À ouvrir dans VS Code avec l'extension officielle Espressif IDF. Aucun service PC n'est requis. La coexistence Wi-Fi station et BLE est activée ; les deux radios partagent le temps d'antenne 2,4 GHz.

## Statut réel

Le protocole DUML, la caractéristique `fff5` et la commande de vitesse proviennent du projet MIT [DJI-Ronin-Bluetooth-Connector](https://github.com/aronesztergomi/DJI-Ronin-Bluetooth-Connector), dont l'auteur rapporte un essai sur RS 3. L'ordre et le signe physiques des axes ne sont pas confirmés dans cette source. Ce port ESP n'a pas été essayé sur carte ni sur Ronin. Le BLE prêt confirme uniquement la découverte de la caractéristique, pas l'exécution d'une commande. Les commandes n'ont pas d'accusé de mouvement.

Joystick pan/tilt expérimental : commandes bornées à ±300 unités, réémises toutes les 100 ms ; délai de sécurité de 400 ms sans renouvellement puis demande neutre. Le Ronin est censé arrêter le mouvement à l'expiration de ses propres commandes si la liaison est perdue : comportement à vérifier impérativement. La communication Wi-Fi ou BLE peut subir des délais. L'arrêt logiciel n'est pas un arrêt d'urgence matériel.

Focus, positions absolues et capture de position sont indisponibles : aucune commande de focus BLE fiable identifiée ; commande de position de la source non vérifiée et pas de lecture fiable des angles. Ne pas supposer que le pignon ou moteur de focus est contrôlable. Le point « pignon bas » reste à clarifier.

## Configuration et lancement

1. Installer ESP-IDF 5.2 et son extension VS Code. Ouvrir ce dossier et sélectionner la cible ESP32-C6. La carte doit disposer d'une alimentation adaptée et d'une flash suffisante (vérifier selon la carte).
2. Dans un terminal ESP-IDF : `idf.py menuconfig`. Sous **Ronin bridge**, saisir localement le SSID, le mot de passe et le nom BLE exact du RS 3, tel qu'affiché dans l'application DJI. Ne pas partager `sdkconfig` ni les binaires compilés : ils contiennent les identifiants Wi-Fi. `sdkconfig` est ignoré par Git.
3. `idf.py build`, puis `idf.py -p /dev/cu.usbmodem... flash monitor` avec le port réel. Débrancher les autres clients BLE de l'application DJI. Le journal affiche `Open http://<adresse>/`. Ouvrir cette adresse depuis le même réseau ; à défaut, consulter la liste DHCP du routeur. La découverte mDNS automatique n'est pas encore implémentée.
4. Premier essai : stabilisateur dégagé et retenu, vitesse réduite, prêt à couper son alimentation ; tester séparément les axes, sens, relâchement, perte de page, Wi-Fi et BLE. Ne pas monter de charge sensible avant validation.

L'interface HTTP est sans authentification ni chiffrement. Tout appareil du réseau pouvant atteindre le pont peut envoyer des commandes ; utiliser uniquement un réseau local de confiance, isolé et sans exposition Internet. Plusieurs utilisateurs ne sont pas arbitrés : une seule page de commande à la fois. Aucune confirmation de sécurité n'est apportée par l'absence de réponse BLE.

## Vérification hôte du protocole

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_duml.c main/duml.c -o /tmp/ronin-test
/tmp/ronin-test
```

La trame capturée de référence valide les CRC ; le test couvre également l'encodage des valeurs signées. Cela ne valide ni le BLE ni le mouvement. Voir `NOTICE` et `LICENSE.upstream` pour l'attribution.
