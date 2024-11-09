## Structure du Projet

Le projet est organisé de manière à suivre les bonnes pratiques en termes de structure de fichiers et de répertoires. Cette organisation permet de maintenir le code clair, modulable et facile à maintenir, comme expliqué [dans cet article de Luca Vallini](https://www.lucavall.in/blog/how-to-structure-c-projects-my-experience-best-practices).

### Détails de l'Organisation

- **`src/`** : Contient tous les fichiers source du projet. Chaque fichier `.c` y est rangé par fonctionnalité ou module.
- **`include/`** : Contient tous les fichiers d'en-têtes `.h` nécessaires pour les déclarations et interfaces publiques des modules.
- **`build/`** : Contient tous les fichiers objets `.o` générés lors de la compilation.

Cette structure assure une séparation claire entre les fichiers de code source, les interfaces et les fichiers de compilation, facilitant ainsi la gestion du projet et la collaboration.
