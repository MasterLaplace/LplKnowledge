# LplKnowledge

**La mémoire du projet Laplace** — corpus, index, graphe temporel, archive.

Quatrième dépôt du projet, aux côtés de [LplKernel](https://github.com/MasterLaplace/LplKernel)
(la machine), LplPlugin (le monde) et LplAssistant (la voix). Un démon de Laplace se
définit par deux choses à égalité : les lois qui régissent son monde, et **ce qu'il
sait**. Ce dépôt est la seconde.

## Architecture

Même contrat dual que LplPlugin, et la même frontière : **lecteur / écrivain**.
Moissonner les archives du monde demande un tas, un réseau et du parsing de texte ;
lire ce qui a été moissonné n'en demande aucun — et le démon en ring 0 est le lecteur
le moins capable qui existe.

| Module | Cible | Rôle |
| --- | --- | --- |
| `knowledge/` | freestanding | le lecteur borné d'une image `.lplknow`, ses requêtes, sa provenance |
| `corpus/` | freestanding | identité et adressage des textes sources (URN/CTS, locus, langue) |
| `harvest/` | hôte | ingestion : lecture d'un corpus, four `.lplknow` |
| `mirror/` | hôte | miroirs hors-ligne de documentation de référence |
| `media/` | hôte | extraire la substance d'une vidéo sans la regarder |

```
lpl-ingest      moissonne un corpus markdown en image .lplknow
lpl-knowbake    cuit le magasin en image .lplknow
lpl-wikimirror  construit le miroir de documentation hors-ligne
lpl-keyframe    extrait les images informatives d'une archive média
lpl-ask         interroge le pack depuis un terminal
```

C, C++ et assembleur uniquement. Les implémentations de référence de ce domaine sont
en Python et bridées par lui ; les réécrire nativement n'est pas une préférence de
style, c'est le levier déclaré pour rendre l'échelle abordable.

## Build autonome

Ce dépôt se construit et se teste **seul**, sans LplPlugin ni LplKernel — comme
LplPlugin se construit sans LplKernel.

```sh
xmake f --root --foundation=detect  # défaut : utilise LplPlugin s'il est là
xmake f --root --foundation=off     # force le build autonome, hôte uniquement
xmake f --root --foundation=force   # échoue si le socle est absent (pour la CI)
xmake --root
```

| Mode | Ce qui est disponible |
| --- | --- |
| socle présent (`LPL_HAS_FOUNDATION`) | contrat de déterminisme, compilation `-ffreestanding` possible |
| autonome | hôte uniquement — **Fixed32 n'est pas émulé** |

⚠ Les mots sont `detect|force|off` et non `auto|y|n` : xmake **booléanise** une option
dont les valeurs ressemblent à des booléens, si bien que `n` *et* `auto` étaient tous deux
stockés comme `false` — le défaut documenté et l'opt-out devenaient le même réglage, et
chaque build « autonome » détectait le socle en silence.

`include/lpl/Foundation.hpp` est le seul endroit qui connaît la différence. Il n'offre
**aucun substitut** à la virgule fixe : une fausse Fixed32 laisserait un build autonome
revendiquer une parité qu'il ne peut pas avoir.

> ⚠ **`graph/` a été replié**, il n'est plus dans le build. Ses huit en-têtes décrivaient
> tous quelque chose que `lpl::history` (LplPlugin, gate P13) implémente et gate déjà — le
> sextuplet, la fiabilité des sources, la fusion bayésienne, la rétrogradation d'une
> affirmation contredite, les mondes possibles — ou que `knowledge/Query.hpp` fait.
> La raison, fichier par fichier, est dans [graph/FOLDED.md](graph/FOLDED.md).
> La frontière est celle que `history/Fact.hpp` a écrite lui-même : il commerce en
> **identifiants**, et les chaînes vivent ici.

## État

`knowledge/` et `corpus/` sont **implémentés et gatés** ; `harvest/` l'est pour son four
(`harvest::Baker`) et reste un échafaudage pour l'ingestion ; `mirror/` et `media/` sont
encore des échafaudages. Chaque fichier non écrit porte son en-tête expliquant pourquoi il
existe et un `TODO(lot N)` à la place du code — un stub qui renverrait un succès non mérité
ressemblerait à une fonctionnalité qui marche.

### Gate P18 `corpus`

Le corpus canonique de la gate P13 est **cuit en image `.lplknow`, relu, et l'histoire est
reconstruite depuis ce qui est revenu**. Les trois signatures d'histoire doivent égaler
celles de P13, sur l'hôte **et** en ring 0 (`libknowledge.a`, cinquième cible du noyau).

Ce qui est éprouvé n'est pas un calcul mais une **traduction** : une image qui s'ouvre
proprement et qui aurait arrondi une confiance passerait tout le reste, et la conséquence
serait un consensus différent sur la mort d'un roi.

```sh
xmake run test-corpus-identity     # identité, adressage, refus du lecteur (sans socle)
xmake run test-harvest-ingest      # lecture d'un corpus (sans socle)
xmake run test-knowledge-parity    # gate P18 — l'aller-retour ne perd rien
```

## Le corpus du projet lui-même

Le premier corpus réel, et le seul à portée : les documents des quatre dépôts. Il existe,
il n'a aucune contrainte de licence — c'est l'écrit de l'auteur —, il tient dans une image
qu'un noyau peut porter, et il a un lecteur dès le premier jour.

```sh
xmake run lpl-ingest laplace.lplknow ~/LplKernel ~/LplKnowledge ~/LplAssistant
xmake run lpl-ask laplace.lplknow --define SIM-016
```

```
SIM-016
  | SIM-016 | ★★ Le sextuplet | La structure canonique retenue : (S, P, O, [t₀,t₁], …
  defined in LplKnowledge/store/LplKnowledge/EXTRACTION.md — line 111
  cited 5 times
    LplKernel/CLAUDE.md — line 1669
    …
```

La lecture est **structurelle** : quels identifiants stables existent, où chacun est défini,
qui le cite, et les dates portées par les titres. Extraire des *affirmations* de la prose
demande un modèle — c'est le travail du côté inférence, et le deviner par motifs produirait
des confiances qui ne veulent rien dire.

Le partage qui rend la chose tenable est entre le **graphe** et le **texte** : le graphe est
dérivé, donc jamais faux longtemps ; le texte d'une définition est écrit par quelqu'un et
voyage verbatim dans la section `Texts` de l'image. Un index cesse d'être une table qu'on
édite et devient une **vue qu'on rend** : éditer le document, ré-ingérer, re-rendre.

Mesuré sur le corpus réel : **92 documents, 533 identifiants, 699 citations, zéro référence
cassée, et exactement un identifiant défini deux fois** — un constat qu'aucune relecture
humaine n'avait produit.

### Où vivent les documents

Ils sont rassemblés dans **`store/`**, par dépôt d'origine (`store/LplKernel/`,
`store/LplAssistant/`, `store/LplKnowledge/`) : une provenance qu'on perd est une citation
qu'on ne peut plus vérifier.

