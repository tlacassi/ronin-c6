# Suite et validation

- Confirmer ce que désigne « pignon bas » et la référence du moteur de focus.
- Identifier la carte C6 exacte ; flasher après configuration Wi-Fi locale. Vérifier la liste, la sélection et la mémorisation du Ronin depuis le navigateur. L’authentification DJI éventuelle et le protocole d’appairage ne sont pas implémentés.
- Vérifier découverte, reconnexion, latence de coexistence et arrêt lors de chaque perte de liaison, y compris perte d'alimentation ESP. Relever les axes, signes et unités sur le RS 3.
- Identifier une commande focus BLE fondée sur des captures reproductibles avant de l'activer. Chercher une lecture de position et valider une commande absolue avant de proposer des mémoires.
- Ajouter découverte mDNS, sélection explicite et arbitrage des utilisateurs, puis protection de l'accès, avant un usage au-delà d'un réseau de test isolé.
- Projet amont en Rust ; ce port C ESP-IDF est indépendant et sa pertinence pour une PR amont reste à discuter après validation.

- Comparer la batterie expérimentale à l’écran sur plusieurs niveaux et contrôler l’expiration ; les erreurs Ronin ne sont pas décodées. Tester les pertes/reprises Wi-Fi, BLE, mDNS et changement IP sans mouvement automatique.

- Valider DualSense et navigateur réels, Gamepad API en HTTP direct et avec relais localhost, axes et gâchette L2 ; tester focus perdu, onglet caché, arrêt, déconnexion et reconnexion avec stick maintenu. Vérifier que les anciennes sessions sont refusées.
