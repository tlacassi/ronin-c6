# Suite et validation

- Confirmer ce que désigne « pignon bas » et la référence du moteur de focus.
- Identifier le nom BLE complet et la carte C6 exacte ; flasher après configuration locale.
- Vérifier découverte, reconnexion, latence de coexistence et arrêt lors de chaque perte de liaison, y compris perte d'alimentation ESP. Relever les axes, signes et unités sur le RS 3.
- Identifier une commande focus BLE fondée sur des captures reproductibles avant de l'activer. Chercher une lecture de position et valider une commande absolue avant de proposer des mémoires.
- Ajouter découverte mDNS, sélection explicite et arbitrage des utilisateurs, puis protection de l'accès, avant un usage au-delà d'un réseau de test isolé.
- Projet amont en Rust ; ce port C ESP-IDF est indépendant et sa pertinence pour une PR amont reste à discuter après validation.
