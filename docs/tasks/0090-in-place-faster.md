# 0090: In place, faster - only moved faces split again, face normals in parallel

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** none (ADR 0034's path, made cheaper)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
After task 0086 a drag on a 50k-face mesh still spent about 9 ms a frame moving its two render caches in place:
every quad was split into triangles again and every face's normal worked out on one thread, though a drag moves a
handful of vertices.

## Scope
- In: `update_in_place` notes which vertices moved (their position differs from the one the cache drew) before it
  writes anything; `retriangulate` splits only the faces with a moved vertex again (a face's triangles depend on its
  own corners alone). `corner_normals` works out each face's normal and weighted normal in parallel; what adds faces
  up stays in face order, so the normals are the same to the bit.
- Out: smoothing only around the moved vertices (the vertex sums would have to be kept per topology).

## Acceptance criteria
1. Round 112's equivalence tests still find no difference from a rebuild (48 drag steps, six kinds of mesh).
2. A cache read after moves it missed, a vertex moved and back to the same bits, and a refused update followed by
   more moves all draw what a rebuild draws.

## As built
- Tests: round 116 (and rounds 112-113's, unchanged). Stress (laptop setting, 50k faces): the two caches moved in
  place 9.1 -> 6.7 ms a frame; the gizmo drag 35.4 -> 33.7 ms, G 34.4 -> 33.0 ms.
