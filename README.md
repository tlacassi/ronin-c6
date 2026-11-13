# Ronin C6 — prototype V2

Pont expérimental pour DJI RS 3, avec découverte et sélection préparées pour les **RS 4, RS 4 Pro et RS 5** : navigateur → Wi-Fi local → ESP32-C6 → Bluetooth LE → stabilisateur. ESP-IDF 5.2, C et FreeRTOS ; pas Arduino. À ouvrir dans VS Code avec l'extension officielle Espressif IDF. Aucun service PC n'est requis. La coexistence Wi-Fi station et BLE est activée ; les deux radios partagent le temps d'antenne 2,4 GHz.

## Statut réel

Le protocole DUML, la caractéristique `fff5` et la commande de vitesse proviennent du projet MIT [DJI-Ronin-Bluetooth-Connector](https://github.com/aronesztergomi/DJI-Ronin-Bluetooth-Connector), dont l'auteur rapporte un essai sur RS 3 seulement. L'ordre et le signe physiques des axes ne sont pas confirmés dans cette source. Ce port a été flashé sur un ESP32-C6 et relié à un RS 3 : un mouvement à vitesse limitée et son arrêt ont été observés, et un essai panoramique absolu a été confirmé par télémétrie. La découverte d'un appareil RS 4, RS 4 Pro ou RS 5 par nom BLE ne valide ni son service de commande ni ses mouvements. Le pilotage RS 3 reste disponible. Sur demande de l’utilisateur, le joystick et les rappels de presets RS 4 Pro sont ouverts aux essais, avec les mêmes limites, expiration de consigne et contrôles de session. Sur RS 4 Pro, la batterie a été confirmée à 32 % et une lecture passive des angles 04/05 (charge de 49 octets) est implémentée : le pan est confirmé par l’utilisateur ; le tilt est lu au décalage 20 et son signe est inversé à l’envoi de la commande absolue, après deux essais isolés de 3° ; le roll réel a été retrouvé au décalage 22 du payload et a suivi une inclinaison physique confirmée d’environ 40° (+41,7° reçu). Le mode d’essai ne constitue pas une validation physique des commandes RS 4 Pro : repère, sens, précision et arrêt pendant un rappel restent à vérifier. Le champ HTTP `controlEnabled` autorise les commandes, tandis que `protocolVerified` conserve le statut des essais physiques. Les anciens presets Pro enregistrés avec les champs tilt/roll erronés sont ignorés et doivent être réenregistrés ; les presets RS 3 gardent leur format. Le RS 4 standard et le RS 5 restent non validés. Le BLE prêt confirme uniquement la découverte de la caractéristique, pas l'exécution d'une commande. Les commandes n'ont pas d'accusé de mouvement.

Joystick pan/tilt expérimental : commandes bornées à ±600 unités à 200 % (±300 à 100 %), réémises toutes les 100 ms ; délai de sécurité de 400 ms sans renouvellement puis demande neutre. Les curseurs indépendants vont de 10 à 200 % pour le joystick virtuel et la manette et de 0,5 à 5,0 s pour la durée indicative des presets ; leur comportement dépend du Ronin. Le Ronin est censé arrêter le mouvement à l'expiration de ses propres commandes si la liaison est perdue : comportement à vérifier impérativement. La communication Wi-Fi ou BLE peut subir des délais. L'arrêt logiciel n'est pas un arrêt d'urgence matériel.

La batterie est affichée à partir des notifications BLE ; sa lecture a été confirmée par l’utilisateur. La donnée disparaît après 10 secondes sans mise à jour ou à la déconnexion. Les erreurs internes du Ronin ne sont pas décodées et sont indiquées indisponibles.

Les angles sont affichés quand une télémétrie récente est reçue. Après abonnement aux notifications sur RS 3, le pont rétablit les demandes d'état `0x04/0x12` à 1 Hz : les trois charges capturées de l'app sont envoyées successivement, puis la dernière est répétée. Le flux passif peut ne pas inclure les angles. Les demandes viennent des captures de [dji_rs3_control](https://github.com/jdesbonnet/dji_rs3_control/blob/main/rs3_ble_protocol_spec.md) et ont rétabli les angles sur notre RS 3 ; leurs effets ne sont pas généralisés aux autres modèles. Une position vieille de 3 secondes reste indisponible. `/status` expose les compteurs de réception, de trames valides et d'angles, l'abonnement et l'âge du dernier angle pour diagnostiquer une perte. Cinq presets peuvent être enregistrés dans la mémoire de l’ESP, rappelés ou supprimés. Le rappel panoramique a été essayé en télémétrie ; les mouvements absolus des autres axes et les arrêts pendant un rappel nécessitent une validation distincte. Le zoom Focus Pro Motor est maintenant disponible sur RS 3 et RS 4 Pro. Sur RS 3, la trame moteur a été capturée pendant une variation physique et une petite commande aller-retour a rejoint exactement les deux positions demandées ; le RS 4 Pro conserve son adresse et son chemin existants.

## Configuration et lancement

1. Télécharger l’app macOS depuis les [Releases](https://github.com/tlacassi/ronin-c6/releases/latest), puis la décompresser. Aucun SDK ni dossier de projet n’est nécessaire côté utilisateur. ESP-IDF 5.2 reste l’environnement de développement du firmware.
2. Pour l’utilisation normale, ouvrir l’app macOS autonome : elle contient le firmware et l’outil de flash. Brancher l’ESP32-C6 sur son USB natif, installer le firmware intégré, puis enregistrer le Wi-Fi dans **Configurer l’ESP**. Le réseau est conservé sur l’ESP et peut être changé en USB sans reflash. Les anciennes options SSID/mot de passe de menuconfig ne sont plus utilisées par le démarrage réseau.
3. Ouvrir l’onglet de contrôle sur le même réseau. Utiliser `http://ronin-c6.local/`, ou l’IP affichée par la lecture USB. Le choix de la cible BLE reste disponible dans **Choisir ou changer de Ronin**.

4. Premier essai : stabilisateur dégagé et retenu, vitesse réduite, prêt à couper son alimentation ; tester séparément les axes, sens, relâchement, perte de page, Wi-Fi et BLE. Ne pas monter de charge sensible avant validation.

L'interface HTTP est sans authentification ni chiffrement. Tout appareil du réseau pouvant atteindre le pont peut envoyer des commandes ; utiliser uniquement un réseau local de confiance, isolé et sans exposition Internet. Plusieurs utilisateurs ne sont pas arbitrés : une seule page de commande à la fois. Aucune confirmation de sécurité n'est apportée par l'absence de réponse BLE.

## Manette optionnelle

Le joystick virtuel est actif par défaut. Une DualSense connectée au Mac est disponible automatiquement : l’icône 🎮 s’allume lorsqu’elle est détectée. Appuyer sur un bouton si le navigateur ne l’a pas encore exposée, relâcher L2 et centrer le stick gauche pour armer. Maintenir L2 pour piloter le panoramique et l'inclinaison avec le stick gauche ; relâcher L2 arrête la demande. Le joystick virtuel reprend la priorité. Après perte de page, de manette ou de liaison, il faut relâcher L2 et centrer à nouveau. Les manettes sans correspondance Gamepad « standard » sont refusées. Les boutons ✕, ○, □ et △ rappellent respectivement les presets 1, 2, 3 et 4, affichés avec leur symbole. Un appui court rappelle le preset au relâchement ; les emplacements vides sont ignorés. Maintenir le symbole pendant une seconde enregistre ou remplace ce preset avec la position actuelle (et le zoom disponible), une seule fois par maintien, sans rappel au relâchement. Maintenir ↑ zoome et maintenir ↓ dézoome à la vitesse du zoom manuel ; relâcher arrête le zoom. Ces raccourcis ne nécessitent pas L2. Une touche déjà maintenue à la détection, après reconnexion ou au retour de la page doit être relâchée avant de fonctionner. Des directions opposées sont annulées. Le joystick tactile et un rappel de preset reprennent la priorité sur le zoom directionnel. Le rappel attend la confirmation HTTP de la demande neutre avant d’envoyer la position.

Certains navigateurs exigent un contexte sécurisé pour la manette. Si la page HTTP directe ne détecte pas la manette, lancer sur le Mac le relais facultatif (Python 3, sans dépendance) :

```sh
python3 tools/ronin_proxy.py
```

Puis ouvrir **http://127.0.0.1:8765/** et laisser le terminal ouvert. Si le nom mDNS ne fonctionne pas : `python3 tools/ronin_proxy.py --esp http://192.168.1.50` en remplaçant l'adresse. Le relais écoute uniquement sur le Mac ; il relaie les échanges vers l'ESP. Le joystick virtuel reste utilisable directement sur la page de l'ESP. Ne pas exposer le relais ni le port de l'ESP sur Internet.

## Application macOS

**ESP Ronin Control 2** est autonome : firmware générique, outils de flash Intel/Apple Silicon et runtimes sont embarqués dans le `.app`. La configuration du Wi‑Fi se fait par USB et reste sur l’ESP, sans projet ni ESP-IDF à installer sur le Mac utilisateur. Voir [macos/README.md](macos/README.md) pour l’utilisation et la reconstruction par un mainteneur.

## Zoom avec le Focus Pro Motor

Le moteur branché et calibré qui entraîne la bague de zoom est détecté automatiquement sur RS 3 et RS 4 Pro. La page affiche sa position réelle en pourcentage et en unités moteur (0–4095), avec curseur, boutons +/− et arrêt du zoom, dès qu'une télémétrie récente et calibrée est reçue. Le RS 3 utilise l’adresse DUML `DF` du moteur, tandis que le RS 4 Pro conserve `BF` ; le décodeur vérifie le CRC, la direction, la longueur, le type et l’état de calibration. Sur le RS 3 connecté, les positions reçues ont changé pendant la manipulation physique. Un essai limité à 1 % de vitesse a atteint 3927 depuis 3947, puis est revenu à 3947 ; les axes du stabilisateur n’ont pas été commandés. Cette vérification couvre ce petit déplacement sur ce montage. Les pourcentages représentent la course calibrée du moteur, pas la focale en millimètres. Le bouton **Inverser le zoom** change immédiatement le sens du curseur, des boutons +/− et des flèches de toute manette Gamepad standard (Xbox ou PlayStation), ainsi que les pourcentages affichés, y compris ceux des presets. Le réglage est conservé dans le stockage local du navigateur pour cette adresse de page. La bascule arrête le zoom courant et demande de relâcher une flèche maintenue avant de repartir ; elle ne lance pas de mouvement opposé. Les valeurs moteur brutes de l’API et des presets restent inchangées, ce qui préserve les positions enregistrées. Les vitesses manuelle et de preset sont indépendantes (1–100 % de course par seconde). À 20 %, une course complète demande environ cinq secondes, hors réponse mécanique du moteur.

Enregistrer un preset ajoute la position du moteur s’il est disponible et calibré. Au rappel, axes et zoom démarrent ensemble avec leurs réglages respectifs. Moteur absent, données périmées ou preset ancien sans zoom : les axes restent utilisables et aucune nouvelle position moteur n’est commandée. Les anciens presets sont conservés ; réenregistrer uniquement ceux auxquels on veut ajouter du zoom. Après changement de montage, d’objectif ou de calibration, réenregistrer les valeurs de zoom concernées.

La lecture et la commande ont d’abord été vérifiées sur le RS 4 Pro avec l’adresse DUML `BF`, puis sur le RS 3 avec `DF` ; les deux utilisent le type de moteur `00`. Le petit pas RS 3 3947→3927→3947 a été lu exactement en télémétrie. Le RS 4 standard, le RS 5, un second moteur et les calibrations automatiques depuis l’ESP ne sont pas activés. La calibration se fait avec les commandes DJI. La commande 04/2f de position est émise à 100 Hz pendant une rampe bornée puis un court maintien final ; une commande isolée était ignorée sur RS 4 Pro, tandis qu’une séquence de 200 ms a atteint la cible mesurée. Le pas RS 4 Pro 4094→4074 a aussi été observé en télémétrie.

Le navigateur renouvelle l’activité du zoom toutes les 500 ms pendant une commande. Une absence de renouvellement pendant 1,5 s, une perte de moteur/calibration/session, un défaut d’écriture BLE ou une interruption prolongée de la tâche arrête l’émission des positions. La tâche zoom est indépendante du contrôle pan/tilt à 10 Hz et utilise un stockage statique. Un dernier point déjà transmis peut encore être exécuté par le moteur ; cette limite ne remplace pas un arrêt matériel.

Les blobs NVS axes seuls de 44 octets restent lisibles ; les nouveaux blobs de 48 octets ajoutent le zoom optionnel dans la même écriture. `/presets` conserve sa forme existante ; `/zoom/presets` expose les valeurs optionnelles. `/zoom`, `/zoom/keepalive` et `/zoom/stop` exigent la session courante. `/preset/goto` accepte toujours l’ancien corps `index,durée` et accepte aussi `index,durée,vitesseZoom` (exemple `0,20,020`).

## Vérification hôte du protocole

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_duml.c main/duml.c -o /tmp/ronin-test
/tmp/ronin-test
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_motion_parse.c main/motion_parse.c -o /tmp/ronin-parse-test
/tmp/ronin-parse-test
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_telemetry.c main/telemetry.c main/duml.c -o /tmp/ronin-telemetry-test
/tmp/ronin-telemetry-test
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_zoom_models.c -o /tmp/ronin-zoom-models-test
/tmp/ronin-zoom-models-test
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_zoom_motion.c main/zoom_motion.c -o /tmp/ronin-zoom-motion-test
/tmp/ronin-zoom-motion-test
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_preset_data.c main/preset_data.c -o /tmp/ronin-preset-test
/tmp/ronin-preset-test
```

Mise à jour du firmware dans l'app macOS (Swift + faux serveur de releases local) :

```sh
swiftc -parse-as-library tests/test_firmware_store.swift macos/Sources/FirmwareUpdater.swift -o /tmp/ronin-fw-test
python3 tests/firmware_server.py > /tmp/ronin-fw-port & sleep 1
/tmp/ronin-fw-test "$(head -1 /tmp/ronin-fw-port)"; kill %1
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

## Diagnostic BLE local

Sur RS 3, l'interface affiche l'état de veille à partir de la notification `04/27` et propose les commandes de veille et de réveil `04/0f`. La mise en veille et le réveil peuvent verrouiller ou déverrouiller automatiquement les axes selon les réglages du Ronin. L'état affiché est celui de la veille, pas une mesure individuelle des verrous mécaniques. L'état devient indisponible après 10 secondes sans notification ou à la perte de la liaison ; les commandes sont limitées au RS 3 et demandent une session et un état récents, sans mouvement de joystick ni zoom en cours. Le RS 4 Pro n'est pas encore validé pour cette fonction. Voir la [description des trames RS 3](https://github.com/jdesbonnet/dji_rs3_control/blob/main/rs3_ble_protocol_spec.md).

L'option ESP-IDF `Ronin bridge > Trace received DUML frames over the local serial console` est désactivée par défaut. Elle copie les trames reçues valides dans une file de taille fixe et diffère leur sortie hexadécimale à la tâche applicative, sans bloquer le rappel NimBLE sur la sortie série. Une capture peut perdre des trames si la file est pleine. Les journaux restent locaux. `/status.telemetry.disconnectReason` conserve le dernier motif de déconnexion NimBLE pour diagnostiquer les coupures. Activer cette trace ne déverrouille aucune commande de mouvement et n'envoie aucune demande d'état aux modèles non validés.

Le format de position diffère entre les deux appareils essayés : RS 3 utilise les tags 22/23/24 de 04/66, RS 4 Pro pousse passivement 04/05 avec une charge de 49 octets. Le décodeur Pro accepte uniquement la longueur, le sens et les plages capturés, avec CRC valide ; il ne réutilise pas la position RS 3 pour le Pro. Tilt, roll et pan proviennent respectivement des entiers signés aux décalages 20, 22 et 4 du payload. Le champ historique de tilt au décalage 0 donnait environ ±180° caméra de niveau et provoquait un rappel vers le bas ; le roll historique au décalage 2 restait près de zéro malgré une inclinaison réelle. Les champs 20/22 sont également décrits dans cette [capture indépendante de Gimbal.State](https://github.com/cybersecua/dji/blob/main/DUMLmap/DUMLmap.py), sur un autre matériel DJI ; les essais locaux du Pro restent la référence pour ce port.

Les consignes Pro conservent le pan et le roll, mais inversent le signe du tilt avant l'encodage 04/14. Un essai isolé non corrigé avec cible filaire +3° a donné −3° ; après correction, la cible affichée −6° a été mesurée à −6°, sans variation notable du pan/roll. Un rappel complet court a aussi atteint ce tilt, avec une coupure/reprise BLE pendant l'essai. Le roll cible 0° a été mesuré vers −0,5° : aucune compensation fixe ni correction automatique supplémentaire n'est appliquée. Ces observations ne qualifient pas encore les rappels sur toute la plage mécanique ni après manipulation de la poignée. Les anciens presets Pro sont écartés par la version RSP3 et doivent être réenregistrés ; aucune conversion approximative ni suppression NVS automatique n'est faite. Les presets RS 3 sont conservés.

## WebSocket de contrôle (`/ws`)

Le joystick, le zoom et l'état passent par une WebSocket quand elle est disponible ; toutes les routes HTTP (`/move`, `/status`, `/zoom*`, `/preset*`, …) restent actives et servent de repli automatique. Presets, veille, réglage LED et appairage restent en HTTP.

**Format** (`main/ws_protocol.c`, sans dépendance ESP-IDF) : une trame texte = une lettre + une charge, 64 octets au plus. Le texte réutilise les parseurs stricts des corps HTTP (`motion_parse`, `zoom_parse`), donc mêmes bornes (±600, 0–4095, vitesse 1–100), sans cJSON ni allocation sur le chemin des commandes, et se lit tel quel dans les outils du navigateur. Le JSON aurait coûté 3 à 4 fois plus d'octets par commande et un second parseur à durcir.

| Client → ESP | Rôle |
|---|---|
| `h<session>` | s'attache à la session BLE courante (la même valeur que `X-Ronin-Session`) |
| `m<x>,<y>` | consigne joystick |
| `z<position>,<vitesse>` · `x` · `k` | zoom, arrêt du zoom, renouvellement du bail |
| `p<ms>` | ping horodaté |

| ESP → client | Rôle |
|---|---|
| `s<json>` | état, strictement le corps de `GET /status`, poussé toutes les 250 ms (remplace le polling à 1 Hz) |
| `o<ms>` | pong : écho de l'horodatage, la page en déduit le RTT |
| `r1` / `r0` | pilote / observateur en lecture seule |
| `e<code>` | `bad`, `role`, `sess`, `link`, `zoom`, `busy` |

**Arrêt sûr.** Une seule socket pilote (`ws_arbiter.c`) ; les autres sont observateurs et ne peuvent pas commander. Une socket doit envoyer `h<session>` avec la session courante, puis `ronin_ble_*` revalide cette session à chaque commande. Fermeture de la socket (y compris purge LRU, car toute fermeture passe par `close_fn`), erreur de lecture, trame invalide, hors bornes, binaire, fragmentée ou trop longue : `ronin_ble_stop()` et arrêt du zoom, puis fermeture. Si le pilote ne donne plus signe de vie pendant 2,5 s, la consigne et le zoom sont arrêtés ; au-delà de 6 s, toute socket muette est fermée. Le watchdog de 400 ms et le bail de zoom de 1,5 s restent actifs quel que soit l'état de la socket.

**Origin.** Au handshake, `Origin` doit être `http://` suivi du même hôte que `Host` ; une valeur absente, `null` ou étrangère est refusée (anti cross-site WebSocket hijacking). Esp-idf envoie le `101` avant d'appeler le gestionnaire : une page étrangère voit donc l'ouverture suivie d'une fermeture immédiate, sans qu'aucune commande ne soit jamais acceptée. Les clients non navigateur doivent envoyer `Origin`. Aucune authentification n'est ajoutée ici, et rien ne la rend plus difficile : le contrôle d'accès pourra s'ajouter au handshake et dans `hello`.

**Limites.** 3 sockets WebSocket au maximum (1 pilote, 2 observateurs), prises sur les 7 sockets du serveur ; une 4e reçoit `ebusy` et est fermée. Les pages en WebSocket n'utilisent plus `/status` ni `/move`, ce qui allège la saturation connue. L'envoi d'état se fait depuis une tâche dédiée (`httpd_ws_send_frame_async`) ; un client bloqué est fermé. Une page observatrice ou en repli commande par HTTP, qui reste sans arbitrage comme avant.

**Page.** Elle ouvre `/ws` si le navigateur le permet, affiche « Liaison : WebSocket · RTT n ms », passe en « Liaison dégradée — repli HTTP » si les pong manquent plus de 2,5 s (RTT > 250 ms en avertissement), et se reconnecte avec un délai croissant (0,5 s à 8 s). À chaque bascule ou perte, elle arrête la consigne et le zoom, puis exige que le joystick ou la gâchette L2 soit relâché et recentré avant de reprendre. Elle ferme la socket quand l'onglet est masqué.

**Relais** (`tools/ronin_proxy.py`) : il relaie aussi `/ws`, toujours en bibliothèque standard. Sans cela, le cas d'usage du relais (manette en contexte sécurisé) retomberait toujours en HTTP. Il vérifie `Host` et `Origin` côté navigateur, présente à l'ESP sa propre autorité comme `Host` et `Origin`, puis relaie les octets sans analyser les trames ; fermer un côté ferme l'autre, donc l'ESP arrête le mouvement. 3 relais simultanés au maximum.

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_ws_protocol.c main/ws_protocol.c main/ws_arbiter.c main/motion_parse.c -o /tmp/ronin-ws-test
/tmp/ronin-ws-test
```

Le serveur HTTP ferme la connexion la moins récente lorsqu'il atteint sa limite de sockets pour rester accessible avec plusieurs pages de pilotage ouvertes.
