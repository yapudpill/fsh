# Structure du Projet

Le projet est organisé de manière à suivre les bonnes pratiques en termes de
structure de fichiers et de répertoires. Cette organisation permet de maintenir
le code clair, modulable et facile à maintenir, comme expliqué
[dans cet article de Luca Vallini](https://www.lucavall.in/blog/how-to-structure-c-projects-my-experience-best-practices).

- **`src/`** : Contient tous les fichiers source du projet. Chaque fichier `.c`
  y est rangé par fonctionnalité ou module.
- **`include/`** : Contient tous les fichiers d'en-têtes `.h` nécessaires pour
  les déclarations et interfaces publiques des modules.
- **`build/`** : Contient tous les fichiers objets `.o` générés lors de la
  compilation.

Cette structure assure une séparation claire entre les fichiers de code source,
les interfaces et les fichiers de compilation, facilitant ainsi la gestion du
projet et la collaboration.

# Parsing

## Types de commandes

Tous les types de commandes sont déclarés dans le fichier `cmd_types.h`.

Les commandes sont représentées par une liste chaînée de `struct cmd`. Chaque
élément de la liste possède 4 champs :
- **`enum cmd_type cmd_type`** : le type de commande que cet élément représente.
  Les valeurs possibles pour ce champ sont `CMD_EMPTY`, `CMD_SIMPLE`,
  `CMD_IF_ELSE`, et `CMD_FOR`.
- **`void *detail`** : un pointeur vers une structure contenant les informations
  de la commande. En fonction de la valeur de `cmd_type`, il doit être casté
  vers l'un des types suivants : `cmd_simple`, `cmd_if_else` et `cmd_for`.
- **`enum newt_type next_type`** : le type de lien que cette commande a avec la
  suivante. Les valeurs possibles pour ce champ sont `NEXT_NONE`, `NEXT_PIPE` et
  `NEXT_SEMICOLON`.
- **`struct cmd *next`** : un pointeur vers la commande suivante si `next_type`
  n'est pas `NEXT_NONE`.

Voici le contenu des 3 types de détail possibles :
- **`cmd_simple`** :
  - Deux champs `argc` et `argv` représentant la commande simple en elle même
  - Trois champs `in`, `out` et `err` contenant un nom de fichier en cas de
    redirection, ou `NULL` s'il n'y en a pas
  - Deux champs `out_type` et `err_type` représentant le type de redirection
    pour les sorties. Les valeurs de ces champs sont `REDIR_NONE` (pas de
    redirection), `REDIR_NORMAL` (`>`), `REDIR_APPEND` (`>>`) et
    `REDIR_OVERWRITE` (`>|`).
- **`cmd_if_else`** : trois pointeurs de commandes `cmd_test`, `cmd_then` et
  `cmd_else`, pointant respectivement vers le test du if...else, la branche
  "true" et la branche "false".
- **`cmd_for`** :
  - Deux champ `var_name` et `dir_name` contenant le nom de la variable de
    boucle et du répertoire sur lequel on itère.
  - Cinq champs représentant chacune des options possibles : `list_all` (`-A`),
    `recursive` (`-r`), `filter_ext` (`-e`), `filter_type` (`-t`) et
    `parallel` (`-p`).
  - Un pointeur de commande `body` pointant vers le corps de la boucle.

## Stratégie de parsing

Tout le parsing se passe dans le fichier `parsing.c`.

La stratégie adoptée est un parsing en un seul passage en construisant la liste
chaînée au fur et à mesure. Toutes les fonctions de parsing partagent une unique
variable `token` pour communiquer en elles. Cette variable pointe toujours vers
le prochain mot à parser.

La fonction `parse_cmd` est la fonction principale du fichier, c'est elle qui
créé le chaînage, détermine le type de commande et appelle les bonnes fonctions
dédiées. Chaque fonction dédiée est chargée de remplir le pointeur
`struct cmd *out` qui leur est donné en créant renseignant le bon type, en
créant le détail et en le liant à la structure.

En cas d'erreur de parsing, la fonction actuellement en charge du parsing
affiche un message sur la sortie erreur et renvoie -1. Ce -1 est propagé jusqu'à
la fonction `parse` qui appelle `free_cmd` à la racine de la liste. Pour que
cela fonctionne bien, toutes les fonctions de `parsing.c` garantissent que quand
elles se terminent, la liste est valide et tous les blocs alloués jusque là y
ont été attachés. Y compris (et surtout) après une erreur.
