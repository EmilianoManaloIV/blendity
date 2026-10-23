# 0143: The texture finder

- **Status:** done
- **Requirements:** R-6
- **Decisions:** ADR 0047
- **Model:** main session
- **PR:** (round 162, stacked on tasks 0136-0142)

## Goal
A model dropped in finds its own textures in the folders around it, whatever the layout and the naming, and says
what it paired, so nothing has to be hunted for and assigned by hand (a wrong guess is one Inspector slot).

## Scope
- In: the crawl (`texture_files`): the model's folder and three levels below, then the folders beside it two levels
  down; nearest first; images only; at most 2,000; never Downloads, Desktop, Documents, a home folder, AppData or a
  drive root, and not folders holding over 50 other files (unless the model's own). A model loose in such a folder
  takes only what lies right beside it.
- In: `kind_of` from the last one or two words, trailing `map` / `tex` / sizes (`2k`, `4096`, `1001`) dropped; AO,
  opacity and height recognised and reported, not applied.
- In: `similarity` (same key 1; one inside the other 0.9; the share of words in common) and `best_match` through the
  material's name and the names of the meshes it alone covers, less 0.05 a folder level away; the best above 0.6
  wins, nearest folder first; the leftover paired 1:1 by
  best when each has its own; a lone material (the import's own, or the model's only) with a lone set pairs.
- In: notes: the pairing, the materials left bare, the sets seen but unused, the maps with no slot.
- Out: looking inside archives; reading image contents to guess a kind; a preference to turn the finder off (a
  wrong match is undone in the Inspector).

## Acceptance criteria
1. Sketchfab's layout with two-word names: each material textured from `textures/`, the file's flat colours kept,
   AO reported.
2. Likeness pairs `Body` with `T_character_body_2k_BaseColor`; a lone `Hand_R.png` textures nothing.
3. Loose in a folder called Downloads: nothing from the folders around; in a folder of its own there: as usual.
4. The editor copies the found textures into `Assets/Textures/<model>/`.
5. The user's `carmen-limbus-company` folder: CarmenB, CarmenF and CarmenH textured, OL bare.

## As built
- `scene/import_materials.cpp` (the finder section); `ImportResult::listing_distance`, `listing_beside`; tests:
  round 162.
- After the review:
  - sidecars (Unity's `.meta`, `.import`, `.txt`, `.json`, `.mat`) count with their images, so a Unity Textures folder
    is read; the "folder of other files" rule needs over 50 of them and twice the images;
  - the crawl has a budget (400 listings, about 150 ms, 2,000 images; a folder of over 200 subfolders is a library
    root) and says in the notes when it stopped early;
  - the home folder and the temp folder are stops by place, `Users` / `Program Files` / `Windows` only at a drive
    root, Downloads / Desktop / Documents / AppData by name;
  - sets are one per folder and name (two projects' `Body` sets stay apart), each map keeping its own distance; a
    set of only AO / height / opacity maps is no candidate (reported under "no slot");
  - the lone-set rule counts the model's own folder tree only (round 154's meaning);
  - likeness: one name's words must be a run inside the other's (`arm` is not in `armor`); numbers are left out of
    the shared words; the 1:1 leftover rule needs a likeness of 0.3;
  - a moved link takes the nearest same-named file; glTF's `metallicRoughness` as a loose file is split;
  - the notes cap every list at 8, say "no texture found by name for" when nothing matched at all, and count only the
    maps applied;
  - round 154's test models sit two folders deep so the folders beside them are their own; new tests for the Unity
    folder, a height-only set, `Wood_01` / `Rock_01`, `Arm` / `Armor`, and the user's folder made synthetic.
