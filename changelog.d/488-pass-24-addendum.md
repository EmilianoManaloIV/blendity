## 2026-11-11 (round 488): An addendum - Pass 24's table with round 487 in it

**Asked:** "Continue and finish all remaining tasks" - round 487 came after the pass's write-up (round 488, task 0464)
and the write-up said it had not run.

**Changed (task 0464, addendum; docs only)**
- PERFORMANCE's "Pass 24 in one table": the bake's time now, and round 487's row saying what it became (the bake on
  every ticked card, in batches by the cards' speed) where it said "not run".
- Task 0464 has an addendum with the same.

**Checked:** `changelog.py check`; nothing is built.

<!-- docs/PERFORMANCE.md -->
after: | The default bake of the world
| The same bake's lightmaps (round 487: a quiet machine, three runs) | 37.7 / 39.1 / 36.6 s on one card - a card was sent 8192 texels at a time and mostly waited | 17.7 / 17.5 / 17.8 s on one card, 16.2 / 16.2 / 16.5 s on the owner's two (batches by the cards' speed); the same lightmap on one card and on two | 487 |

replace: | 487 | - (0463 was not written) | Not run: light cookies, the Light Volume mist and OIDN's lightmap weights each wait for the owner to ask. |
with: | 487 | 0463 | Light cookies were chosen first (the world has none), then two faults of the owner's bake (dropped after the diagnosis), then what was built: Generate Lighting's gather on every ticked graphics card, in batches of what the cards return in about three quarters of a second. The Light Volume mist and OIDN's lightmap weights still wait for the owner to ask. |