⚠ **`store/` est gitignoré, délibérément.** Ce dépôt est public et certains de ces documents
sont des conversations privées. Le défaut est donc « privé », et publier quelque chose est un
geste explicite — l'inverse serait un dépôt open source qui publie par accident. `*.lplknow`
l'est pour la même raison : l'image cuite porte le **texte** de chaque définition.

Ce qui **ne bouge pas**, parce qu'un outil le lit à son emplacement et ne sait pas le chercher
ailleurs — `lpl-ingest` les lit donc sur place :

| Reste là où il est | Qui le lit là |
| --- | --- |
| `CLAUDE.md`, les quatre `README.md` | Claude Code charge le premier depuis la racine du projet ; GitHub rend les seconds où ils sont |
| `LplKernel/docs/Books/LplKernel_Book.md` | `site/scripts/split-book.mjs` le découpe depuis ce chemin |
| `LplKernel/docs/ROADMAP.md` | suivi en git, cité par le site |
| `LplKernel/.claude/**` | le harnais y charge les skills, l'agent `book-humanizer`, le profil de voix |
| `LplKernel/.github/**` | GitHub ne les lit qu'à cet emplacement |
| `site/src/content/blog/` | pages Astro, pas des notes |

⚠ Ce qui est **exclu** de l'ingestion, et pourquoi : la sortie de build, les vues générées
(`site/src/content/book/`, écrit par `split-book.mjs`) et les **dépôts tiers clonés**
(`repos_storage/`). Mesuré : sans ces exclusions la lecture voyait 711 documents pour le même
savoir — 87 % de bruit, et la documentation de llama.cpp n'est pas la connaissance de ce
projet.

Le plan par lots est dans `store/LplKernel/RAPPORT_LplKnowledge_synthese_et_plan.md`, et le
savoir source extrait dans `store/LplKnowledge/EXTRACTION.md`.

## Licence

MIT. Auteur : [@MasterLaplace](https://github.com/MasterLaplace).
