# Task 0345: the pen in sculpting

- **Status:** done (round 368, 2026-10-31)
- **Asked:** the plan's last Phase A round. Round 350's comparison (and the user's 2-in-1 with a pen) showed that
  Sculpt read the pen's pressure only. The eraser-end message (`keymap.cpp`) promised it worked "in every brush mode".

## What
- **The eraser end inverts the brush.** In `sculpt_dab`, it counts as Alt: Draw digs, Clay Buildup carves, Inflate
  deflates (the brushes Alt inverts). In Mask mode it takes the mask away. A console dab brings its own pen
  (`sculpt_pen_for_test`).
- **Pen tilt** (`SculptBrush::tilt`, Use Tilt, off at first, with Tilt Strength), after Blender's Use Tilt:
  - The area normal of Clay Buildup, Flatten and Scrape is turned toward the pen's lean by its angle times Tilt
    Strength, capped at 60 degrees. Leaning right tips the plane to the view's right; leaning toward you tips it down.
  - Each symmetry instance leans its own mirrored or turned way.
  - A lean past 5 degrees turns the alpha to its direction instead of the stroke's.
  - `sculpt tilt on|off [strength]`.
- **The Pressure Curve** (`pen_curve_`): a straight line by default. Every dab's pressure goes through it, in Sculpt
  and in Paint. It's drawn in both brush panels and saved in Preferences (`pen_curve=`).
- **E** (`brush.erase`, the Adobe keymap's Eraser) switches Carve in Sculpt, and still toggles Paint's eraser.

## The Opus review's fixes
- **Majors:**
  - **Mirrored dabs leaned the same way**, so symmetry broke with tilt. The lean is now worked out per instance from
    its mirrored view directions.
  - **Adding tan(tilt) to the normal could flip a plane seen side-on** (Clay dug, Scrape filled). The normal is now
    turned by the angle, at most 60 degrees, so it keeps its side.
  - **Tilt was on at full strength by default**, so every resting pen tipped the planes and took over the alpha's
    turn. Use Tilt is now off at first, with a Tilt Strength.
- **Minors:**
  - the help said the eraser inverts "any" brush (Pinch doesn't): it now names the brushes;
  - the prefs were written every frame of a curve drag: now once, after it;
  - a missed console dab left the test pen on;
  - the mask mode reads the test pen;
  - the curve's help says a mouse's dabs go through it too;
  - the E action's label names Carve.
- **Tests:**
  - the tilt's sign (the cut moves left as the plane tips right);
  - Mirror X's two sides moving as mirror images;
  - the normal turned exactly 40 degrees, and a side-on one not flipped;
  - the eraser taking the mask away.
- **A test leak:** round 360's atlas test left a baked material and its images in the scratch project each run
  ("Cube (1) N Baked"), which crowded the Materials window's grid until the multi-select test failed. It now removes
  them.

## Checked
- **`tests_round368`:**
  - the volume rises with a Draw dab and falls below the start after two eraser dabs, and Carve is left off;
  - a Scrape dab off the centre moves 2% along the view's right upright and 65% leaning 40 degrees;
  - the bowed curve maps 0.5 to 0.1, and a half-pressure dab adds under a tenth of a full one;
  - E switches Carve in Sculpt;
  - the Try-it.

## Not done
- **Tilt for Draw's direction** (Blender's Tilt Strength on Draw) and for Paint's brushes: the round's scope was the
  plane brushes and the alpha.
- **A live check on the user's pen:** the tests set the pen through the test hook. The pen events' tilt and eraser fields
  are the Draw tab's (task 0188).
