# Task 0321: forced perspective - a lens per object

- **Status:** done (round 343, 2026-10-31)
- **Asked:** the 2D-look methods ("Add those components onto the plan."): the opposite of the flatten - a fist or a
  sword swelling toward the camera - and an object drawn through a lens of its own.

## What
- **Camera Flatten's Amount goes below 0:** the depth along the view is stretched about the pivot instead (-1: twice
  the depth), so what reaches toward the camera swells - a punch, a pointed sword.
- **Mode > Focal Length:** the object is drawn as if through a lens of `Focal Length` mm while the camera's own lens
  draws the rest - an 85 mm character before a 24 mm camera's wide background (flatter), or 12 mm for a fist at the
  lens. Seen through the real camera from where the pivot keeps its size, a lens of f mm is the depth kept at the
  camera's focal length over f, so it is the same squash (`flatten_matrix`) with that keep (0.02 to 4).
- The camera's focal length is the views' own (`flatten_view_.focal`, from the projection on a 36 mm-wide frame); an
  orthographic camera has none, and Focal Length mode leaves the object as modelled there.
- **Console:** `flatten <amount -1..1>`, `flatten lens <mm>|off`.

## The review's fixes
- The camera's lens came from the projection's horizontal term: the Game view's window shape, a portrait render or a
  non-36 mm sensor changed the drawn shape. It is the camera's own: a physical camera's Focal Length, else its vertical
  field of view on a 36 x 24 frame (tested at two aspects and with a physical Super 35 camera in portrait).
- An amount typed or the anime preset go back to Amount; the Inspector shows the field the Mode uses; a lens equal to
  the camera's is as modelled (no needless CPU fallback); one example lens (12 mm); the near plane noted.

## Acceptance (`tests_round343`)
- Amount -0.5 draws a point 0.4 m toward the camera from the pivot at 0.6 m.
- Focal Length 85 through a 24 mm camera keeps 24/85 of the depth, 12 mm twice it; an orthographic camera leaves it.
- `flatten lens off` and a refused word.
