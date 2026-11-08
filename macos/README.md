# ESP Ronin Control pour macOS

Application SwiftUI native qui prépare et flashe un ESP32-C6, puis ouvre l’interface web du Ronin dans une fenêtre intégrée. Elle ne nécessite ni serveur web séparé ni dépendance Swift externe.

## Construire l’application

Depuis la racine du dépôt, lance :

```sh
./macos/build_app.sh
```

Le script produit `macos/build/ESP Ronin Control.app`, une application universelle Apple Silicon et Intel, compatible macOS 13 ou supérieur. Elle peut être lancée depuis Finder ou avec `open "macos/build/ESP Ronin Control.app"`.

## Flasher et configurer le Wi‑Fi

1. Installe ESP-IDF 5.2 ou une version compatible sur le Mac.
2. Ouvre l’application, sélectionne la racine du dépôt et le dossier ESP-IDF (celui qui contient `export.sh`).
3. Branche l’ESP32-C6 en USB, choisis son port `/dev/cu.usb*`, entre le SSID et le mot de passe puis clique **Construire et flasher**.

L’app met à jour uniquement `CONFIG_RONIN_WIFI_SSID` et `CONFIG_RONIN_WIFI_PASSWORD` dans le `sdkconfig` local, puis lance `idf.py build flash`. Ces réglages sont conservés localement dans le fichier ignoré par Git. Le mot de passe ne va ni dans les préférences de l’application, ni dans les arguments ou les logs du processus. Il reste toutefois dans le firmware flashé, comme dans le fonctionnement actuel du projet. Chaque changement de réseau demande une nouvelle compilation et un nouveau flash.

Le moniteur série s’ouvre par défaut après le flash. Il affiche les logs de démarrage et l’adresse du pont ; le bouton **Arrêter** ferme le moniteur et libère le port. Le flash écrit le firmware ESP-IDF, sans effacer intentionnellement les presets de la partition NVS.

## Ouvrir le contrôle

L’onglet **Contrôler le Ronin** vérifie toutes les quatre secondes `http://ronin-c6.local/status`, puis charge l’interface du pont dans WebKit. Tu peux saisir une adresse IP manuellement si mDNS n’est pas disponible. macOS demande l’autorisation d’accès au réseau local au premier essai.

L’application ne crée pas de réseau Wi‑Fi et ne remplace pas l’application DJI pour l’appairage initial. Le Mac et l’ESP doivent rejoindre le même réseau pour accéder à l’interface de commande. Le firmware ESP-IDF et les outils Espressif restent nécessaires pour reconstruire et flasher.

## Manette en arrière-plan

Quand l’onglet **Contrôler le Ronin** est ouvert dans l’app, macOS GameController reçoit les entrées DualSense ou Xbox même si une autre app passe au premier plan. Le relais natif envoie directement au pont ESP les axes, presets (appui court pour rappeler, appui long pour enregistrer) et zoom par la croix directionnelle. Il reprend les vitesses, la durée des presets et l’inversion du zoom choisies dans la page intégrée. La gâchette gauche doit être relâchée, stick centré, puis maintenue pour piloter.

Le relais s’arrête si l’onglet de contrôle est fermé, si la manette est déconnectée ou si le Ronin perd sa liaison. macOS peut demander l’autorisation Bluetooth au premier usage. L’app doit rester ouverte pour le pilotage en arrière-plan.

## Limites de cette V1

- Le flash USB est réalisé avec `idf.py`, pas avec un firmware `.bin` téléchargé depuis Internet.
- Le SSID et le mot de passe sont compilés dans le firmware parce que le firmware actuel ne propose pas de configuration Wi‑Fi à chaud par USB.
- L’app ne déverrouille pas le Ronin et ne réalise pas l’appairage DJI.
- Le contrôle matériel dépend toujours de la liaison Wi‑Fi/BLE et des limites décrites dans le README racine.
