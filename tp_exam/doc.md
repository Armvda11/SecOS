# Documentation Technique — Projet SecOS

Cette documentation décrit les **choix d’implémentation concrets** réalisés pour la gestion de la mémoire, de la préemption et des privilèges dans le cadre du projet SecOS.
Elle met l’accent sur les **décisions de conception** et leur **traduction directe dans le code**.

---

## 1. Cartographie mémoire (synthèse)

Le système utilise un **mélange de pages de 4 Mo et de pages de 4 Ko** :
- pages **4 Mo** pour simplifier le mapping du noyau et du code utilisateur,
- pages **4 Ko** pour les zones nécessitant une granularité fine (pile user, mémoire partagée).

| Segment            | Adresse Physique | Adresse Virtuelle (T1 / T2)        | Taille |
|--------------------|------------------|------------------------------------|--------|
| Noyau & I/O        | `0x00000000`     | `0x00000000` (identity mapping)    | 4 Mo   |
| Code utilisateur   | `0x00400000`     | `0x00400000` (identity mapping)    | 4 Mo   |
| Page partagée      | `0x00A00000`     | `0x10000000` / `0x20000000`        | 4 Ko   |
| Pile utilisateur   | `ustack_tX`      | `0x40001000` (sommet de pile)      | 4 Ko   |

---

## 2. Choix techniques et réalisation

### A. Alignement stratégique de la section `.user`

**Choix**

Placer le code utilisateur à l’adresse physique **0x00400000 (4 Mo)**.

**Réalisation**

La section `.user` est alignée sur une frontière de 4 Mo, ce qui permet :
- de mapper l’intégralité du code utilisateur via **une seule entrée de répertoire de pages (PDE)**,
- d’utiliser le bit **PDE_PS** (Page Size) pour activer une page de 4 Mo.

Ce choix simplifie la configuration du PGD de chaque tâche et garantit un mapping clair et prévisible du code utilisateur.

---

### B. Isolation par répertoires de pages distincts

**Choix**

Attribuer à chaque tâche un **répertoire de pages distinct** (`pgd_t1`, `pgd_t2`).

**Réalisation**

Chaque tâche possède :
- son propre **CR3**,
- son propre PGD,
- ses propres tables de pages pour la pile user et la mémoire partagée.

Le noyau reste identity-mappé dans chaque espace d’adressage afin de garantir :
- le bon fonctionnement des interruptions,
- l’accès au code noyau quel que soit le contexte de tâche.

La page partagée est mappée :
- à `V_SHARED_T1` pour T1,
- à `V_SHARED_T2` pour T2,

tout en pointant vers **la même page physique (`0x00A00000`)**, satisfaisant ainsi la contrainte de mémoire partagée à adresses virtuelles différentes.

---

### C. Gestion du TSS pour la bascule de privilèges

**Choix**

Utiliser **un unique segment TSS**, mis à jour dynamiquement.

**Réalisation**

Lors d’une transition Ring 3 → Ring 0 (IRQ ou syscall), le processeur utilise le couple `(SS0, ESP0)` contenu dans la TSS pour basculer sur la pile noyau.

Dans ce projet :
- `my_tss.esp0` est **mis à jour à chaque changement de tâche** dans `do_timer_logic`,
- chaque tâche dispose ainsi de sa **propre pile noyau** lors des interruptions.

Sans cette mise à jour dynamique, une interruption provenant d’une tâche ring3 ne pourrait pas sauvegarder correctement son contexte.

---

### D. Ordonnancement par « forgeage » de pile

**Choix**

Initialiser manuellement le contexte d’exécution des tâches utilisateur avant leur premier lancement.

**Réalisation**

La fonction `forge_context()` construit artificiellement une pile noyau contenant :
- les segments utilisateur (CS, DS, etc.),
- un `EFLAGS` valide (`IF=1`),
- un `EIP` pointant vers `user1` ou `user2`.

Ainsi, lorsque le wrapper d’interruption termine par `iret`, le processeur :
- croit retourner d’une interruption,
- bascule en réalité pour la première fois en **Ring 3**.

Ce mécanisme permet de démarrer une tâche utilisateur sans instruction spéciale dédiée.

---

### E. Interface d’appel système (int 0x80)

**Choix**

Passage du paramètre par registre (**EBX**).

**Réalisation**

La tâche utilisateur :
- place l’adresse virtuelle du compteur dans `EBX`,
- déclenche `int 0x80`.

Côté noyau :
- l’entrée **IDT[0x80]** est configurée avec un **DPL=3**,
- le wrapper sauvegarde le contexte,
- `do_syscall_logic()` récupère la valeur de `EBX` directement depuis la pile de sauvegarde (`stack[8]`).

Ce choix évite toute copie de données complexe et permet de travailler directement avec des pointeurs utilisateur.

---

## 3. Note sur la sécurité et les permissions

Les flags **PDE_USER** et **PDE_RW** sont appliqués uniquement sur :
- le code utilisateur,
- les piles utilisateur,
- la page partagée.

Le noyau reste majoritairement protégé, tout en conservant un identity mapping pour garantir la stabilité des interruptions et du bootstrap initial.


--- 

## 4. Limites connues et points de vigilance

### 4.1 Protection stricte noyau / utilisateur

Une séparation stricte des droits (suppression complète de `PDE_USER` sur les mappings bas) a été envisagée.
Cependant, le **bootstrap initial vers le Ring 3**, basé sur une frame `iret` forgée, reste sensible à l’emplacement exact des mappings.

Par manque de temps, une protection totalement hermétique n’a pas été finalisée sans compromettre l’exécution.

---


## Conclusion

Le système implémente :
- un ordonnancement préemptif fonctionnel,
- une séparation Ring 0 / Ring 3 correcte,
- une gestion mémoire par tâche avec CR3 distincts,
- une zone de mémoire partagée conforme,

