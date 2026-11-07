# Suite et validation

- Confirmer ce que désigne « pignon bas » et la référence du moteur de focus.
- ESP32-C6FH4 flashé ; sélection BLE, lecture des angles et impulsion panoramique constatées sur un RS 3. L’authentification DJI éventuelle et le protocole d’appairage ne sont pas implémentés.
- Vérifier découverte, reconnexion, latence de coexistence et arrêt lors de chaque perte de liaison, y compris perte d'alimentation ESP. Relever les axes, signes et unités sur le RS 3.
- Identifier une commande focus BLE fondée sur des captures reproductibles avant de l'activer. Valider séparément les rappels absolus tilt/roll et les arrêts pendant un rappel.
- Ajouter découverte mDNS, sélection explicite et arbitrage des utilisateurs, puis protection de l'accès, avant un usage au-delà d'un réseau de test isolé.
- Projet amont en Rust ; ce port C ESP-IDF est indépendant et sa pertinence pour une PR amont reste à discuter après validation.

- Contrôler l’expiration de la batterie, dont la lecture a été confirmée par l’utilisateur ; les erreurs Ronin ne sont pas décodées. Tester les pertes/reprises Wi-Fi, BLE, mDNS et changement IP sans mouvement automatique.

- Valider DualSense et navigateur réels, Gamepad API en HTTP direct et avec relais localhost, axes et gâchette L2 ; tester focus perdu, onglet caché, arrêt, déconnexion et reconnexion avec stick maintenu. Vérifier que les anciennes sessions sont refusées.
