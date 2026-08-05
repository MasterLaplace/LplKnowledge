# `graph/` a été replié — ne pas l'implémenter

**Date :** 2026-08-05. Ce dossier n'est plus dans le build (`xmake.lua` racine ne l'inclut
plus). Les huit en-têtes qui restent ici sont l'échafaudage de 2026-08-03, écrit **avant**
que `lpl::history` existe dans LplPlugin. Ils décrivent tous quelque chose qui est
aujourd'hui livré ailleurs, testé, et **gaté** — le remplir serait la cinquième duplication
d'affilée du dépôt, et cette fois une duplication d'un module entier.

Le tableau, vérifié fichier par fichier le 2026-08-05 :

| Fichier ici | Ce qui l'implémente déjà | Où |
|---|---|---|
| `Sextuplet.hpp` | `history::Fact` — sujet, prédicat, objet, `[fromYear,toYear]`, source, σ | `LplPlugin/history/include/lpl/history/Fact.hpp` |
| `Trust.hpp` | `history::trustworthiness` + `history::TrustWeights` (proximité / type / consensus, poids **stockés**) | idem |
| `Bayes.hpp` | `history::fuseConfidence` = `1 − (1−p)(1−q)` | idem |
| `SoftLogic.hpp` | la règle de rétrogradation : `history::contradicts` puis démotion **jamais** effacement | `history/src/PossibleWorld.cpp` |
| `Reification.hpp` | un `Fact` **est** la réification : il porte sa source, et l'image porte son locus | `Fact.hpp` + `knowledge/Types.hpp` |
| `PossibleWorld.hpp` | `history::WorldView{admittedSources, seedThreshold, forceThreshold, weights}` + `buildTimeline` | `history/include/lpl/history/PossibleWorld.hpp` |
| `Timeline.hpp` | l'interrogation par intervalle sur une section **triée et cuite** | `knowledge/include/lpl/knowledge/Query.hpp` |
| `Parity.hpp` | gate **P13 `history`**, `timeline=0xC5273B29 chronicle=0x9BD50DB9 minority=0xE9C84CC1` | `history/include/lpl/history/Parity.hpp` |

## Pourquoi le repli plutôt qu'un second exemplaire

Deux raisons, et la seconde est décisive.

**La première est la règle §0 du dépôt** : deux implémentations d'un même concept ne restent
pas égales, elles prennent leur tour d'avoir raison. C'est ce qui est arrivé à
`apps/mapview`, qui portait encore un canal olfactif codé en dur longtemps après que le
moteur l'ait corrigé.

**La seconde est propre à ce cas** : `include/lpl/Foundation.hpp` dit qu'en build autonome
**Fixed32 n'est pas émulé**, parce qu'une fausse virgule fixe laisserait ce dépôt revendiquer
une parité qu'il ne peut pas avoir. Une arithmétique de la confiance écrite ici serait donc,
en autonome, une arithmétique qui **ne peut pas** s'accorder au bit près avec celle du ring 0.
Ce ne serait pas une duplication risquée, ce serait une duplication dont on sait d'avance
qu'elle divergera.

## Ce que LplKnowledge garde, et qui n'est pas dans `history/`

La frontière est celle que `history/Fact.hpp` a **lui-même** écrite : « Subject, predicate and
object are IDENTIFIERS, not strings. **The strings live in LplKnowledge**, which is where a
corpus is curated. » Donc ce dépôt possède, et `history/` ne possède pas :

- **l'identité** — `corpus/` : URN/CTS, locus, langue, et la dérivation déterministe d'un
  identifiant depuis un texte canonique (avec refus en cas de collision) ;
- **le format** — `knowledge/` : l'image `.lplknow`, son vocabulaire, ses sources, ses
  documents, ses loci, son lecteur borné et ses requêtes par intervalle ;
- **la moisson** — `harvest/`, `mirror/`, `media/` : hôte, parce que ça alloue et parse.

`history/` consomme, `knowledge/` fournit. Le pont est `knowledge/History.hpp`, sous
`LPL_HAS_FOUNDATION`, et la gate **P18 `corpus`** mesure que l'aller-retour par l'image ne
perd rien : le corpus de P13, cuit puis relu, doit folder **exactement** les signatures de P13.

## Suite

Le dossier peut être supprimé. Il est laissé en place parce que supprimer un dossier d'un
dépôt appartient à son auteur, pas à une session — mais il ne compile plus, et rien ne
l'inclut.
